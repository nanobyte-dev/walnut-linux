/* SPEQ_FAKE builds (the host tests, speq-bench-fake): the SPU program (spu/runtime.c and its
 * ops, built for the host) runs on a thread per "context", with the MFC emulated, so the
 * queue's logic (claims, parking, completion, faults, teardown) runs with real threads on any
 * machine. Selected at speq_open by SPEQ_FAKE=1.
 *
 * What is emulated, and how far:
 * - spu_run: the SPU thread runs until a stop (park, exit) or a fault; run() waits for it,
 *   a later run() resumes it.
 * - getllar/putllc: the line is copied with a snapshot; putllc is an 8-byte CAS of the
 *   line's first 8 bytes against the snapshot, and stores nothing else. Enough for libspeq,
 *   which keeps every atomically changed word there (speq-abi.h).
 * - the event wait polls every 50 us: LLR_LOST when the reserved line's contents differ from
 *   the snapshot, DECREMENTER when the emulated decrementer goes negative. A resume raises
 *   LLR_LOST, as a context restore does.
 * - DMA: memcpy, with the MFC's size and alignment rules checked (DMA_ALIGNMENT event);
 *   an EA below 64 KB is a DATA_STORAGE fault (the context dies, as on hardware).
 * - stop 0x2104: getpid and futex, mapped to the host's syscall numbers.
 * - programs: every program linked into the test registers its operation table under its file
 *   name (SPEQ_PROGRAM, spu/speq-spu.h); a context runs the one its queue opened (the only one,
 *   when just one is linked in), so DXT's queue and the deadline pool run side by side as on
 *   the PS3. A name nothing registered is -ENOENT, as a missing file. */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "speq-int.h"
#include "speq-spu.h"

#define MAX_PROGS	8

static struct {
	const char *name;
	const speq_op_fn *ops;
	unsigned n;
} progs[MAX_PROGS];
static unsigned nprogs;

__thread const speq_op_fn *spq_fake_ops;
__thread unsigned spq_fake_nops;

/* constructors, before main: no lock needed */
void spq_fake_register(const char *name, const speq_op_fn *ops, unsigned n)
{
	if (nprogs < MAX_PROGS) {
		progs[nprogs].name = name;
		progs[nprogs].ops = ops;
		progs[nprogs].n = n;
		nprogs++;
	}
}

static int find_prog(const char *prog, unsigned *k)
{
	const char *base = strrchr(prog, '/');
	unsigned i;

	base = base ? base + 1 : prog;
	for (i = 0; i < nprogs; i++)
		if (!strcmp(progs[i].name, base)) {
			*k = i;
			return 0;
		}
	if (nprogs == 1) {	/* a tool given a path to its program */
		*k = 0;
		return 0;
	}
	return -ENOENT;
}

struct fake {
	pthread_t th;
	pthread_mutex_t m;
	pthread_cond_t cv;
	int started, running, exited, kill;
	unsigned status, event;
	uint64_t r3, r4;
	/* the "SPU" */
	uint64_t res_ea;	/* reservation, 0: none */
	uint32_t res_line[32];
	double dec_t0;
	uint32_t dec_v0;
	int dec_armed;
	uint32_t mask, pending;
	const speq_op_fn *ops;
	unsigned nops;
};

static __thread struct fake *cur;

/* The SPU stops: hand status and event to run(); then wait to be resumed, unless final */
static void fake_stop(struct fake *f, unsigned status, unsigned event, int final)
{
	int kill;

	pthread_mutex_lock(&f->m);
	f->status = status;
	f->event = event;
	f->running = 0;
	f->exited = final;
	pthread_cond_broadcast(&f->cv);
	while (!final && !f->running && !f->kill)
		pthread_cond_wait(&f->cv, &f->m);
	kill = f->kill;
	pthread_mutex_unlock(&f->m);
	if (final || kill)
		pthread_exit(NULL);
	f->pending |= SPEQ_LLR_LOST_EVENT;	/* as a context restore does */
	f->res_ea = 0;
}

static void fault(unsigned event)
{
	fake_stop(cur, 0, event, 1);
}

static void *spu_thread(void *arg)
{
	struct fake *f = arg;
	int ret;

	cur = f;
	spq_fake_ops = f->ops;
	spq_fake_nops = f->nops;
	ret = speq_fake_main(f->r3, f->r4, 0);
	fake_stop(f, (SPEQ_STOP_EXIT | (ret & 0xff)) << 16 | SPU_STATUS_STOPPED_BY_STOP, 0, 1);
	return NULL;
}

static int fake_create(struct spq_worker *w)
{
	struct fake *f;
	unsigned k;

	if (find_prog(w->q->prog, &k))
		return -ENOENT;
	f = calloc(1, sizeof(*f));
	if (!f)
		return -ENOMEM;
	f->ops = progs[k].ops;
	f->nops = progs[k].n;
	pthread_mutex_init(&f->m, NULL);
	pthread_cond_init(&f->cv, NULL);
	f->r3 = w->idx;
	f->r4 = (uintptr_t)w->q->ctl;
	w->fake = f;
	w->npc = 0;
	snprintf(w->path, sizeof(w->path), "fake-%u-%u", w->idx, w->gen++);
	return 0;
}

static long fake_run(struct spq_worker *w, unsigned *npc, unsigned *event)
{
	struct fake *f = w->fake;
	long st;

	pthread_mutex_lock(&f->m);
	if (f->exited) {
		pthread_mutex_unlock(&f->m);
		return -EINVAL;
	}
	f->running = 1;
	if (!f->started) {
		f->started = 1;
		if (pthread_create(&f->th, NULL, spu_thread, f)) {
			f->started = 0;
			f->running = 0;
			pthread_mutex_unlock(&f->m);
			return -EAGAIN;
		}
	} else {
		pthread_cond_broadcast(&f->cv);
	}
	while (f->running)
		pthread_cond_wait(&f->cv, &f->m);
	*event = f->event;
	st = f->status;
	pthread_mutex_unlock(&f->m);
	return st;
}

static void fake_destroy(struct spq_worker *w)
{
	struct fake *f = w->fake;

	pthread_mutex_lock(&f->m);
	f->kill = 1;
	pthread_cond_broadcast(&f->cv);
	pthread_mutex_unlock(&f->m);
	if (f->started)
		pthread_join(f->th, NULL);
	pthread_cond_destroy(&f->cv);
	pthread_mutex_destroy(&f->m);
	free(f);
	w->fake = NULL;
}

const struct spq_backend spq_fake = {
	.name = "fake",
	.create = fake_create,
	.run = fake_run,
	.destroy = fake_destroy,
};

/* --- the SPU primitives (spu/speq-spu.h) --- */

static void dma_check(volatile void *ls, uint64_t ea, uint32_t size)
{
	uintptr_t l = (uintptr_t)ls;
	int ok;

	if (size == 1 || size == 2 || size == 4 || size == 8)
		ok = !(ea & (size - 1)) && (l & 15) == (ea & 15);
	else
		ok = size && !(size & 15) && size <= 16384 && !(l & 15) && !(ea & 15);
	if (!ok)
		fault(SPE_EVENT_DMA_ALIGNMENT);
	if (ea < 65536)
		fault(SPE_EVENT_SPE_DATA_STORAGE);
}

void speq_get(volatile void *ls, uint64_t ea, uint32_t size, unsigned tag)
{
	dma_check(ls, ea, size);
	memcpy((void *)ls, (void *)(uintptr_t)ea, size);
}

void speq_put(volatile void *ls, uint64_t ea, uint32_t size, unsigned tag)
{
	dma_check(ls, ea, size);
	memcpy((void *)(uintptr_t)ea, (void *)ls, size);
}

void speq_wait_tags(uint32_t mask)
{
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}

void speq_getllar(volatile void *ls, uint64_t ea)
{
	uint32_t *src = (uint32_t *)(uintptr_t)ea, *dst = (uint32_t *)ls;
	uint64_t first;
	int i;

	if (ea & 127)
		fault(SPE_EVENT_DMA_ALIGNMENT);
	if (ea < 65536)
		fault(SPE_EVENT_SPE_DATA_STORAGE);
	first = __atomic_load_n((uint64_t *)src, __ATOMIC_SEQ_CST);
	memcpy(cur->res_line, &first, 8);
	for (i = 2; i < 32; i++)
		cur->res_line[i] = __atomic_load_n(&src[i], __ATOMIC_RELAXED);
	memcpy(dst, cur->res_line, 128);
	cur->res_ea = ea;
}

int speq_putllc(volatile void *ls, uint64_t ea)
{
	uint64_t old, new;

	if (cur->res_ea != ea)
		return 1;
	cur->res_ea = 0;
	memcpy(&old, cur->res_line, 8);
	memcpy(&new, (void *)ls, 8);
	return !__atomic_compare_exchange_n((uint64_t *)(uintptr_t)ea, &old, new, 0,
					    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

uint32_t speq_dec_read(void)
{
	double ticks = (spq_now_us() - cur->dec_t0) * spq_tb_hz() / 1e6;

	return cur->dec_v0 - (uint32_t)(uint64_t)ticks;
}

void speq_dec_write(uint32_t v)
{
	cur->dec_t0 = spq_now_us();
	cur->dec_v0 = v;
	cur->dec_armed = (int32_t)v >= 0;
}

void speq_event_mask(uint32_t m)
{
	cur->mask = m;
}

static uint32_t poll_events(void)
{
	struct fake *f = cur;
	uint32_t *line = (uint32_t *)(uintptr_t)f->res_ea;
	int i;

	if (line)
		for (i = 0; i < 32; i++)
			if (__atomic_load_n(&line[i], __ATOMIC_ACQUIRE) != f->res_line[i]) {
				f->pending |= SPEQ_LLR_LOST_EVENT;
				f->res_ea = 0;
				break;
			}
	if (f->dec_armed && (int32_t)speq_dec_read() < 0) {
		f->pending |= SPEQ_DECREMENTER_EVENT;
		f->dec_armed = 0;
	}
	return f->pending & f->mask;
}

uint32_t speq_event_wait(void)
{
	struct timespec ts = { 0, 50000 };
	uint32_t ev;

	while (!(ev = poll_events()))
		nanosleep(&ts, NULL);
	cur->pending &= ~ev;
	return ev;
}

void speq_event_drain(void)
{
	cur->pending &= ~poll_events();
}

long long speq_syscall(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
		       uint64_t a4, uint64_t a5)
{
	long r;

	/* the SPU passes powerpc numbers */
	if (nr == SPEQ_NR_getpid)
		r = syscall(SYS_getpid);
	else if (nr == SPEQ_NR_futex)
		r = syscall(SYS_futex, (void *)(uintptr_t)a0, (int)a1, (int)a2, NULL, NULL, 0);
	else
		return -ENOSYS;
	return r < 0 ? -errno : r;
}

void speq_stop_park(void)
{
	fake_stop(cur, SPEQ_STOP_PARK << 16 | SPU_STATUS_STOPPED_BY_STOP, 0, 0);
}

void speq_ibox_write(uint32_t v)
{
}
