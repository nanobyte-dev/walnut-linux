/* The spufs backend: one context per test through spu_create, the program loaded through the
 * mem file, r3/r4/r5 through regs (as libspeq does, ps3-spu/lib/spe.c), the mailboxes and
 * signal registers through their files. Contexts are created with SPU_CREATE_EVENTS_ENABLED:
 * without it a DMA fault would kill this whole process with SIGSEGV. */
#define _GNU_SOURCE
#include <dirent.h>
#include <elf.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "spt.h"

#define SPU_CREATE_EVENTS_ENABLED 1
#ifdef SYS_spu_create
#define SPT_SPU_CREATE(path, flags, mode) syscall(SYS_spu_create, path, flags, mode)
#define SPT_SPU_RUN(fd, npc, ev) syscall(SYS_spu_run, fd, npc, ev)
#else	/* not powerpc: no spufs (the unit tests use the simulator backend) */
#define SPT_SPU_CREATE(path, flags, mode) (errno = ENOSYS, -1)
#define SPT_SPU_RUN(fd, npc, ev) (errno = ENOSYS, -1)
#endif
#define LS_SIZE (256 * 1024)
#ifndef EM_SPU
#define EM_SPU 23
#endif

struct sc {
	int dir, mbox, ibox, wbox, sig1, sig2, sig1t, sig2t, phys;
	unsigned npc;
	volatile int intr;
	char path[256];
};

static char mnt_path[128] = "/spu";
static unsigned ctx_serial;

static int probe(char *mnt, unsigned len)
{
	char dev[64], dir[256], type[32];
	FILE *f = fopen("/proc/mounts", "r");
	unsigned n = 0, k;
	struct dirent *d;
	DIR *s;
	int found = 0;

	if (!f)
		return 0;
	while (fscanf(f, "%63s %255s %31s %*[^\n]", dev, dir, type) == 3)
		if (!strcmp(type, "spufs") && strlen(dir) < len) {
			strcpy(mnt, dir);
			found = 1;
			break;
		}
	fclose(f);
	if (!found)
		return 0;
	snprintf(mnt_path, sizeof(mnt_path), "%s", mnt);
	s = opendir("/sys/devices/system/spu");
	if (!s)
		return 0;
	while ((d = readdir(s)))
		if (sscanf(d->d_name, "spu%u", &k) == 1)
			n++;
	closedir(s);
	return n;
}

static int spe_ids(unsigned *ids, unsigned max)
{
	struct dirent *d;
	DIR *s = opendir("/sys/devices/system/spu");
	unsigned n = 0, k, i, j;

	if (!s)
		return 0;
	while ((d = readdir(s)))
		if (sscanf(d->d_name, "spu%u", &k) == 1 && n < max)
			ids[n++] = k;
	closedir(s);
	for (i = 0; i < n; i++)	/* sorted */
		for (j = i + 1; j < n; j++)
			if (ids[j] < ids[i]) {
				k = ids[i];
				ids[i] = ids[j];
				ids[j] = k;
			}
	return (int)n;
}

static void sigusr1(int sig) { (void)sig; }

static int set_reg(int fd, unsigned n, uint64_t v)
{
	uint8_t qw[16] = { 0 };

	v = htobe64(v);
	memcpy(qw, &v, 8);
	return pwrite(fd, qw, 16, n * 16) == 16 ? 0 : -errno;
}

static int create(struct spt_ctx *c, const char *elf, uint64_t r3, uint64_t r4, uint64_t r5)
{
	struct sc *s = calloc(1, sizeof(*s));
	struct sigaction sa;
	struct stat st;
	uint8_t *img, *ls;
	const Elf32_Ehdr *eh;
	const Elf32_Phdr *ph;
	int fd, i, ret = 0;

	if (!s)
		return -ENOMEM;
	s->dir = s->mbox = s->ibox = s->wbox = s->sig1 = s->sig2 = s->sig1t = s->sig2t = s->phys = -1;
	/* run() is stopped with SIGUSR1: the handler does nothing and must not restart the call */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigusr1;
	sigaction(SIGUSR1, &sa, NULL);

	fd = open(elf, O_RDONLY | O_CLOEXEC);
	if (fd < 0 || fstat(fd, &st)) {
		ret = -errno;
		goto fail;
	}
	img = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (img == MAP_FAILED) {
		ret = -errno;
		goto fail;
	}
	eh = (const Elf32_Ehdr *)img;
	if ((size_t)st.st_size < sizeof(*eh) || memcmp(eh->e_ident, ELFMAG, SELFMAG) ||
	    be16toh(eh->e_machine) != EM_SPU) {
		munmap(img, st.st_size);
		ret = -ENOEXEC;
		goto fail;
	}
	snprintf(s->path, sizeof(s->path), "%s/spetest-%d-%u", mnt_path, getpid(),
		 __sync_fetch_and_add(&ctx_serial, 1));
	s->dir = SPT_SPU_CREATE(s->path, SPU_CREATE_EVENTS_ENABLED, 0700);
	if (s->dir < 0) {
		ret = -errno;
		munmap(img, st.st_size);
		goto fail;
	}
	fcntl(s->dir, F_SETFD, FD_CLOEXEC);
	fd = openat(s->dir, "mem", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		ret = -errno;
		munmap(img, st.st_size);
		goto fail;
	}
	ls = mmap(NULL, LS_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (ls == MAP_FAILED) {
		ret = -errno;
		munmap(img, st.st_size);
		goto fail;
	}
	ph = (const Elf32_Phdr *)(img + be32toh(eh->e_phoff));
	for (i = 0; i < be16toh(eh->e_phnum); i++) {
		uint32_t off = be32toh(ph[i].p_offset), fsz = be32toh(ph[i].p_filesz);
		uint32_t va = be32toh(ph[i].p_vaddr), msz = be32toh(ph[i].p_memsz);

		if (be32toh(ph[i].p_type) != PT_LOAD)
			continue;
		if (va > LS_SIZE || msz > LS_SIZE - va || fsz > msz || off > (uint32_t)st.st_size ||
		    fsz > (uint32_t)st.st_size - off) {
			ret = -ENOEXEC;
			break;
		}
		memcpy(ls + va, img + off, fsz);
		memset(ls + va + fsz, 0, msz - fsz);
	}
	s->npc = be32toh(eh->e_entry);
	munmap(ls, LS_SIZE);
	munmap(img, st.st_size);
	if (ret)
		goto fail;
	fd = openat(s->dir, "regs", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		ret = -errno;
		goto fail;
	}
	ret = set_reg(fd, 3, r3);
	if (!ret)
		ret = set_reg(fd, 4, r4);
	if (!ret)
		ret = set_reg(fd, 5, r5);
	close(fd);
	if (ret)
		goto fail;
	s->mbox = openat(s->dir, "mbox", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	s->ibox = openat(s->dir, "ibox", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	s->wbox = openat(s->dir, "wbox", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	s->sig1 = openat(s->dir, "signal1", O_WRONLY | O_CLOEXEC);
	s->sig2 = openat(s->dir, "signal2", O_WRONLY | O_CLOEXEC);
	s->sig1t = openat(s->dir, "signal1_type", O_RDWR | O_CLOEXEC);
	s->sig2t = openat(s->dir, "signal2_type", O_RDWR | O_CLOEXEC);
	s->phys = openat(s->dir, "phys-id", O_RDONLY | O_CLOEXEC);
	if (s->mbox < 0 || s->wbox < 0) {
		ret = -ENOENT;
		goto fail;
	}
	c->priv = s;
	return 0;
fail:
	c->priv = s;
	/* leave nothing behind: closing the directory fd removes the context */
	{
		int *f[] = { &s->mbox, &s->ibox, &s->wbox, &s->sig1, &s->sig2, &s->sig1t, &s->sig2t, &s->phys, &s->dir };
		unsigned k;

		for (k = 0; k < sizeof(f) / sizeof(f[0]); k++)
			if (*f[k] >= 0)
				close(*f[k]);
	}
	free(s);
	c->priv = NULL;
	return ret ? ret : -EIO;
}

static long run(struct spt_ctx *c, unsigned *event)
{
	struct sc *s = c->priv;
	long ret;

	for (;;) {
		*event = 0;
		ret = SPT_SPU_RUN(s->dir, &s->npc, event);
		if (ret >= 0)
			return ret;
		if (errno == EINTR && !s->intr)
			continue;	/* a stray signal: carry on from npc */
		return -errno;
	}
}

static int physid(struct spt_ctx *c)
{
	struct sc *s = c->priv;
	char b[64];
	ssize_t n;
	unsigned long long v;

	if (s->phys < 0)
		return -1;
	n = pread(s->phys, b, sizeof(b) - 1, 0);
	if (n <= 0)
		return -1;
	b[n] = 0;
	v = strtoull(b, NULL, 0);
	return v < 64 ? (int)v : -1;
}

static long now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int mbox_put(struct spt_ctx *c, uint32_t w, int timeout_ms)
{
	struct sc *s = c->priv;
	struct pollfd p = { .fd = s->wbox, .events = POLLOUT };
	long end = now_ms() + timeout_ms;

	for (;;) {
		ssize_t n = write(s->wbox, &w, 4);	/* a native word, as the kernel takes it */

		if (n == 4)
			return 0;
		if (n < 0 && errno != EAGAIN && errno != EINTR)
			return -errno;
		if (c->done || now_ms() >= end)
			return -ETIMEDOUT;
		poll(&p, 1, 5);
	}
}

/* mbox has no poll in spufs and returns EAGAIN while empty: spin a little, then sleep */
static int mbox_get(struct spt_ctx *c, uint32_t *w, int timeout_ms)
{
	struct sc *s = c->priv;
	long end = now_ms() + timeout_ms;
	unsigned spins = 0;

	for (;;) {
		ssize_t n = read(s->mbox, w, 4);

		if (n == 4)
			return 0;
		if (n < 0 && errno != EAGAIN && errno != EINTR)
			return -errno;
		if (now_ms() >= end)
			return -ETIMEDOUT;
		if (c->done) {	/* the SPU has stopped: one last look, then give up */
			n = read(s->mbox, w, 4);
			return n == 4 ? 0 : -ECANCELED;
		}
		if (++spins < 200)
			sched_yield();
		else
			usleep(100);
	}
}

static int ibox_get(struct spt_ctx *c, uint32_t *w, int timeout_ms)
{
	struct sc *s = c->priv;
	struct pollfd p = { .fd = s->ibox, .events = POLLIN };
	long end = now_ms() + timeout_ms;

	if (s->ibox < 0)
		return -ENOENT;
	for (;;) {
		ssize_t n = read(s->ibox, w, 4);

		if (n == 4)
			return 0;
		if (n < 0 && errno != EAGAIN && errno != EINTR)
			return -errno;
		if (now_ms() >= end)
			return -ETIMEDOUT;
		if (c->done) {
			n = read(s->ibox, w, 4);
			return n == 4 ? 0 : -ECANCELED;
		}
		poll(&p, 1, 5);
	}
}

static int signal_put(struct spt_ctx *c, int which, uint32_t v)
{
	struct sc *s = c->priv;
	int fd = which == 1 ? s->sig1 : s->sig2;

	if (fd < 0)
		return -ENOENT;
	return write(fd, &v, 4) == 4 ? 0 : -errno;
}

static int signal_type(struct spt_ctx *c, int which, int or_mode)
{
	struct sc *s = c->priv;
	int fd = which == 1 ? s->sig1t : s->sig2t;

	if (fd < 0)
		return -ENOENT;
	return write(fd, or_mode ? "1" : "0", 1) == 1 ? 0 : -errno;
}

static void interrupt(struct spt_ctx *c)
{
	struct sc *s = c->priv;

	s->intr = 1;
	pthread_kill(c->runner, SIGUSR1);
}

static void destroy(struct spt_ctx *c)
{
	struct sc *s = c->priv;
	int *f[] = { &s->mbox, &s->ibox, &s->wbox, &s->sig1, &s->sig2, &s->sig1t, &s->sig2t, &s->phys, &s->dir };
	unsigned k;

	for (k = 0; k < sizeof(f) / sizeof(f[0]); k++)
		if (*f[k] >= 0)
			close(*f[k]);	/* the directory fd last: that removes the context */
	free(s);
	c->priv = NULL;
}

const struct spt_be spt_be_spufs = {
	.name = "spufs",
	.probe = probe,
	.spe_ids = spe_ids,
	.create = create,
	.run = run,
	.physid = physid,
	.mbox_put = mbox_put,
	.mbox_get = mbox_get,
	.ibox_get = ibox_get,
	.signal_put = signal_put,
	.signal_type = signal_type,
	.interrupt = interrupt,
	.destroy = destroy,
};
