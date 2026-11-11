/* SPDX-License-Identifier: GPL-2.0 */
/*
 * spe_core.h: the decisions of ps3rsx's SPE client (spe.c) as pure functions of their inputs, so the host
 * tests (test/spe-core-test.c) run the same code: the descriptor check, poll or interrupt, the job's time
 * budget, what a timeout means, and when the SPE counts as gone.
 *
 *  - The descriptor check runs on the PPE before a job is handed to the SPE: every page's source and
 *    destination must be a whole, 4 KiB aligned page inside a window of the kind the op allows (kernel
 *    RAM for plaintext; the VRAM pool, the copy bounce or kernel RAM for ciphertext). The SPU checks
 *    alignment and counts again, but only the PPE knows the windows.
 *  - Poll or interrupt by the job's expected SPE time, not its page count: a job expected to finish within
 *    SPE_POLL_US is polled (spinning costs less than an interrupt round trip); anything slower, and every
 *    job whose pages come from VRAM by SPE reads, waits for the interrupt. A poll is bounded by its
 *    expected time plus a margin, then falls back to the interrupt.
 *  - The budget scales with the job: SPE_BUDGET_FLOOR_US plus a bound per page by path.
 *  - A timeout first looks at the done word (a finished job isn't a timeout) and at the SPE's progress
 *    counter: an SPE that is still progressing gets more time (up to SPE_EXTEND_MAX budgets), and only one
 *    that made no progress is restarted. Only such no-progress restarts count; SPE_GONE_AFTER of them in a
 *    row and the SPE is given up for the boot.
 */
#ifndef PS3RSX_SPE_CORE_H
#define PS3RSX_SPE_CORE_H

#include "vseal_abi.h"

#define SPE_POLL_US		50	/* poll only jobs expected to take less */
#define SPE_POLL_MARGIN_US	30	/* a poll spins at most expected + this, then waits for the interrupt */
#define SPE_BUDGET_FLOOR_US	20000
#define SPE_PAGE_US_SEAL	15	/* expected SPE time a page: seal or open from kernel RAM or the bounce */
#define SPE_PAGE_US_VRAM	250	/* ... open whose ciphertext the SPE reads from VRAM itself */
#define SPE_PAGE_BUDGET_SEAL	100	/* budget a page */
#define SPE_PAGE_BUDGET_VRAM	1000
#define SPE_EXTEND_MAX		8	/* budgets a progressing job may take in all */
#define SPE_GONE_AFTER		3	/* no-progress restarts in a row */

enum spe_win_kind { SPE_WIN_RAM = 1, SPE_WIN_VRAM = 2, SPE_WIN_BOUNCE = 4 };

struct spe_window {
	vs_u64 start, len;
	unsigned int kind;
};

/* the path a job's source pages take, for its expected time */
enum spe_path { SPE_PATH_RAM, SPE_PATH_VRAM };

static inline int spe_win_kind_of(const struct spe_window *w, unsigned int nw, vs_u64 ea)
{
	unsigned int i;

	if (ea & (VSEAL_PAGE - 1))
		return 0;
	for (i = 0; i < nw; i++)
		if (ea >= w[i].start && ea - w[i].start <= w[i].len - VSEAL_PAGE && w[i].len >= VSEAL_PAGE)
			return w[i].kind;
	return 0;
}

/* 0, or the first page that is refused + 1 (or VSEAL_PAGES_MAX + 2 for the header). test: test ops and
 * RAM-to-RAM seals allowed (the debugfs hook of test builds) */
static inline int spe_check_job(const struct vseal_job *j, const struct spe_window *w, unsigned int nw,
				int test)
{
	unsigned int i, sk, dk;

#ifdef SPE_FIXOFF_NO_CHECK
	return 0;
#endif
	if (j->magic != VSEAL_MAGIC)
		return VSEAL_PAGES_MAX + 2;
	switch (j->op) {
	case VSEAL_OP_ECHO:
	case VSEAL_OP_GOLDEN:
		return j->npages ? VSEAL_PAGES_MAX + 2 : 0;
	case VSEAL_OP_TEST_HANG:
	case VSEAL_OP_TEST_NOIRQ:
		return test && !j->npages ? 0 : VSEAL_PAGES_MAX + 2;
	case VSEAL_OP_SEAL:
		sk = SPE_WIN_RAM;
		dk = test ? SPE_WIN_VRAM | SPE_WIN_RAM : SPE_WIN_VRAM;
		break;
	case VSEAL_OP_TEST_SLOW:
	case VSEAL_OP_TEST_DIFF:
		if (!test)
			return VSEAL_PAGES_MAX + 2;
		sk = SPE_WIN_RAM;
		dk = SPE_WIN_RAM;
		break;
	case VSEAL_OP_OPEN:
		sk = SPE_WIN_VRAM | SPE_WIN_BOUNCE | SPE_WIN_RAM;
		dk = SPE_WIN_RAM;
		break;
	default:
		return VSEAL_PAGES_MAX + 2;
	}
	if (j->npages == 0 || j->npages > VSEAL_PAGES_MAX)
		return VSEAL_PAGES_MAX + 2;
	for (i = 0; i < j->npages; i++) {
		int s = spe_win_kind_of(w, nw, j->page[i].src), d = spe_win_kind_of(w, nw, j->page[i].dst);

		if (!(s & sk) || !(d & dk) || j->page[i].src == j->page[i].dst)
			return i + 1;
	}
	return 0;
}

static inline unsigned int spe_expected_us(unsigned int op, unsigned int npages, enum spe_path path)
{
	if (op == VSEAL_OP_ECHO || op == VSEAL_OP_TEST_NOIRQ)
		return 2;
	if (op == VSEAL_OP_GOLDEN)
		return SPE_PAGE_US_SEAL;
	return npages * (path == SPE_PATH_VRAM ? SPE_PAGE_US_VRAM : SPE_PAGE_US_SEAL);
}

/* poll (1) or wait for the interrupt (0) */
static inline int spe_poll(unsigned int op, unsigned int npages, enum spe_path path)
{
	return path != SPE_PATH_VRAM && spe_expected_us(op, npages, path) < SPE_POLL_US;
}

static inline unsigned int spe_poll_spin_us(unsigned int op, unsigned int npages, enum spe_path path)
{
	return spe_expected_us(op, npages, path) + SPE_POLL_MARGIN_US;
}

static inline unsigned int spe_budget_us(unsigned int op, unsigned int npages, enum spe_path path)
{
	(void)op;
#ifdef SPE_FIXOFF_NO_TIMEOUT
	return 0xffffffffu;
#endif
	return SPE_BUDGET_FLOOR_US +
	       npages * (path == SPE_PATH_VRAM ? SPE_PAGE_BUDGET_VRAM : SPE_PAGE_BUDGET_SEAL);
}

/* a job's sequence word: the SPE's generation (bumped at every load and restart) above the job's number,
 * so a done word or an interrupt left over from an earlier incarnation never matches a current job */
static inline vs_u64 spe_seq(vs_u32 gen, vs_u32 n)
{
	return (vs_u64)gen << 32 | n;
}

/* the wait of one job */
struct spe_wait {
	vs_u64 seq;
	unsigned int budget_us, budgets;
	vs_u64 progress_at_check;
};

enum spe_verdict { SPE_WAIT_MORE, SPE_DONE, SPE_RESTART };

static inline void spe_wait_start(struct spe_wait *w, vs_u64 seq, unsigned int budget_us, vs_u64 progress)
{
	w->seq = seq;
	w->budget_us = budget_us;
	w->budgets = 1;
	w->progress_at_check = progress;
}

/* A budget ran out (or an SPE error was seen: err): what now. done: the job's done word; progress: the
 * status line's counter now. A finished job is done whatever else; an error or no progress means a
 * restart; progress means another budget, up to SPE_EXTEND_MAX in all. */
static inline enum spe_verdict spe_on_timeout(struct spe_wait *w, vs_u64 done, vs_u64 progress, int err)
{
	if (done == w->seq)
		return SPE_DONE;
	if (err)
		return SPE_RESTART;
#ifndef SPE_FIXOFF_RESTART_ON_SLOW
	if (progress != w->progress_at_check && w->budgets < SPE_EXTEND_MAX) {
		w->progress_at_check = progress;
		w->budgets++;
		return SPE_WAIT_MORE;
	}
#endif
	return SPE_RESTART;
}

/* the SPE's health across jobs */
struct spe_health {
	unsigned int noprogress_restarts;	/* in a row */
	int gone;
};

/* after a restart: progress tells whether the timed-out job had moved at all since it was submitted */
static inline void spe_restarted(struct spe_health *h, int progressed, int reset_ok)
{
	if (!reset_ok) {
		h->gone = 1;
		return;
	}
	if (progressed) {
		h->noprogress_restarts = 0;
		return;
	}
	if (++h->noprogress_restarts >= SPE_GONE_AFTER)
		h->gone = 1;
}

/* At unload: the ring, the status line and every page a job may name are freed only when the SPE was
 * given back quiet (spu_kernel_release() returned 0) and was never given up unquiet; otherwise the SPE
 * may still DMA into them, and they stay allocated for the boot. */
static inline int spe_may_free(int release_ret, int kept_unquiet)
{
#ifdef SPE_FIXOFF_FREE_UNQUIET
	return 1;
#endif
	return release_ret == 0 && !kept_unquiet;
}

static inline void spe_job_succeeded(struct spe_health *h)
{
	h->noprogress_restarts = 0;
}

#endif
