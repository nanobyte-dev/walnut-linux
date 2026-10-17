/* libspeq internals: the queue (lib/queue.c), the context backends (lib/spe.c: spufs;
 * lib/fake.c: SPU programs built for the host, on threads, for the tests) and the spq_* calls
 * speq-bench and the tests use. Not exported from libspeq.so (libspeq.map: speq_* only). */
#ifndef SPEQ_INT_H
#define SPEQ_INT_H

#include <pthread.h>
#include <stdint.h>
#include "speq-abi.h"
#include "speq.h"

struct speq;

struct spq_worker {
	struct speq *q;
	unsigned idx;
	pthread_t thread;
	int started;		/* thread created */
	int live;		/* has a context */
	int broken;		/* gave up: contexts die without running a job */
	unsigned gen;		/* contexts created, for their names */
	unsigned npc;
	unsigned strikes;	/* contexts died in a row without claiming a job */
	unsigned long acc[6];	/* counters of its dead contexts' lines (claims .. timer) */
	int ctx;		/* spufs: the context's fd */
	void *ls;		/* spufs: the local store mmap */
	void *fake;		/* fake backend's state */
	char path[96];		/* spufs: the context's directory */
};

struct spq_backend {
	const char *name;
	/* a fresh context with the program loaded and r3/r4/r6 set; sets w->npc */
	int (*create)(struct spq_worker *w);
	/* spu_run: the SPU status (>= 0) or -errno; *event as spu_run's */
	long (*run)(struct spq_worker *w, unsigned *npc, unsigned *event);
	void (*destroy)(struct spq_worker *w);
};

struct speq {
	struct speq_ctl *ctl;	/* the control block, SPEQ_CB_SIZE, mlocked */
	const struct spq_backend *be;
	pthread_mutex_t lock;	/* submitters */
	unsigned nworkers, id;
	int quit, dead, forked, leaked;
	int alive;		/* workers not broken */
	unsigned spin_us, free_ms;
	uint8_t *img;		/* the SPU ELF */
	size_t img_size;
	char prog[64];		/* the program as speq_open got it (the fake backend's lookup) */
	unsigned order;		/* SPEQ_ORDER_* (speq-abi.h) */
	char spufs[64];
	struct spq_worker w[SPEQ_MAX_WORKERS];
	struct speq *next;	/* open queues, for the fork handler */
	/* PPE-side counters (__atomic) */
	unsigned long spawns, parks, unparks, frees, faults, failed, exits;
};

struct spq_stats {
	unsigned workers, alive;
	unsigned long spawns, parks, unparks, frees, faults, failed, exits;
	/* sums of the workers' lines, as of each one's last claim or park */
	unsigned long claims, wakes, spurious, lost, spu_parks, timer;
	uint32_t tail_parked, head;
};

/* queue.c */
/* speq_open with libspeq's own flags too: SPQ_OPEN_DEADLINE, workers claim jobs by deadline
 * (speq-abi.h, the deadline pool); SPQ_OPEN_FREE, nworkers but no more than the SPEs other
 * queues leave (at least one) */
#define SPQ_OPEN_DEADLINE	0x100u
#define SPQ_OPEN_FREE		0x200u
int spq_open_ex(struct speq **q, const char *prog, unsigned nworkers, unsigned flags);
/* speq_submit with the jobs' deadline (spq_deadline: microseconds of CLOCK_MONOTONIC), the
 * order workers of a deadline queue take them in; speq_submit's jobs come after every deadline
 * (UINT64_MAX), in arrival order. Other queues ignore it. */
int spq_submit_dl(struct speq *q, const struct speq_job *jobs, unsigned n, struct speq_done *done,
		  uint64_t deadline);
uint64_t spq_deadline(double us_from_now);
int spq_wait_ex(struct speq *q, struct speq_done *done, int spin_us, long timeout_us);
void spq_tune(struct speq *q, int spin_us, int idle_us, int free_ms);	/* < 0: unchanged */
void spq_stats(struct speq *q, struct spq_stats *s);
uint32_t spq_tb_hz(void);
double spq_now_us(void);
void spq_fail_job(struct speq_done *done, int err);

/* pool.c: the process's deadline pool (deadline.spu), opened on first use; NULL with *err once
 * it can't be had. spq_pool_check(q, ret) after a failed submit (its return; after a failed
 * wait, 0: a job's own -EIO is not the pool's): marks it broken when its workers are gone. */
struct speq *spq_pool(int *err);
int spq_pool_ok(void);
void spq_pool_broke(void);
void spq_pool_check(struct speq *q, int ret);

/* spe.c */
extern const struct spq_backend spq_spufs;
int spq_spufs_probe(char *mnt, size_t len, unsigned *nspe);
int spq_load_program(struct speq *q, const char *prog);
int spq_elf_check(const uint8_t *img, size_t size, uint32_t *entry);

/* s3tc.c: for speq-dxt and the tests */
enum { SPQ_S3TC_OFF, SPQ_S3TC_PPE, SPQ_S3TC_SPU, SPQ_S3TC_AUTO };
struct spq_s3tc_stats {
	unsigned long spu;	/* images encoded on the SPEs */
	unsigned long ppe;	/* ... on the PPE (redone ones included) */
	unsigned long redone;	/* SPU path failed, redone on the PPE */
	unsigned long jobs;	/* SPU jobs submitted */
};
int spq_s3tc_mode(int mode);	/* overrides SPEQ_S3TC (-1: no change); returns the mode */
void spq_s3tc_stats(struct spq_s3tc_stats *s);
struct speq *spq_s3tc_queue(void);	/* the queue, once opened */
/* Tests: when n > 0, the next SPU call gives job n - 1 (or its last) an unmapped dst EA, so
 * that job faults and the image is redone on the PPE */
extern int spq_s3tc_fault;

/* synth.c: for the tests. Mode SPQ_SYNTH_* (-1: unchanged), the wait's timeout, its allowance
 * per voice block and the auto mode's minimum (< 0: unchanged). A synth opened before keeps its
 * queue (ppe: none). */
enum { SPQ_SYNTH_OFF, SPQ_SYNTH_PPE, SPQ_SYNTH_SPU, SPQ_SYNTH_AUTO };
struct spq_synth_stats {
	unsigned long rounds, spu, ppe, small, ents_spu, ents_ppe, jobs, timeouts, errors,
		      nofree, backoffs, redone;
	double wait_us, wait_max;
};
struct speq_synth;
void spq_synth_tune(int mode, long timeout_us, int per_block_us, int min_voices);
void spq_synth_stats(struct speq_synth *s, struct spq_synth_stats *st);

/* skin.c: for speq-skin and the tests. Mode SPQ_SKIN_* (-1: unchanged), the wait's timeout, its
 * allowance per vertex (ns) and the auto mode's minimum (< 0: unchanged). A speq_skin opened
 * before keeps its queue (ppe: none). */
enum { SPQ_SKIN_OFF, SPQ_SKIN_PPE, SPQ_SKIN_SPU, SPQ_SKIN_AUTO };
struct spq_skin_stats {
	unsigned long batches, spu, ppe, small, declined, meshes_spu, verts_spu, verts_ppe, jobs,
		      timeouts, errors, refused, nofree, backoffs;
	double wait_us, wait_max;
};
struct speq_skin;
void spq_skin_tune(int mode, long timeout_us, int per_vert_ns, int min_verts);
void spq_skin_stats(struct speq_skin *s, struct spq_skin_stats *st);
/* Tests: when n > 0, the n-th mesh added on the SPEs from now gets an unmapped stream EA, so
 * its jobs fault */
extern int spq_skin_fault;

/* fake.c (SPEQ_FAKE builds only) */
extern const struct spq_backend spq_fake;

#endif
