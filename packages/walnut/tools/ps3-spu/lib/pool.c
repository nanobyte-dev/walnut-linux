/* The process's deadline pool: one queue running deadline.spu (lib/pool-job.h) for every
 * latency-bound service: fluidsynth's voice DSP (synth.c) and vertex skinning (skin.c), and
 * whatever joins them. Decided with the user on 2026-10-17: before it, each service opened its
 * own queue of up to two workers next to DXT's bulk queue of four, so a game with music and
 * skinning ran 8 contexts on 6 SPEs and spufs time-sliced them (80 ms slices: far past any
 * audio deadline). Now a process runs at most DXT's 4 and the pool's 2.
 *
 * The pool opens with the first service that needs the SPEs (SPEQ_POOL_SPES workers, default
 * SPEQ_DEADLINE_SPES = 2, no more than the SPEs other queues of the process leave) and stays
 * open; its workers stay resident 200 ms past their last job (audio callbacks and frames come
 * every 1.5-60 ms) and free their contexts after a minute. Jobs are taken by deadline (each
 * service submits with the time it gives up on them: spq_submit_dl), the earliest first, so a
 * service's backlog never holds back another's earlier deadline; a job already running is not
 * preempted, so each service keeps its jobs short (tens of us to ~0.1 ms). Each service keeps
 * its own policy, waits, timeouts, PPE fallback and back-off; a fault fails only the job that
 * faulted (its worker gets a fresh context). Once every worker has broken, the pool is gone for
 * the process and every service stays on the PPE. */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pool-job.h"
#include "speq-int.h"

#define IDLE_US		200000
#define FREE_MS		60000

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct speq *pool;	/* set once, never closed */
static int perr;		/* the open's error, or -EIO once the pool broke */

struct speq *spq_pool(int *err)
{
	const char *e;
	struct speq *q = NULL;
	unsigned n;
	int ret;

	pthread_mutex_lock(&lock);
	if (!pool && !perr) {
		e = getenv("SPEQ_POOL_SPES");
		n = e && *e ? (unsigned)strtoul(e, NULL, 0) : SPEQ_DEADLINE_SPES;
		if (!n)
			n = SPEQ_DEADLINE_SPES;
		ret = spq_open_ex(&q, POOL_PROG, n, SPQ_OPEN_DEADLINE | SPQ_OPEN_FREE);
		if (ret) {
			perr = ret;
		} else {
			spq_tune(q, -1, IDLE_US, FREE_MS);
			__atomic_store_n(&pool, q, __ATOMIC_RELEASE);
		}
		e = getenv("SPEQ_DEBUG");
		if (e && *e && strcmp(e, "0"))
			fprintf(stderr, "speq: deadline pool (%s): %s, %u workers of %u asked\n",
				POOL_PROG, ret ? strerror(-ret) : "open", ret ? 0 : speq_nworkers(q), n);
	}
	q = perr ? NULL : pool;
	*err = perr;
	pthread_mutex_unlock(&lock);
	return q;
}

void spq_pool_broke(void)
{
	pthread_mutex_lock(&lock);
	if (!perr)
		perr = -EIO;
	pthread_mutex_unlock(&lock);
}

int spq_pool_ok(void)
{
	return !__atomic_load_n(&perr, __ATOMIC_ACQUIRE);
}

/* A service's jobs failed: when the cause is that no worker is left, the pool is gone */
void spq_pool_check(struct speq *q, int ret)
{
	if (ret == -EIO || __atomic_load_n(&q->dead, __ATOMIC_ACQUIRE))
		spq_pool_broke();
}
