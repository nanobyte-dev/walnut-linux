/* The SPE work queue, PPE side: the control block, submit and wait, and one thread per SPU
 * worker that sits in spu_run and handles its stops (park, exit, faults).
 *
 * Barriers. The PPE side uses GCC __atomic builtins; on ppc64 a seq_cst read-modify-write is
 * "sync; lwarx; ... stwcx.; bne; isync" and an acquire fence is lwsync. Each use says what it
 * orders. The SPU's getllar/putllc and the PPE's lwarx/stwcx. both reserve the whole 128-byte
 * line, so a word changed atomically by either side is safe against the other's.
 *
 * Done words: pending (low 31 bits) | SPEQ_WAITERS, error next to it in the same 8 bytes.
 * The SPU decrements with getllar/putllc (spu/runtime.c complete()); the last one zeroes the
 * word and futex-wakes if a waiter set the bit. The PPE fails jobs the same way
 * (spq_fail_job: an 8-byte CAS on pending|error). */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "speq-int.h"

#define SPIN_US		80	/* speq_wait spins this long before sleeping: a futex sleep costs ~62 us more than
			 * spinning on the PS3 (speq-bench P2, 2026-10-16) */
#define IDLE_US		10000	/* an SPU parks after this long without work (P3) */
#define FREE_MS		5000	/* a parked worker frees its context after this (~260 KB vmalloc) */
#define STRIKES		3	/* contexts dying in a row without a job: the worker gives up */
#define CLOSE_MS	3000	/* speq_close waits this long for each worker */

static pthread_mutex_t queues_lock = PTHREAD_MUTEX_INITIALIZER;
static struct speq *queues;
static unsigned next_id;
static pthread_once_t fork_once = PTHREAD_ONCE_INIT;

#define LOAD(p)		__atomic_load_n((p), __ATOMIC_ACQUIRE)
#define STORE(p, v)	__atomic_store_n((p), (v), __ATOMIC_SEQ_CST)
#define CAS(p, o, n)	__atomic_compare_exchange_n((p), (o), (n), 0, __ATOMIC_SEQ_CST, \
						    __ATOMIC_ACQUIRE)
#define INC(p)		__atomic_fetch_add((p), 1, __ATOMIC_RELAXED)

static inline void cpu_relax(void)
{
#ifdef __powerpc__
	/* low SMT priority while spinning, so the other PPE thread runs; then medium again */
	__asm__ volatile ("or 1,1,1\n\tor 2,2,2" ::: "memory");
#endif
}

double spq_now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static long futex(void *addr, int op, uint32_t val, const struct timespec *ts)
{
	return syscall(SYS_futex, addr, op, val, ts, NULL, 0);
}

static void futex_wake_all(void *addr)
{
	futex(addr, FUTEX_WAKE_PRIVATE, INT_MAX, NULL);
}

static void us_timespec(double us, struct timespec *ts)
{
	ts->tv_sec = us / 1e6;
	ts->tv_nsec = (us - ts->tv_sec * 1e6) * 1e3;
}

/* The decrementer runs at the timebase: 79.8 MHz on the PS3 */
uint32_t spq_tb_hz(void)
{
	static uint32_t hz;
	char line[128];
	unsigned long v;
	FILE *f;

	if (hz)
		return hz;
	v = 79800000;
	f = fopen("/proc/cpuinfo", "r");
	if (f) {
		while (fgets(line, sizeof(line), f))
			if (sscanf(line, "timebase : %lu", &v) == 1 && v && v < 0xffffffffu)
				break;
		fclose(f);
	}
	hz = v;
	return hz;
}

static uint32_t idle_ticks(unsigned us)
{
	uint64_t t = (uint64_t)us * spq_tb_hz() / 1000000;

	return t > 0x7fffffff ? 0x7fffffff : t;
}

static unsigned env_uint(const char *name, unsigned def)
{
	const char *s = getenv(name);

	return s && *s ? (unsigned)strtoul(s, NULL, 0) : def;
}

static struct speq_job *ring(struct speq *q, uint32_t seq)
{
	return (struct speq_job *)((char *)q->ctl + SPEQ_RING_OFF + (seq % SPEQ_JOBS) * SPEQ_LINE);
}

static struct speq_wline *wline(struct speq *q, unsigned i)
{
	return (struct speq_wline *)((char *)q->ctl + SPEQ_WLINE_OFF(i));
}

/* pending and error as one 8-byte word: both change in one CAS, as in the SPU's putllc */
union done_pair {
	uint64_t v;
	struct {
		uint32_t pending;
		int32_t error;
	} s;
};

void spq_fail_job(struct speq_done *done, int err)
{
	uint64_t *p = (uint64_t *)done;
	union done_pair old, new;
	uint32_t left;

	old.v = LOAD(p);
	do {
		new = old;
		left = old.s.pending & ~SPEQ_WAITERS;
		if (!left)
			return;
		new.s.pending = left == 1 ? 0 : old.s.pending - 1;
		if (!new.s.error)
			new.s.error = err;
	} while (!CAS(p, &old.v, new.v));
	if (left == 1 && (old.s.pending & SPEQ_WAITERS))
		futex_wake_all(&done->pending);
}

/* Claims and fails every queued job, when no worker is left to run them (close, all broken).
 * A PPE stwcx. on head is safe against an SPU claiming at the same time: same line. */
static void drain_ring(struct speq *q, int err)
{
	struct speq_ctl *c = q->ctl;
	uint32_t head, tail;
	uint64_t done;

	for (;;) {
		head = LOAD(&c->head);
		tail = LOAD(&c->tail_parked) >> SPEQ_TAIL_SHIFT;
		if (head == tail)
			return;
		done = ring(q, head)->done;	/* before the claim, as the SPU does */
		if (CAS(&c->head, &head, (head + 1) & SPEQ_SEQ_MASK))
			spq_fail_job((struct speq_done *)(uintptr_t)done, err);
	}
}

/* A context is going away: keep its counters, clear its line (no stale held job) */
static void retire(struct spq_worker *w)
{
	struct speq_wline *l = wline(w->q, w->idx);
	const uint32_t *c = &l->claims;
	int i;

	for (i = 0; i < 6; i++)
		w->acc[i] += c[i];
	memset(l, 0, sizeof(*l));
	w->q->be->destroy(w);
	w->live = 0;
}

/* The context died: a fault event (the MFC stays suspended, it can't be rerun), an exit we
 * didn't ask for, or a stop we don't serve. Fail the job it held, if any, and close it. */
static void died(struct spq_worker *w, unsigned event, long status)
{
	struct speq *q = w->q;
	struct speq_wline *l = wline(q, w->idx);
	int err = event & (SPE_EVENT_SPE_DATA_STORAGE | SPE_EVENT_SPE_DATA_SEGMENT) ? -EFAULT : -EIO;

	INC(&q->faults);
	if (getenv("SPEQ_DEBUG"))
		fprintf(stderr, "speq: worker %u died: event 0x%x status 0x%lx held %u\n", w->idx,
			event, status, LOAD(&l->held) ? LOAD(&l->seq) : 0);
	/* held was put (and waited for) before the job ran, so it is current */
	if (LOAD(&l->held)) {
		INC(&q->failed);	/* before the waiter wakes */
		spq_fail_job((struct speq_done *)(uintptr_t)l->done, err);
		w->strikes = 0;
	} else if (!l->claims) {
		w->strikes++;
	}
	retire(w);
}

static void broke(struct spq_worker *w)
{
	struct speq *q = w->q;

	w->broken = 1;
	if (!__atomic_sub_fetch(&q->alive, 1, __ATOMIC_SEQ_CST)) {
		/* nobody left to run the queue: fail what is in it and what comes */
		STORE(&q->dead, 1);
		drain_ring(q, -EIO);
	}
}

/* Sleeps with the worker's parked bit set until a submit clears it (or close). Live (the SPU
 * stopped at 0x2110): returns to resume it; after free_ms it frees the context and sleeps on.
 * No context (freed, or died): returns to respawn only when there is work, else sets the bit
 * again. futex_wait on the value it saw: a submit in between changed it (EAGAIN), so no wake
 * is lost. Returns 1 to exit. */
static int park_wait(struct spq_worker *w)
{
	struct speq *q = w->q;
	uint32_t *tp = &q->ctl->tail_parked, bit = 1u << w->idx, v;
	double deadline = spq_now_us() + q->free_ms * 1000.0, rem;
	struct timespec ts;

	for (;;) {
		if (LOAD(&q->quit))
			return 1;
		v = LOAD(tp);
		if (!(v & bit)) {
			if (w->live || v >> SPEQ_TAIL_SHIFT != LOAD(&q->ctl->head))
				return 0;
			if (!CAS(tp, &v, v | bit))
				continue;
			v |= bit;
		}
		if (w->live) {
			rem = deadline - spq_now_us();
			if (rem <= 0) {
				retire(w);
				INC(&q->frees);
				continue;
			}
			us_timespec(rem, &ts);
			futex(tp, FUTEX_WAIT_PRIVATE, v, &ts);
		} else {
			futex(tp, FUTEX_WAIT_PRIVATE, v, NULL);
		}
	}
}

static void *worker_main(void *arg)
{
	struct spq_worker *w = arg;
	struct speq *q = w->q;
	unsigned event, code;
	long st;
	int ret;

	for (;;) {
		if (!w->live) {
			if (LOAD(&q->quit))
				break;
			ret = q->be->create(w);
			if (ret) {
				if (++w->strikes >= STRIKES) {
					broke(w);
					break;
				}
				usleep(10000);
				continue;
			}
			w->live = 1;
			INC(&q->spawns);
		}
		event = 0;
		st = q->be->run(w, &w->npc, &event);
		code = st >= 0 ? SPU_STOP_CODE((unsigned long)st) : 0;
		if (!event && (st == -EINTR || st == -EAGAIN))
			continue;
		if (!event && st >= 0 && (st & SPU_STATUS_STOPPED_BY_STOP)) {
			if (code == SPEQ_STOP_PARK) {
				INC(&q->parks);
				if (park_wait(w))
					break;
				if (w->live)
					INC(&q->unparks);
				continue;
			}
			if (code == SPEQ_STOP_EXIT && LOAD(&q->quit)) {
				INC(&q->exits);
				retire(w);
				break;
			}
			if (code == (SPEQ_STOP_EXIT | SPEQ_EXIT_ABI)) {
				retire(w);
				broke(w);
				break;
			}
		}
		died(w, event, st);
		if (w->strikes >= STRIKES) {
			broke(w);
			break;
		}
		if (park_wait(w))	/* respawn lazily: when there is work */
			break;
	}
	if (w->live)
		retire(w);
	return NULL;
}

static void fork_prepare(void)
{
	pthread_mutex_lock(&queues_lock);
}

static void fork_parent(void)
{
	pthread_mutex_unlock(&queues_lock);
}

/* The child has none of the worker threads: every call fails (-EIO) and speq_close only
 * frees memory */
static void fork_child(void)
{
	struct speq *q;

	for (q = queues; q; q = q->next) {
		q->forked = 1;
		q->dead = 1;
	}
	pthread_mutex_unlock(&queues_lock);
}

static void fork_init(void)
{
	pthread_atfork(fork_prepare, fork_parent, fork_child);
}

static int use_fake(void)
{
#ifdef SPEQ_FAKE
	const char *s = getenv("SPEQ_FAKE");

	return s && *s && strcmp(s, "0");
#else
	return 0;
#endif
}

static void free_queue(struct speq *q)
{
	if (q->ctl) {
		munlock(q->ctl, SPEQ_CB_SIZE);
		free(q->ctl);
	}
	free(q->img);
	pthread_mutex_destroy(&q->lock);
	free(q);
}

static void unlist(struct speq *q)
{
	struct speq **p;

	pthread_mutex_lock(&queues_lock);
	for (p = &queues; *p; p = &(*p)->next)
		if (*p == q) {
			*p = q->next;
			break;
		}
	pthread_mutex_unlock(&queues_lock);
}

int speq_open(struct speq **qp, const char *prog, unsigned nworkers, unsigned flags)
{
	struct speq *q;
	sigset_t all, old;
	pthread_attr_t attr;
	unsigned nspe = 0, i;
	void *cb;
	int ret;

	if (!qp)
		return -EINVAL;
	*qp = NULL;
	if (!prog || flags)
		return -EINVAL;
	q = calloc(1, sizeof(*q));
	if (!q)
		return -ENOMEM;
	pthread_mutex_init(&q->lock, NULL);
	if (use_fake()) {
#ifdef SPEQ_FAKE
		q->be = &spq_fake;
		nspe = 6;
#endif
	} else {
		ret = spq_spufs_probe(q->spufs, sizeof(q->spufs), &nspe);
		if (!ret)
			ret = spq_load_program(q, prog);
		if (ret)
			goto fail;
		q->be = &spq_spufs;
	}
	/* never more workers than SPEs: an oversubscribed context waits for a time slice */
	q->nworkers = nworkers && nworkers < nspe ? nworkers : nspe;
	if (q->nworkers > SPEQ_MAX_WORKERS)
		q->nworkers = SPEQ_MAX_WORKERS;
	ret = -ENOMEM;
	if (posix_memalign(&cb, SPEQ_LINE, SPEQ_CB_SIZE))
		goto fail;
	memset(cb, 0, SPEQ_CB_SIZE);
	q->ctl = cb;
	/* the SPUs' getllar/putllc and the parked futex: keep it resident (the job buffers
	 * fault in through the worker's spu_run, which works); best effort */
	mlock(cb, SPEQ_CB_SIZE);
	q->spin_us = env_uint("SPEQ_SPIN_US", SPIN_US);
	q->free_ms = env_uint("SPEQ_FREE_MS", FREE_MS);
	q->ctl->abi = SPEQ_ABI_VERSION;
	q->ctl->nworkers = q->nworkers;
	q->ctl->tb_hz = spq_tb_hz();
	q->ctl->idle_ticks = idle_ticks(env_uint("SPEQ_IDLE_US", IDLE_US));
	q->alive = q->nworkers;
	pthread_mutex_lock(&queues_lock);
	q->id = next_id++;
	pthread_mutex_unlock(&queues_lock);

	/* the contexts now, so that failures show here */
	for (i = 0; i < q->nworkers; i++) {
		struct spq_worker *w = &q->w[i];

		w->q = q;
		w->idx = i;
		w->ctx = -1;
		ret = q->be->create(w);
		if (ret) {
			while (i--)
				retire(&q->w[i]);
			goto fail;
		}
		w->live = 1;
		INC(&q->spawns);
	}

	pthread_once(&fork_once, fork_init);
	pthread_mutex_lock(&queues_lock);
	q->next = queues;
	queues = q;
	pthread_mutex_unlock(&queues_lock);

	/* workers take no signals (they would interrupt spu_run) */
	sigfillset(&all);
	pthread_sigmask(SIG_SETMASK, &all, &old);
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 256 * 1024);
	for (i = 0; i < q->nworkers; i++) {
		char name[16];

		if (pthread_create(&q->w[i].thread, &attr, worker_main, &q->w[i]))
			break;
		q->w[i].started = 1;
		snprintf(name, sizeof(name), "speq-%u", i);
		pthread_setname_np(q->w[i].thread, name);
	}
	pthread_attr_destroy(&attr);
	pthread_sigmask(SIG_SETMASK, &old, NULL);
	if (i < q->nworkers) {
		speq_close(q);
		return -EAGAIN;
	}
	*qp = q;
	return 0;

fail:
	free_queue(q);
	return ret;
}

int speq_submit(struct speq *q, const struct speq_job *jobs, unsigned n, struct speq_done *done)
{
	struct speq_ctl *c;
	uint32_t head, tail, v, space, k, j, i = 0;

	if (!q || !done || ((uintptr_t)done & (SPEQ_LINE - 1)) || (n && !jobs))
		return -EINVAL;
	if (LOAD(&q->dead) || LOAD(&q->quit))
		return -EIO;
	if (!n)
		return 0;
	c = q->ctl;
	/* counted before any job is visible, so no SPU can take it to 0 early */
	__atomic_fetch_add(&done->pending, n, __ATOMIC_SEQ_CST);
	pthread_mutex_lock(&q->lock);
	while (i < n) {
		head = LOAD(&c->head);
		v = LOAD(&c->tail_parked);
		tail = v >> SPEQ_TAIL_SHIFT;
		space = SPEQ_JOBS - ((tail - head) & SPEQ_SEQ_MASK);
		if (!space) {	/* 64 queued: the workers are busy */
			if (LOAD(&q->dead))
				break;
			sched_yield();
			continue;
		}
		k = n - i < space ? n - i : space;
		for (j = 0; j < k; j++) {
			struct speq_job *s = ring(q, tail + j);

			memcpy(s, &jobs[i + j], sizeof(*s));
			s->done = (uintptr_t)done;
		}
		/* One stwcx.: the tail moves and the parked mask is taken and cleared. The sync
		 * before it (seq_cst) orders the job lines ahead of the tail for the SPUs' DMA.
		 * Only an SPU parking changes the word meanwhile (the tail is ours, under lock). */
		while (!CAS(&c->tail_parked, &v, ((tail + k) & SPEQ_SEQ_MASK) << SPEQ_TAIL_SHIFT))
			;
		if (v & SPEQ_PARKED_MASK)
			futex_wake_all(&c->tail_parked);
		i += k;
	}
	pthread_mutex_unlock(&q->lock);
	if (LOAD(&q->dead))	/* the last worker broke meanwhile: nobody will run them */
		drain_ring(q, -EIO);
	if (i < n) {
		for (; i < n; i++)
			spq_fail_job(done, -EIO);
		return -EIO;
	}
	return 0;
}

int spq_wait_ex(struct speq *q, struct speq_done *done, int spin_us, long timeout_us)
{
	double t0 = spq_now_us(), rem;
	struct timespec ts;
	uint32_t p;

	if (!done)
		return -EINVAL;
	if (q && LOAD(&q->forked))
		return -EIO;
	if (spin_us < 0)
		spin_us = q ? q->spin_us : SPIN_US;
	p = LOAD(&done->pending);
	if ((p & ~SPEQ_WAITERS) && spin_us)
		do {
			cpu_relax();
			p = LOAD(&done->pending);
		} while ((p & ~SPEQ_WAITERS) && spq_now_us() - t0 < spin_us);
	while (p & ~SPEQ_WAITERS) {
		/* the bit tells the last SPU to wake us; set before sleeping on that value */
		if (!(p & SPEQ_WAITERS)) {
			if (!CAS(&done->pending, &p, p | SPEQ_WAITERS))
				continue;
			p |= SPEQ_WAITERS;
		}
		if (timeout_us >= 0) {
			rem = t0 + timeout_us - spq_now_us();
			if (rem <= 0)
				return -ETIMEDOUT;
			us_timespec(rem, &ts);
		}
		/* EAGAIN (changed), EINTR, spurious wakes: all just recheck */
		futex(&done->pending, FUTEX_WAIT_PRIVATE, p, timeout_us >= 0 ? &ts : NULL);
		p = LOAD(&done->pending);
	}
	/* lwsync: the job's output (DMA complete before the SPU's putllc) is read after this */
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	return __atomic_load_n(&done->error, __ATOMIC_RELAXED);
}

int speq_wait(struct speq *q, struct speq_done *done)
{
	return spq_wait_ex(q, done, -1, -1);
}

int speq_run(struct speq *q, const struct speq_job *job)
{
	struct speq_done done __attribute__((aligned(128)));
	int ret;

	memset(&done, 0, sizeof(done));
	ret = speq_submit(q, job, 1, &done);
	return ret ? ret : speq_wait(q, &done);
}

void speq_close(struct speq *q)
{
	struct timespec ts;
	uint32_t *tp, v;
	unsigned i;
	int leaked = 0;

	if (!q)
		return;
	unlist(q);
	if (q->forked) {	/* the parent's threads and contexts: drop our copies only */
		for (i = 0; i < q->nworkers; i++)
			if (q->w[i].live && q->be == &spq_spufs) {
				munmap(q->w[i].ls, 256 * 1024);
				close(q->w[i].ctx);
			}
		free_queue(q);
		return;
	}
	tp = &q->ctl->tail_parked;
	STORE(&q->quit, 1);
	STORE(&q->ctl->quit, 1);	/* a store to line 0: idle SPUs wake (LLR_LOST) and exit */
	/* clear every parked bit: the futex word changes, so no parked thread misses the wake;
	 * a resumed SPU sees quit and exits */
	v = LOAD(tp);
	while (!CAS(tp, &v, v & ~SPEQ_PARKED_MASK))
		;
	futex_wake_all(tp);
	for (i = 0; i < q->nworkers; i++) {
		struct spq_worker *w = &q->w[i];

		if (!w->started) {
			if (w->live)
				retire(w);
			continue;
		}
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += CLOSE_MS / 1000;
		if (pthread_timedjoin_np(w->thread, NULL, &ts)) {
			fprintf(stderr, "speq: worker %u does not stop (SPU stuck?); queue leaked\n", i);
			leaked = 1;
		}
	}
	if (leaked) {	/* its threads may still touch the control block */
		q->leaked = 1;
		return;
	}
	drain_ring(q, -ECANCELED);
	free_queue(q);
}

void spq_tune(struct speq *q, int spin_us, int idle_us, int free_ms)
{
	if (spin_us >= 0)
		q->spin_us = spin_us;
	if (free_ms >= 0)
		q->free_ms = free_ms;
	if (idle_us >= 0)	/* read by the SPUs at every getllar of line 0 */
		STORE(&q->ctl->idle_ticks, idle_ticks(idle_us));
}

void spq_stats(struct speq *q, struct spq_stats *s)
{
	unsigned long *sum[6] = { &s->claims, &s->wakes, &s->spurious, &s->lost, &s->spu_parks,
				  &s->timer };
	unsigned i, k;

	memset(s, 0, sizeof(*s));
	s->workers = q->nworkers;
	s->alive = LOAD(&q->alive);
	s->spawns = LOAD(&q->spawns);
	s->parks = LOAD(&q->parks);
	s->unparks = LOAD(&q->unparks);
	s->frees = LOAD(&q->frees);
	s->faults = LOAD(&q->faults);
	s->failed = LOAD(&q->failed);
	s->exits = LOAD(&q->exits);
	for (i = 0; i < q->nworkers; i++) {
		const uint32_t *c = &wline(q, i)->claims;

		for (k = 0; k < 6; k++)
			*sum[k] += q->w[i].acc[k] + LOAD(&c[k]);
	}
	s->tail_parked = LOAD(&q->ctl->tail_parked);
	s->head = LOAD(&q->ctl->head);
}

unsigned speq_abi_version(void)
{
	return SPEQ_ABI_VERSION;
}
