/* The spufs backend: SPU contexts without libspe2. One context per worker:
 * spu_create(SPU_CREATE_EVENTS_ENABLED), the program's segments copied into the local store
 * through the mem mmap, r3/r4/r6 written through the regs file, then spu_run from the entry
 * point. spurun (src/spurun.c) is the minimal version of the same. */
#define _GNU_SOURCE
#include <dirent.h>
#include <elf.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "speq-int.h"

#define LS_SIZE (256 * 1024)
#ifndef EM_SPU
#define EM_SPU 23
#endif

/* Not powerpc (the host tests): no spufs; the fake backend stands in */
#ifdef SYS_spu_create
static long spu_create(const char *path, unsigned flags, mode_t mode)
{
	return syscall(SYS_spu_create, path, flags, mode);
}
static long spu_run(int fd, unsigned *npc, unsigned *event)
{
	return syscall(SYS_spu_run, fd, npc, event);
}
#else
static long spu_create(const char *path, unsigned flags, mode_t mode)
{
	errno = ENOSYS;
	return -1;
}
static long spu_run(int fd, unsigned *npc, unsigned *event)
{
	errno = ENOSYS;
	return -1;
}
#endif

/* Where spufs is mounted (/proc/mounts), and how many SPEs Linux has
 * (/sys/devices/system/spu/spuN: 6 on the PS3) */
int spq_spufs_probe(char *mnt, size_t len, unsigned *nspe)
{
	char dev[64], dir[256], type[32];
	struct dirent *d;
	FILE *f = fopen("/proc/mounts", "r");
	DIR *s;
	int found = 0;
	unsigned n = 0, k;

	if (!f)
		return -ENOENT;
	while (!found && fscanf(f, "%63s %255s %31s %*[^\n]", dev, dir, type) == 3)
		if (!strcmp(type, "spufs") && strlen(dir) < len) {
			strcpy(mnt, dir);
			found = 1;
		}
	fclose(f);
	if (!found)
		return -ENOENT;
	s = opendir("/sys/devices/system/spu");
	if (!s)
		return -ENOENT;
	while ((d = readdir(s)))
		if (sscanf(d->d_name, "spu%u", &k) == 1)
			n++;
	closedir(s);
	*nspe = n;
	return n ? 0 : -EBUSY;
}

/* An SPU ELF that fits the local store and carries our ABI word (section .speq_abi, put
 * there by libspeq-spu.a). Big-endian whatever the host, so the host tests can check
 * bench.spu too. */
int spq_elf_check(const uint8_t *img, size_t size, uint32_t *entry)
{
	const Elf32_Ehdr *eh = (const Elf32_Ehdr *)img;
	const Elf32_Phdr *ph;
	const Elf32_Shdr *sh;
	const char *names;
	uint32_t phoff, shoff, i, n, abi = 0, found = 0;

	if (size < sizeof(*eh) || memcmp(eh->e_ident, ELFMAG, SELFMAG) ||
	    eh->e_ident[EI_CLASS] != ELFCLASS32 || eh->e_ident[EI_DATA] != ELFDATA2MSB ||
	    be16toh(eh->e_machine) != EM_SPU)
		return -EPROTO;
	phoff = be32toh(eh->e_phoff);
	n = be16toh(eh->e_phnum);
	if (phoff > size || n > (size - phoff) / sizeof(*ph))
		return -EPROTO;
	ph = (const Elf32_Phdr *)(img + phoff);
	for (i = 0; i < n; i++) {
		uint32_t off = be32toh(ph[i].p_offset), filesz = be32toh(ph[i].p_filesz);
		uint32_t vaddr = be32toh(ph[i].p_vaddr), memsz = be32toh(ph[i].p_memsz);

		if (be32toh(ph[i].p_type) != PT_LOAD)
			continue;
		if (vaddr > LS_SIZE || memsz > LS_SIZE - vaddr || filesz > memsz ||
		    off > size || filesz > size - off)
			return -EPROTO;
	}
	shoff = be32toh(eh->e_shoff);
	n = be16toh(eh->e_shnum);
	i = be16toh(eh->e_shstrndx);
	if (shoff > size || n > (size - shoff) / sizeof(*sh) || i >= n)
		return -EPROTO;
	sh = (const Elf32_Shdr *)(img + shoff);
	if (be32toh(sh[i].sh_offset) >= size)
		return -EPROTO;
	names = (const char *)img + be32toh(sh[i].sh_offset);
	for (i = 0; i < n; i++) {
		uint32_t name = be32toh(sh[i].sh_name), off = be32toh(sh[i].sh_offset);

		if (names + name + 10 > (const char *)img + size || strcmp(names + name, ".speq_abi"))
			continue;
		if (be32toh(sh[i].sh_size) < 4 || off > size - 4)	/* padded to 16 */
			return -EPROTO;
		memcpy(&abi, img + off, 4);
		abi = be32toh(abi);
		found = 1;
	}
	if (!found || abi != SPEQ_ABI_VERSION)
		return -EPROTO;
	if (entry)
		*entry = be32toh(eh->e_entry);
	return 0;
}

/* prog: a path, or a name in /usr/lib/ps3/spu (SPEQ_SPU_DIR overrides it, for tests) */
int spq_load_program(struct speq *q, const char *prog)
{
	const char *dir = getenv("SPEQ_SPU_DIR");
	char path[512];
	struct stat st;
	uint8_t *img;
	int fd, ret;

	if (strchr(prog, '/'))
		snprintf(path, sizeof(path), "%s", prog);
	else
		snprintf(path, sizeof(path), "%s/%s", dir && *dir ? dir : "/usr/lib/ps3/spu", prog);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -ENOENT;
	if (fstat(fd, &st) || st.st_size <= 0 || st.st_size > 4 * LS_SIZE) {
		close(fd);
		return -EPROTO;
	}
	img = malloc(st.st_size);
	if (!img) {
		close(fd);
		return -ENOMEM;
	}
	ret = read(fd, img, st.st_size) == st.st_size ? 0 : -EIO;
	close(fd);
	if (!ret)
		ret = spq_elf_check(img, st.st_size, NULL);
	if (ret) {
		free(img);
		return ret;
	}
	q->img = img;
	q->img_size = st.st_size;
	return 0;
}

/* A register's preferred slot: bytes 0-7 of quadword n of the regs file, big-endian */
static int set_reg(int fd, unsigned n, uint64_t v)
{
	uint8_t qw[16] = { 0 };

	v = htobe64(v);
	memcpy(qw, &v, 8);
	return pwrite(fd, qw, 16, n * 16) == 16 ? 0 : -errno;
}

static int spufs_create(struct spq_worker *w)
{
	struct speq *q = w->q;
	const Elf32_Ehdr *eh = (const Elf32_Ehdr *)q->img;
	const Elf32_Phdr *ph = (const Elf32_Phdr *)(q->img + be32toh(eh->e_phoff));
	uint8_t *ls;
	uint32_t entry;
	int ctx, fd, i, ret;

	snprintf(w->path, sizeof(w->path), "%s/speq-%d-%u-%u-%u", q->spufs, getpid(), q->id,
		 w->idx, w->gen++);
	ctx = spu_create(w->path, SPU_CREATE_EVENTS_ENABLED, 0700);
	if (ctx < 0)
		return -errno;
	/* an exec'd child must not keep our contexts (and SPEs) alive */
	fcntl(ctx, F_SETFD, FD_CLOEXEC);
	fd = openat(ctx, "mem", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		ret = -errno;
		close(ctx);
		return ret;
	}
	ls = mmap(NULL, LS_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (ls == MAP_FAILED) {
		ret = -errno;
		close(ctx);
		return ret;
	}
	spq_elf_check(q->img, q->img_size, &entry);	/* checked at open */
	for (i = 0; i < be16toh(eh->e_phnum); i++) {
		uint32_t off = be32toh(ph[i].p_offset), filesz = be32toh(ph[i].p_filesz);
		uint32_t vaddr = be32toh(ph[i].p_vaddr), memsz = be32toh(ph[i].p_memsz);

		if (be32toh(ph[i].p_type) != PT_LOAD)
			continue;
		memcpy(ls + vaddr, q->img + off, filesz);
		memset(ls + vaddr + filesz, 0, memsz - filesz);
	}
	/* crt0: main(r3, r4, r5), r6 into __ea_local_store, r2 = 0: the default stack */
	fd = openat(ctx, "regs", O_RDWR | O_CLOEXEC);
	ret = fd < 0 ? -errno : 0;
	if (!ret)
		ret = set_reg(fd, 3, w->idx);
	if (!ret)
		ret = set_reg(fd, 4, (uintptr_t)q->ctl);
	if (!ret)
		ret = set_reg(fd, 6, (uintptr_t)ls);
	if (fd >= 0)
		close(fd);
	if (ret) {
		munmap(ls, LS_SIZE);
		close(ctx);
		return ret;
	}
	w->ctx = ctx;
	w->ls = ls;
	w->npc = entry;
	return 0;
}

/* spu_run returns the status word; a fault shows only in *event (a nonzero one is fatal:
 * the MFC stays suspended). Signals: the worker blocks them, but SIGSTOP/SIGCONT can still
 * interrupt it (-EINTR), and the caller simply reruns. */
static long spufs_run(struct spq_worker *w, unsigned *npc, unsigned *event)
{
	long ret;

	*event = 0;
	ret = spu_run(w->ctx, npc, event);
	return ret < 0 ? -errno : ret;
}

static void spufs_destroy(struct spq_worker *w)
{
	munmap(w->ls, LS_SIZE);
	close(w->ctx);	/* the context directory goes with its last fd */
	w->ls = NULL;
	w->ctx = -1;
}

const struct spq_backend spq_spufs = {
	.name = "spufs",
	.create = spufs_create,
	.run = spufs_run,
	.destroy = spufs_destroy,
};
