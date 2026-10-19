/* spetest runner internals: the backend the runner drives (spufs on the PS3, a simulator in the
 * unit tests), the options and the results. */
#ifndef SPT_H
#define SPT_H

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include "proto.h"
#include "oplist.h"

/* what the runner shares with the backend per context */
struct spt_ctx {
	void *priv;		/* the backend's */
	volatile int done;	/* set by the runner when run() has returned */
	pthread_t runner;	/* the thread inside run(), for interrupt() */
};

/* One SPE context at a time. All calls but run() come from the conductor thread; run() blocks in
 * its own thread until the program stops. */
struct spt_be {
	const char *name;
	/* how many SPEs there are (0 if none); fills mnt with the spufs mount point */
	int (*probe)(char *mnt, unsigned len);
	/* the physical SPE numbers Linux lists (spu0, spu1, ...), up to max; their count */
	int (*spe_ids)(unsigned *ids, unsigned max);
	/* a context with the program loaded; r3, r4, r5 as main()'s arguments */
	int (*create)(struct spt_ctx *c, const char *elf, uint64_t r3, uint64_t r4, uint64_t r5);
	/* spu_run until the program stops: the status word (>= 0) or -errno; *event: spufs events */
	long (*run)(struct spt_ctx *c, unsigned *event);
	int (*physid)(struct spt_ctx *c);			/* -1: not on an SPE */
	int (*mbox_put)(struct spt_ctx *c, uint32_t w, int timeout_ms);		/* to the SPU */
	int (*mbox_get)(struct spt_ctx *c, uint32_t *w, int timeout_ms);	/* from the SPU */
	int (*ibox_get)(struct spt_ctx *c, uint32_t *w, int timeout_ms);	/* interrupt mailbox */
	int (*signal_put)(struct spt_ctx *c, int which, uint32_t v);		/* which 1 or 2 */
	int (*signal_type)(struct spt_ctx *c, int which, int or_mode);
	void (*interrupt)(struct spt_ctx *c);		/* make a blocked run() return */
	void (*destroy)(struct spt_ctx *c);
};

extern const struct spt_be spt_be_spufs;

/* outcome of one context run */
enum { R_PASS, R_FAIL, R_CRASH, R_HUNG, R_NOLOAD };

#define MAXMSG 8
struct spt_job {
	int test;
	unsigned slot;
	int outcome;
	int phys, phys_moved;	/* the physical SPE, and whether it changed during the run */
	long status;
	unsigned event;
	unsigned nfail;		/* failures found (SPU side + PPE checks) */
	char msg[MAXMSG][200];
	unsigned nmsg;
	double secs;
	double rate_hz;		/* chan: the decrementer rate measured */
	char info[96];		/* ls: the range tested */
	uint32_t sig[OPX_N];	/* exec: digest of the SPE's results per op */
	int has_sig;
};

#endif
