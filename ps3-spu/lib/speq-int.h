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
int spq_wait_ex(struct speq *q, struct speq_done *done, int spin_us, long timeout_us);
void spq_tune(struct speq *q, int spin_us, int idle_us, int free_ms);	/* < 0: unchanged */
void spq_stats(struct speq *q, struct spq_stats *s);
uint32_t spq_tb_hz(void);
double spq_now_us(void);
void spq_fail_job(struct speq_done *done, int err);

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

/* fake.c (SPEQ_FAKE builds only) */
extern const struct spq_backend spq_fake;

#endif
