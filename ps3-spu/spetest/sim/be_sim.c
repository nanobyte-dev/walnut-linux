/* The simulator backend: spetest's runner against spusim.c instead of spufs, so the runner and
 * the SPU programs run on any host. It stands in for the kernel too: contexts get the next free
 * SPE when they start, hold it until they stop, and "phys-id" says which.
 * Environment: SPT_SIM_SPES (SPEs, default 6), SPT_SIM_BUSY (SPEs held by someone else),
 * SPT_SIM_FAULTS = "spe:kind[=n];..." with kind ls=ADDR/OR, hang, fa, dma=N, dec=N, reg=N. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "../spt.h"
#include "spusim.h"

#define MAXS 16
static pthread_mutex_t kmu = PTHREAD_MUTEX_INITIALIZER;
static int taken[MAXS], last_alloc = -1;

struct sc {
	struct sim_spe *sim;
	int phys;
};

static unsigned nspes(void)
{
	const char *e = getenv("SPT_SIM_SPES");
	int n = e ? atoi(e) : 6;

	return n < 0 ? 0 : n > MAXS ? MAXS : n;
}

static int probe(char *mnt, unsigned len)
{
	const char *e = getenv("SPT_SIM_BUSY");
	int i, busy = e ? atoi(e) : 0;

	snprintf(mnt, len, "simulated spufs");
	pthread_mutex_lock(&kmu);
	for (i = 0; i < busy && i < MAXS; i++)
		taken[(int)nspes() - 1 - i] = 1;	/* someone else's contexts */
	pthread_mutex_unlock(&kmu);
	return (int)nspes();
}

static int spe_ids(unsigned *ids, unsigned max)
{
	unsigned i, n = nspes();

	for (i = 0; i < n && i < max; i++)
		ids[i] = i;
	return (int)(n < max ? n : max);
}

static void fault_for(int phys, struct sim_fault *f)
{
	const char *e = getenv("SPT_SIM_FAULTS");
	char *dup, *tok, *sv = NULL;

	memset(f, 0, sizeof(*f));
	f->ls_addr = f->reg_stuck = -1;
	if (!e)
		return;
	dup = strdup(e);
	for (tok = strtok_r(dup, ";", &sv); tok; tok = strtok_r(NULL, ";", &sv)) {
		int spe;
		char kind[32] = "";
		unsigned long a = 0, b = 0;

		if (sscanf(tok, "%d:%31[a-z]=%lx/%lx", &spe, kind, &a, &b) < 2 || spe != phys)
			continue;
		if (!strcmp(kind, "ls")) { f->ls_addr = (int)a; f->ls_or = b ? (uint8_t)b : 0x04; }
		else if (!strcmp(kind, "hang")) f->hang = 1;
		else if (!strcmp(kind, "fa")) f->fp_bit = 1;
		else if (!strcmp(kind, "dma")) f->dma_flip = a ? (int)a : 7;
		else if (!strcmp(kind, "dec")) f->dec_div = a ? (int)a : 2;
		else if (!strcmp(kind, "reg")) f->reg_stuck = (int)a;
	}
	free(dup);
}

static int create(struct spt_ctx *c, const char *elf, uint64_t r3, uint64_t r4, uint64_t r5)
{
	struct sc *s = calloc(1, sizeof(*s));
	struct stat st;
	uint8_t *img;
	int fd;

	if (!s)
		return -ENOMEM;
	fd = open(elf, O_RDONLY);
	if (fd < 0 || fstat(fd, &st)) {
		free(s);
		return -errno;
	}
	img = malloc(st.st_size);
	if (!img || read(fd, img, st.st_size) != st.st_size) {
		close(fd);
		free(s);
		free(img);
		return -EIO;
	}
	close(fd);
	s->sim = sim_new(img, st.st_size, r3, r4, r5, NULL, 0);
	free(img);
	if (!s->sim) {
		free(s);
		return -ENOEXEC;
	}
	s->phys = -1;
	c->priv = s;
	return 0;
}

static long run(struct spt_ctx *c, unsigned *event)
{
	struct sc *s = c->priv;
	struct sim_fault f;
	long ret;
	int p = -1, n = (int)nspes(), tries;

	/* the "kernel": the next free SPE after the last one used, or wait for one */
	for (tries = 0; tries < 6000 && p < 0; tries++) {
		int k;

		pthread_mutex_lock(&kmu);
		for (k = 1; k <= n; k++) {
			int q = (last_alloc + k) % n;

			if (!taken[q]) {
				taken[q] = 1;
				last_alloc = q;
				p = q;
				break;
			}
		}
		pthread_mutex_unlock(&kmu);
		if (p < 0)
			usleep(1000);
	}
	if (p < 0)
		return -EBUSY;
	s->phys = p;
	fault_for(p, &f);
	sim_set_fault(s->sim, &f);
	ret = sim_run(s->sim, event);
	s->phys = -1;
	pthread_mutex_lock(&kmu);
	taken[p] = 0;
	pthread_mutex_unlock(&kmu);
	return ret;
}

static int physid(struct spt_ctx *c)
{
	struct sc *s = c->priv;

	return s->phys;
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
	long end = now_ms() + timeout_ms;

	while (sim_mbox_put(s->sim, w)) {
		if (c->done || now_ms() >= end)
			return -ETIMEDOUT;
		usleep(100);
	}
	return 0;
}

static int box_get(struct spt_ctx *c, uint32_t *w, int timeout_ms, int intr)
{
	struct sc *s = c->priv;
	long end = now_ms() + timeout_ms;

	for (;;) {
		if (!(intr ? sim_ibox_get(s->sim, w) : sim_mbox_get(s->sim, w)))
			return 0;
		if (now_ms() >= end)
			return -ETIMEDOUT;
		if (c->done)
			return -ECANCELED;
		usleep(50);
	}
}

static int mbox_get(struct spt_ctx *c, uint32_t *w, int t) { return box_get(c, w, t, 0); }
static int ibox_get(struct spt_ctx *c, uint32_t *w, int t) { return box_get(c, w, t, 1); }

static int signal_put(struct spt_ctx *c, int which, uint32_t v)
{
	sim_signal(((struct sc *)c->priv)->sim, which, v);
	return 0;
}

static int signal_type(struct spt_ctx *c, int which, int or_mode)
{
	sim_signal_type(((struct sc *)c->priv)->sim, which, or_mode);
	return 0;
}

static void interrupt(struct spt_ctx *c) { sim_interrupt(((struct sc *)c->priv)->sim); }

static void destroy(struct spt_ctx *c)
{
	struct sc *s = c->priv;

	sim_free(s->sim);
	free(s);
	c->priv = NULL;
}

const struct spt_be spt_be_sim = {
	.name = "sim",
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
