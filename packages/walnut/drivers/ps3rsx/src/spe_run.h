/* SPDX-License-Identifier: GPL-2.0 */
/*
 * spe_run.h: the SPE client's running code (spe_run.c): a job's submission, its wait (poll, then the
 * interrupt), the interrupt-mailbox drain, the interrupt handlers' bodies, the restart and the give-up. It
 * touches the hardware only through the spe_hw_*() calls below, which spe.c implements for the PS3 and
 * test/spe-run-test.c for a fake SPE on the PC, so the host tests run this very code.
 */
#ifndef PS3RSX_SPE_RUN_H
#define PS3RSX_SPE_RUN_H

#include "spe_core.h"

#ifdef __KERNEL__
#include <linux/compiler.h>
#include <linux/string.h>
#define SPE_RD(x)	READ_ONCE(x)
#define SPE_WR(x, v)	WRITE_ONCE(x, v)
#else
#include <string.h>
#define SPE_RD(x)	(*(volatile __typeof__(x) *)&(x))
#define SPE_WR(x, v)	(*(volatile __typeof__(x) *)&(x) = (v))
#endif

enum spe_state { SPE_OFF, SPE_READY, SPE_GONE };

struct spe_ctl {
	struct vseal_job *ring;		/* VSEAL_JOBS descriptors, one per 4 KiB */
	struct vseal_status *status;	/* the status line */
	vs_u32 gen, n;			/* the load's generation, the job number */
	enum spe_state state;
	int keep_pages;			/* given up unquiet: nothing a descriptor named may be freed */
	int released;			/* the client gave the SPE back (spu_kernel_release) */
	int test;			/* test ops and RAM-to-RAM seals allowed (test builds' hook) */
	struct spe_health health;
	struct spe_bounds bounds;
	/* shared with the interrupt handlers */
	vs_u32 waiting;			/* the mailbox word an interrupt may complete, 0 if none */
	vs_u32 got;			/* the job's word, popped by the interrupt handler */
	vs_u32 err_word;		/* the job (its word) an error was reported during, 0 if none */
	int err_hw;			/* ... reported by the hardware itself (class 0 or 1 status) */
	/* counters */
	vs_u64 jobs, polled, interrupted, extended, restarts, stale, errors, spurious, early_wakes;
	void *hw;
};

/* --- what spe.c (or the host test) provides -------------------------------------------------- */
vs_u64 spe_hw_now_us(void *hw);
void spe_hw_mb(void *hw);
void spe_hw_rmb(void *hw);
void spe_hw_relax(void *hw);
unsigned int spe_hw_inbox_free(void *hw);	/* free entries of the SPU's inbound mailbox */
void spe_hw_inbox_write(void *hw, vs_u32 word);
unsigned int spe_hw_ibox_count(void *hw);	/* entries in the SPU's outbound interrupt mailbox */
vs_u32 spe_hw_ibox_pop(void *hw);
void spe_hw_ibox_arm(void *hw);			/* clear the class-2 mailbox status, then unmask it */
void spe_hw_ibox_rearm_irq(void *hw);		/* from the handler (its lock held): unmask again */
void spe_hw_ibox_clear(void *hw);		/* clear a latched class-2 mailbox status, masked */
void spe_hw_sleep_prepare(void *hw);		/* forget earlier wakes */
int spe_hw_sleep(void *hw, vs_u64 us);		/* 1 woken, 0 timed out */
void spe_hw_wake(void *hw);			/* from a handler */
int spe_hw_trouble(void *hw);			/* SPU_Status or the MFC say it stopped or failed */
int spe_hw_reset(void *hw);			/* spu_kernel_reset(): 0 only if seen quiet */
int spe_hw_load(void *hw);			/* the image, the parameters, run, LS decode off, unmask */
void spe_hw_give_up(void *hw, int quiet);	/* quiet: may be given back to spufs now */

/* --- the client ------------------------------------------------------------------------------ */
/* run descriptor idx (op, npages, page[] filled by the caller); counted: a sealing or opening job, whose
 * finish clears the restart count. 0, SPE_RET_BADMSG (some pages refused), SPE_RET_IO (refused by the
 * SPU), SPE_RET_INVAL (refused by the check: nothing ran), SPE_RET_TIMEDOUT (restarted after a quiet
 * reset), SPE_RET_NODEV (no SPE), SPE_RET_KEEP (given up unquiet: keep every page). */
int spe_run_job(struct spe_ctl *c, unsigned int idx, enum spe_path path);
/* the load's start: the ring and the status line zeroed, the program loaded, the known answer checked */
int spe_start(struct spe_ctl *c);
/* the known-answer job (vseal_golden.h): 0 if right */
int spe_golden(struct spe_ctl *c);
/* the interrupt handlers' bodies (hard IRQ, the SPE's register lock held) */
void spe_irq_ibox(struct spe_ctl *c);
/* cls: the interrupt class; detail: class 0's pending status, class 1's DSISR (0 for class 2) */
void spe_irq_stop(struct spe_ctl *c, int cls, vs_u64 detail);

#endif
