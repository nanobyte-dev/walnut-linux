// SPDX-License-Identifier: GPL-2.0
/*
 * spe_run.c: the SPE client's running code (spe_run.h), built into ps3rsx and into the host tests.
 *
 * A job. Its descriptor's input part is filled and checked (spe_core.h), its seq and mailbox word made
 * from the load's generation and the job's number (vseal_word()), the result part cleared; then the
 * word goes into the SPU's inbound mailbox. The SPU answers by writing the result part, the done word
 * last, then the same word into its outbound interrupt mailbox.
 *
 * The wait. A short job (spe_poll()) is polled on the done word, for a bounded time; after it the
 * interrupt-mailbox entry is drained (popping until the job's own word, SPE_DRAIN_US at most) and the
 * class-2 mailbox status cleared, so nothing latched is left for the next job. A longer job, or a polled
 * one that didn't finish, arms the interrupt (status cleared first, then unmasked) and re-reads the done
 * word after arming, so a finish just before the arm isn't missed. It then sleeps to an absolute
 * deadline: a wake before it (an interrupt for an older word, a spurious class-0 call) goes back to sleep
 * for the rest; an error the handler saw counts only if SPU_Status or the MFC confirm it. At the
 * deadline, spe_on_timeout() decides: done, another budget (progress), or a restart.
 *
 * The restart. spu_kernel_reset() (spe_hw_reset): if it couldn't prove the SPE stopped, the SPE is given up
 * unquiet and every page any descriptor named must be kept for the boot (SPE_RET_KEEP). Otherwise the ring
 * and the status line are zeroed (no old descriptor can run again), the program is loaded and the known
 * answer checked, and the job returns SPE_RET_TIMEDOUT: its pages may be freed, and the caller resubmits
 * under fresh counters. SPE_GONE_AFTER restarts in a row without progress (a known answer doesn't count as
 * progress) and the SPE is given up quiet: SPE_RET_NODEV from then on.
 *
 * The handlers. The interrupt-mailbox handler pops only when the mailbox says an entry is there (else it
 * re-arms and returns), and wakes the waiter only for its own word. The stop handler ignores a class-0 call
 * with nothing pending and anything while no job runs, and tags what it reports with the job's word, so
 * the waiter never acts on an error of another job or load.
 */
#include "spe_run.h"
#include "vseal_golden.h"

static void drain(struct spe_ctl *c, vs_u32 word)
{
	vs_u64 end = spe_hw_now_us(c->hw) + SPE_DRAIN_US;

	for (;;) {
		while (spe_hw_ibox_count(c->hw)) {
			vs_u32 v = spe_hw_ibox_pop(c->hw);

			if (v == word) {
				spe_hw_ibox_clear(c->hw);
				return;
			}
			c->stale++;
		}
		if (SPE_RD(c->got) == word || spe_hw_now_us(c->hw) > end)
			break;
		spe_hw_relax(c->hw);
	}
	spe_hw_ibox_clear(c->hw);
}

static void give_up(struct spe_ctl *c, int quiet)
{
	c->state = SPE_GONE;
	if (!quiet)
		c->keep_pages = 1;
	spe_hw_give_up(c->hw, quiet);
}

static void zero_ring(struct spe_ctl *c)
{
#ifndef SPE_FIXOFF_KEEP_RING		/* (the fix-off: an old load's descriptors left in the ring) */
	memset(c->ring, 0, VSEAL_JOBS * VSEAL_PAGE);
#endif
	memset(c->status, 0, sizeof(*c->status));
	spe_hw_mb(c->hw);
}

/* the load's program and its known answer; the ring and the status line zeroed first */
int spe_start(struct spe_ctl *c)
{
	int ret;

	zero_ring(c);
	c->gen++;
	c->n = 0;
	ret = spe_hw_load(c->hw);
	if (ret)
		return ret;
	c->state = SPE_READY;
	ret = spe_golden(c);
	if (ret == SPE_RET_TIMEDOUT && c->state == SPE_READY)
		return 0;	/* restarted within, and that load's known answer was right */
	return ret;
}

/* returns the job's verdict after a restart (TIMEDOUT, NODEV or KEEP) */
static int restart(struct spe_ctl *c, int progressed)
{
	int ret;

	c->restarts++;
	SPE_WR(c->waiting, 0);
	ret = spe_hw_reset(c->hw);
	if (spe_after_reset(ret) == SPE_RELEASED) {	/* released under us: stop quietly */
		c->restarts--;
		c->state = SPE_GONE;
		return SPE_RET_NODEV;
	}
	if (spe_after_reset(ret) == SPE_KEEP) {
		spe_restarted(&c->health, progressed, 0);
		give_up(c, 0);
		return spe_ret_after_reset_failed();
	}
	spe_restarted(&c->health, progressed, 1);
	if (c->health.gone) {
		zero_ring(c);
		give_up(c, 1);
		return SPE_RET_NODEV;
	}
	if (spe_start(c)) {
		if (c->state != SPE_GONE)
			give_up(c, 1);
		return c->keep_pages ? SPE_RET_KEEP : SPE_RET_NODEV;
	}
	return SPE_RET_TIMEDOUT;
}

static int finished(struct spe_ctl *c, struct vseal_job *j, unsigned int op)
{
	unsigned int i;

	spe_hw_rmb(c->hw);		/* the results after the done word */
	SPE_WR(c->waiting, 0);
	SPE_WR(j->magic, 0);		/* a replay of this word now finds no job */
	spe_job_finished(&c->health, op);
	if (j->status != VSEAL_ST_OK)
		return SPE_RET_IO;
	for (i = 0; i < j->npages; i++)
		if (j->res[i].status != VSEAL_ST_OK)
			return SPE_RET_BADMSG;
	return 0;
}

int spe_run_job(struct spe_ctl *c, unsigned int idx, enum spe_path path)
{
	struct vseal_job *j;
	unsigned int op, n;
	struct spe_wait w;
	vs_u64 seq, p0, now, deadline;
	vs_u32 word;

	if (c->state != SPE_READY)
		return c->keep_pages ? SPE_RET_KEEP : SPE_RET_NODEV;
	if (idx >= VSEAL_JOBS)
		return SPE_RET_INVAL;
	j = &c->ring[idx];
	op = j->op;
	n = j->npages;
	j->magic = VSEAL_MAGIC;
	if (spe_check_job(j, &c->bounds, c->test))
		return SPE_RET_INVAL;
	seq = spe_seq(c->gen, ++c->n);
	word = vseal_word(seq, idx);
	j->seq = seq;
	memset((unsigned char *)j + VSEAL_OUT_OFF, 0, VSEAL_OUT_BYTES);
	p0 = SPE_RD(c->status->progress);
	SPE_WR(c->got, 0);
	SPE_WR(c->err_word, 0);
	SPE_WR(c->waiting, word);
	spe_hw_sleep_prepare(c->hw);
	spe_hw_mb(c->hw);		/* the descriptor in memory before the SPU hears of it */
	if (!spe_hw_inbox_free(c->hw))	/* the inbound mailbox full: the SPU is stuck */
		return restart(c, 0);
	spe_hw_inbox_write(c->hw, word);
	c->jobs++;
	spe_wait_start(&w, seq, spe_budget_us(op, n, path), p0);

	if (spe_poll(op, n, path)) {
		vs_u64 end = spe_hw_now_us(c->hw) + spe_poll_spin_us(op, n, path);

		while (SPE_RD(j->done) != seq && spe_hw_now_us(c->hw) <= end)
			spe_hw_relax(c->hw);
		if (SPE_RD(j->done) == seq) {
			spe_hw_rmb(c->hw);
			drain(c, word);
			c->polled++;
			return finished(c, j, op);
		}
	}
	spe_hw_ibox_arm(c->hw);		/* status cleared, then unmasked */
	deadline = spe_hw_now_us(c->hw) + w.budget_us;
	for (;;) {
		if (SPE_RD(j->done) == seq)	/* also right after the arm: a finish just before it */
			break;
		now = spe_hw_now_us(c->hw);
		if (SPE_RD(c->err_word)) {
			/* an error counts only for this job's word (its generation and number), and only if the
			 * hardware reported it (a class 0 or 1 status) or SPU_Status and the MFC confirm it */
			int mine = SPE_RD(c->err_word) == word, hw = SPE_RD(c->err_hw);

			SPE_WR(c->err_word, 0);
			if (mine && (hw || spe_hw_trouble(c->hw))) {
				if (spe_on_timeout(&w, SPE_RD(j->done), SPE_RD(c->status->progress), 1) == SPE_DONE)
					break;
				c->errors++;
				return restart(c, SPE_RD(c->status->progress) != p0);
			}
			c->spurious++;
		}
		if (now < deadline) {
			if (spe_hw_sleep(c->hw, deadline - now) && SPE_RD(j->done) != seq && SPE_RD(c->got) != word) {
				c->early_wakes++;	/* not this job's end: sleep again for the rest */
#ifdef SPE_FIXOFF_WAKE_IS_TIMEOUT
				deadline = now;	/* (the fix-off: an early wake taken as the deadline) */
#endif
			}
			spe_hw_sleep_prepare(c->hw);	/* before the done word is read again */
			continue;
		}
		switch (spe_on_timeout(&w, SPE_RD(j->done), SPE_RD(c->status->progress), 0)) {
		case SPE_DONE:
			goto done;
		case SPE_WAIT_MORE:
			c->extended++;
			deadline = now + w.budget_us;
			spe_hw_ibox_arm(c->hw);
			continue;
		case SPE_RESTART:
			return restart(c, SPE_RD(c->status->progress) != p0);
		}
	}
done:
	spe_hw_rmb(c->hw);
	c->interrupted++;
	if (SPE_RD(c->got) != word)	/* finished on the done word: take its mailbox entry too */
		drain(c, word);
	return finished(c, j, op);
}

int spe_golden(struct spe_ctl *c)
{
	unsigned int idx = c->n % VSEAL_JOBS;
	struct vseal_job *j = &c->ring[idx];
	int ret;

	memset(j, 0, sizeof(*j));
	j->op = VSEAL_OP_GOLDEN;
	ret = spe_run_job(c, idx, SPE_PATH_RAM);
	if (ret)
		return ret;
	if (memcmp(j->res[0].tag, vseal_golden_tag, 16) || j->fnv != VSEAL_GOLDEN_FNV) {
		give_up(c, 1);
		return SPE_RET_IO;
	}
	return 0;
}

void spe_irq_ibox(struct spe_ctl *c)
{
	vs_u32 v, want = SPE_RD(c->waiting);

	if (!spe_hw_ibox_count(c->hw)) {	/* a latched status with nothing there: arm again */
		spe_hw_ibox_rearm_irq(c->hw);
		return;
	}
	v = spe_hw_ibox_pop(c->hw);
	if (want && v == want) {
		SPE_WR(c->got, v);
		spe_hw_wake(c->hw);
	} else {
		c->stale++;
		spe_hw_ibox_rearm_irq(c->hw);	/* the job's own entry may still come */
	}
}

void spe_irq_stop(struct spe_ctl *c, int cls, vs_u64 detail)
{
	vs_u32 want = SPE_RD(c->waiting);

	if (!want)
		return;			/* no job: a late interrupt from before */
	if (cls != 2 && !detail) {	/* class 0 with nothing pending (spu_base calls it so), or 1 without a fault */
		c->spurious++;
		return;
	}
	SPE_WR(c->err_hw, cls != 2);
	SPE_WR(c->err_word, want);
	spe_hw_wake(c->hw);
}
