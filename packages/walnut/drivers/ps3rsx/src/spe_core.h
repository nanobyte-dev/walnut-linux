/* SPDX-License-Identifier: GPL-2.0 */
/*
 * spe_core.h: the decisions of ps3rsx's SPE client as pure functions of their inputs, so the host tests
 * (test/spe-core-test.c) run the same code: the descriptor check, poll or interrupt, the job's time budget,
 * what a timeout means, when the SPE counts as gone, and what a caller may do with its pages afterwards.
 * The code that acts on these decisions (the wait, the drain, the restart, the interrupt handling) is
 * spe_run.c, which the host tests also run.
 *
 *  - The descriptor check runs on the PPE before a job is handed to the SPE. Every page's source and
 *    destination must be a whole, 4 KiB aligned page. Refused ranges are checked first and always win (the
 *    job ring and the status line, the copy bounce, every GART-mapped chunk, the VRAM pool where plaintext
 *    would go); then the address must lie in a window of a kind the op allows (kernel RAM for plaintext;
 *    the VRAM pool, the bounce or kernel RAM for ciphertext); and a page in a RAM or VRAM window must pass
 *    the caller's per-page predicate (RAM: not mapped for the GPU; VRAM: a slot of the pool), which such
 *    windows can't be given without. So plaintext only ever lands in kernel RAM outside every GPU mapping,
 *    and a caller's bug can't point the SPE at the ring or anything refused.
 *  - Poll or interrupt by the job's expected SPE time, not its page count: a job expected to finish within
 *    SPE_POLL_US is polled; anything slower, and every job whose pages the SPE reads from VRAM, waits for the
 *    interrupt. A poll is bounded by its expected time plus a margin, then falls back to the interrupt.
 *  - The budget scales with the job: SPE_BUDGET_FLOOR_US plus a bound per page by path.
 *  - At the deadline (never at an early wake) the done word is read first (a finished job isn't a timeout),
 *    then the SPE's progress counter: an SPE still progressing gets another budget, up to SPE_EXTEND_MAX
 *    budgets in all, and only one that made no progress, or that has a confirmed error, is restarted. Only
 *    no-progress restarts count, and only a sealing or opening job that finishes clears the count (the
 *    known-answer job after a restart doesn't); SPE_GONE_AFTER in a row and the SPE is given up.
 *  - After a reset that couldn't prove the SPE stopped, nothing any descriptor ever named may be freed or
 *    reused for the rest of the boot: spe_after_reset() says SPE_KEEP, and the job returns -EHWPOISON. A
 *    reset refused with -EINVAL means the SPE was already released (quiet): the client stops, -ENODEV,
 *    nothing counted as a failure.
 */
#ifndef PS3RSX_SPE_CORE_H
#define PS3RSX_SPE_CORE_H

#include "vseal_abi.h"

#define SPE_POLL_US		50	/* poll only jobs expected to take less */
#define SPE_POLL_MARGIN_US	30	/* a poll spins at most expected + this, then waits for the interrupt */
#define SPE_DRAIN_US		20	/* how long a drain waits for the job's interrupt-mailbox entry */
#define SPE_BUDGET_FLOOR_US	20000
#define SPE_PAGE_US_SEAL	15	/* expected SPE time a page: seal or open from kernel RAM or the bounce */
#define SPE_PAGE_US_VRAM	250	/* ... open whose ciphertext the SPE reads from VRAM itself */
#define SPE_PAGE_BUDGET_SEAL	100	/* budget a page */
#define SPE_PAGE_BUDGET_VRAM	1000
#define SPE_EXTEND_MAX		8	/* budgets a progressing job may take in all */
#define SPE_GONE_AFTER		3	/* no-progress restarts in a row */
#define SPE_NWIN		8
#define SPE_NREFUSED		16

enum spe_win_kind { SPE_WIN_RAM = 1, SPE_WIN_VRAM = 2, SPE_WIN_BOUNCE = 4 };

struct spe_window {
	vs_u64 start, len;
	unsigned int kind;
};

/* the ranges a job may name, and the ones it never may */
struct spe_bounds {
	struct spe_window win[SPE_NWIN];
	unsigned int nwin;
	struct spe_window refused[SPE_NREFUSED];
	unsigned int nrefused;
	/* per page in a RAM or VRAM window (kind: SPE_WIN_RAM or SPE_WIN_VRAM): 1 if it may be named */
	int (*page_ok)(void *arg, vs_u64 ea, unsigned int kind);
	void *page_ok_arg;
};

/* the path a job's source pages take, for its expected time */
enum spe_path { SPE_PATH_RAM, SPE_PATH_VRAM };

static inline int spe_in(const struct spe_window *w, vs_u64 ea)
{
	return w->len >= VSEAL_PAGE && ea >= w->start && ea - w->start <= w->len - VSEAL_PAGE;
}

static inline int spe_overlaps(const struct spe_window *w, vs_u64 ea)
{
	return ea + VSEAL_PAGE > w->start && ea < w->start + w->len;
}

/* the kind of the page at ea, or 0 if it may not be named at all */
static inline unsigned int spe_kind_of(const struct spe_bounds *b, vs_u64 ea)
{
	unsigned int i;

	if (ea & (VSEAL_PAGE - 1) || ea + VSEAL_PAGE < ea)
		return 0;
#ifndef SPE_FIXOFF_NO_REFUSED
	for (i = 0; i < b->nrefused; i++)
		if (spe_overlaps(&b->refused[i], ea))
			return 0;
#endif
	for (i = 0; i < b->nwin; i++)
		if (spe_in(&b->win[i], ea)) {
			if (b->win[i].kind != SPE_WIN_BOUNCE &&
			    (!b->page_ok || !b->page_ok(b->page_ok_arg, ea, b->win[i].kind)))
				return 0;
			return b->win[i].kind;
		}
	return 0;
}

/* a window list as a caller gives it: 0, or -1 for anything off (alignment, zero or wrapping lengths, an
 * unknown kind, more than SPE_NWIN) */
static inline int spe_windows_valid(const struct spe_window *w, unsigned int n)
{
	unsigned int i;

	if (n > SPE_NWIN)
		return -1;
	for (i = 0; i < n; i++)
		if ((w[i].start | w[i].len) & (VSEAL_PAGE - 1) || !w[i].len || w[i].start + w[i].len < w[i].start ||
		    (w[i].kind != SPE_WIN_RAM && w[i].kind != SPE_WIN_VRAM && w[i].kind != SPE_WIN_BOUNCE))
			return -1;
	return 0;
}

/* a whole bounds set as a caller gives it: the windows and the refused ranges valid (refused entries take
 * any kind), and a predicate whenever a RAM or VRAM window is given */
static inline int spe_bounds_valid(const struct spe_window *w, unsigned int nw, const struct spe_window *r,
				   unsigned int nr, int has_page_ok)
{
	unsigned int i;

	if (spe_windows_valid(w, nw) || nr > SPE_NREFUSED)
		return -1;
	for (i = 0; i < nr; i++) {
		struct spe_window t = r[i];

		t.kind = SPE_WIN_RAM;
		if (spe_windows_valid(&t, 1))
			return -1;
	}
#ifndef SPE_FIXOFF_RAM_WITHOUT_CHECK
	for (i = 0; i < nw; i++)
		if (w[i].kind != SPE_WIN_BOUNCE && !has_page_ok)
			return -1;
#endif
	return 0;
}

/* 0, or the first page that is refused + 1 (or VSEAL_PAGES_MAX + 2 for the header). test: test ops and
 * RAM-to-RAM seals allowed (the debugfs hook of test builds) */
static inline int spe_check_job(const struct vseal_job *j, const struct spe_bounds *b, int test)
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
	case VSEAL_OP_TEST_BADADDR:	/* the hook's own untranslated address: no window applies */
		return test && j->npages == 1 ? 0 : VSEAL_PAGES_MAX + 2;
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
		unsigned int s = spe_kind_of(b, j->page[i].src), d = spe_kind_of(b, j->page[i].dst);

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
	return 0x7fffffffu;
#endif
	return SPE_BUDGET_FLOOR_US +
	       npages * (path == SPE_PATH_VRAM ? SPE_PAGE_BUDGET_VRAM : SPE_PAGE_BUDGET_SEAL);
}

/* a job's sequence word: the SPE's generation (bumped at every load and restart) above the job's number */
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

/* At the deadline (or a confirmed SPE error: err): what now. done: the job's done word; progress: the
 * status line's counter now. A finished job is done whatever else; a confirmed error or no progress means
 * a restart; progress means another budget, up to SPE_EXTEND_MAX in all. */
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

/* after a reset: progressed tells whether the timed-out job had moved at all since it was submitted */
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

/* a job finished: only a sealing or opening job (never the known answer, an echo or a test op) proves
 * the SPE healthy again */
static inline void spe_job_finished(struct spe_health *h, unsigned int op)
{
#ifndef SPE_FIXOFF_GOLDEN_COUNTS
	if (op != VSEAL_OP_SEAL && op != VSEAL_OP_OPEN)
		return;
#endif
	h->noprogress_restarts = 0;
}

/* what a reset's result means for every page a descriptor named: 0, seen quiet; -EINVAL after the client
 * itself released the SPE (a restart that raced the release, which gave it back only once quiet): not ours
 * any more; any other error, or -EINVAL without our release, it couldn't be seen stopped */
enum spe_pages { SPE_FREE_OK, SPE_KEEP, SPE_RELEASED };

static inline enum spe_pages spe_after_reset(int reset_ret, int released)
{
	if (reset_ret == 0)
		return SPE_FREE_OK;
#ifndef SPE_FIXOFF_RELEASED_IS_FAILURE
	if (reset_ret == -22 && released)
		return SPE_RELEASED;
#endif
	return SPE_KEEP;
}

/* the client's return codes (the kernel's errno values, spelt out so the host tests use the same) */
#define SPE_RET_KEEP		(-133)	/* -EHWPOISON: given up unquiet, keep every page for the boot */
#define SPE_RET_NODEV		(-19)	/* -ENODEV: no SPE (never was, or given up quiet): use the disk */
#define SPE_RET_TIMEDOUT	(-110)	/* -ETIMEDOUT: restarted after a proven-quiet reset: resubmit */
#define SPE_RET_INVAL		(-22)	/* -EINVAL: the descriptor check refused it; nothing ran */
#define SPE_RET_IO		(-5)	/* -EIO: the SPU refused the descriptor */
#define SPE_RET_BADMSG		(-74)	/* -EBADMSG: done, some pages refused (see their status) */

/* a failed reset's return: keep every page, never "no SPE" */
static inline int spe_ret_after_reset_failed(void)
{
#ifdef SPE_FIXOFF_RESET_FAIL_NODEV
	return SPE_RET_NODEV;
#endif
	return SPE_RET_KEEP;
}

/* At unload: the ring, the status line and every page a job may name are freed only when the SPE was
 * given back quiet (spu_kernel_release() returned 0) and was never given up unquiet. */
static inline int spe_may_free(int release_ret, int kept_unquiet)
{
#ifdef SPE_FIXOFF_FREE_UNQUIET
	return 1;
#endif
	return release_ret == 0 && !kept_unquiet;
}

#endif
