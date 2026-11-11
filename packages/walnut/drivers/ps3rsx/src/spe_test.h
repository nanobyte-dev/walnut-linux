/* SPDX-License-Identifier: GPL-2.0 */
/*
 * spe_test.h: the SPE client's debugfs hook, test builds only (PS3RSX_VSEAL_TEST:
 * DEB_BUILD_OPTIONS=ps3rsx-vseal, a ~dev version saying vseal; the test image vseal_spu_test.h). Included by
 * spe.c. Its pages are kernel RAM, which the VRAM swap's bounds accept (its RAM window and predicate); without
 * the swap's bounds (no backing partition yet) the hook sets its own: one RAM window over its pages, with a
 * predicate that accepts only them. The hook's commands run under spe->lock, between the swap's jobs.
 *
 * debugfs dri/ps3rsx/ps3rsx_vseal (root, 0600). Write one command, read the report:
 *   golden            the known-answer job
 *   echo N            N round trips (no pages), polled
 *   seal N            N pages (1-32) of the hook's own kernel pages: sealed, opened back and compared, then
 *                     opened again with page 0's tag changed (refused, its destination untouched); the
 *                     pages are RAM to RAM (allowed only here), never VRAM
 *   slow N TICKS      a seal of N pages that waits TICKS decrementer ticks (79.8 MHz) before each page: it
 *                     must outlast its budget and finish on progress, without a restart
 *   hang              a job that never answers nor progresses: it must be restarted within its budget, the
 *                     program reloaded and its known answer checked again
 *   noirq             a job that finishes without the interrupt, waited for by interrupt: it must end on
 *                     the done word at the budget, not as a timeout
 *   diff N            N pages (1-32) through the program's own differential job: the four-lane Poly1305
 *                     against the scalar one (half the keys with r at its clamp maximum, one page all 0xff)
 *                     and the four-block ChaCha20 against the scalar one, on this SPE
 *   badaddr           one DMA get from an address with no translation (the guard page after the hook's
 *                     own vmalloc page): the MFC faults (class 1), the SPE must be restarted at once (not
 *                     at the budget) and work again
 *   stale             spare words into the SPU's inbound mailbox: a finished job's word again, a word for
 *                     a job number not in the ring, and one left there before a restart: each dropped, no
 *                     answer, no descriptor written; the next jobs work
 *   reload            a reset and a fresh load without the known-answer job: for 2 ms after the load the
 *                     program must sit in its first mailbox read (no answer, no descriptor or status line
 *                     written, SPU_Status running), then the known answer
 *   stats             the counters
 * The hook's pages (3 x 32 and the vmalloc page) are allocated with the ring and kept like it: freed at
 * unload only if the SPE was seen quiet.
 */
#include <linux/sort.h>

static void say(struct ps3rsx_spe *spe, const char *fmt, ...) __printf(2, 3);
static void say(struct ps3rsx_spe *spe, const char *fmt, ...)
{
	va_list ap;

	if (spe->len >= 4095)
		return;
	va_start(ap, fmt);
	spe->len += vscnprintf(spe->report + spe->len, 4096 - spe->len, fmt, ap);
	va_end(ap);
}

static u8 *tpage(struct ps3rsx_spe *spe, unsigned int set, unsigned int i)
{
	return spe->tpages + (set * VSEAL_PAGES_MAX + i) * VSEAL_PAGE;
}

static void stats_line(struct ps3rsx_spe *spe)
{
	struct spe_ctl *c = &spe->ctl;

	say(spe, "state %s gen %u jobs %llu polled %llu interrupt %llu extended %llu restarts %llu (no-progress in a row %u) errors %llu stale %llu spurious %llu early-wakes %llu keep %d progress %llu\n",
	    c->state == SPE_READY ? "ready" : c->state == SPE_GONE ? "GONE" : "off", c->gen, c->jobs, c->polled,
	    c->interrupted, c->extended, c->restarts, c->health.noprogress_restarts, c->errors, c->stale,
	    c->spurious, c->early_wakes, c->keep_pages, READ_ONCE(c->status->progress));
}

static struct vseal_job *tjob(struct ps3rsx_spe *spe, unsigned int op, unsigned int n, unsigned int *idx)
{
	struct vseal_job *j;

	*idx = spe->ctl.n % VSEAL_JOBS;
	j = &spe->ctl.ring[*idx];
	memset(j, 0, sizeof(*j));
	j->op = op;
	j->npages = n;
	return j;
}

static int cmp_s64(const void *a, const void *b)
{
	s64 x = *(const s64 *)a, y = *(const s64 *)b;

	return x < y ? -1 : x > y;
}

static void t_echo(struct ps3rsx_spe *spe, unsigned int n)
{
	static s64 t[1000];
	unsigned int i, idx;
	int ret;

	n = clamp(n, 1u, 1000u);
	for (i = 0; i < n; i++) {
		ktime_t t0 = ktime_get();

		tjob(spe, VSEAL_OP_ECHO, 0, &idx);
		ret = spe_run_job(&spe->ctl, idx, SPE_PATH_RAM);
		t[i] = ktime_to_ns(ktime_sub(ktime_get(), t0));
		if (ret) {
			say(spe, "echo %u: %d\n", i, ret);
			return;
		}
	}
	sort(t, n, sizeof(t[0]), cmp_s64, NULL);
	say(spe, "echo x%u: median %lld ns, p99 %lld ns, max %lld ns\n", n, t[n / 2], t[n * 99 / 100], t[n - 1]);
}

static void t_seal(struct ps3rsx_spe *spe, unsigned int n)
{
	static struct vseal_page saved[VSEAL_PAGES_MAX];	/* (spe->lock held) */
	static struct vseal_result tags[VSEAL_PAGES_MAX];
	struct vseal_job *j;
	unsigned int i, idx, bad = 0;
	ktime_t t0;
	s64 ts, to;
	int ret;

	n = clamp(n, 1u, (unsigned int)VSEAL_PAGES_MAX);
	j = tjob(spe, VSEAL_OP_SEAL, n, &idx);
	for (i = 0; i < n; i++) {
		get_random_bytes(tpage(spe, 0, i), VSEAL_PAGE);
		j->page[i].src = (unsigned long)tpage(spe, 0, i);
		j->page[i].dst = (unsigned long)tpage(spe, 1, i);
		get_random_bytes(&j->page[i].ctr, sizeof(j->page[i].ctr));
		get_random_bytes(j->page[i].ad, 9);
	}
	t0 = ktime_get();
	ret = spe_run_job(&spe->ctl, idx, SPE_PATH_RAM);
	ts = ktime_to_ns(ktime_sub(ktime_get(), t0));
	if (ret) {
		say(spe, "seal: %d (status %u)\n", ret, j->status);
		return;
	}
	memcpy(saved, j->page, n * sizeof(saved[0]));
	memcpy(tags, j->res, n * sizeof(tags[0]));
	for (i = 0; i < n; i++)
		bad += !memcmp(tpage(spe, 0, i), tpage(spe, 1, i), VSEAL_PAGE);	/* ciphertext = plaintext? */
	j = tjob(spe, VSEAL_OP_OPEN, n, &idx);
	for (i = 0; i < n; i++) {
		j->page[i] = saved[i];
		memcpy(j->page[i].tag, tags[i].tag, 16);
		j->page[i].src = (unsigned long)tpage(spe, 1, i);
		j->page[i].dst = (unsigned long)tpage(spe, 2, i);
		memset(tpage(spe, 2, i), 0xa5, VSEAL_PAGE);
	}
	t0 = ktime_get();
	ret = spe_run_job(&spe->ctl, idx, SPE_PATH_RAM);
	to = ktime_to_ns(ktime_sub(ktime_get(), t0));
	for (i = 0; i < n; i++)
		bad += j->res[i].status != VSEAL_ST_OK || memcmp(tpage(spe, 0, i), tpage(spe, 2, i), VSEAL_PAGE);
	say(spe, "seal x%u: %lld ns (%lld a page, %s), open %lld ns (%lld a page), %u pages wrong: %s\n", n, ts,
	    ts / n, spe_poll(VSEAL_OP_SEAL, n, SPE_PATH_RAM) ? "polled" : "interrupt", to, to / n, bad,
	    ret || bad ? "FAIL" : "ok");
	/* page 0's tag changed: refused, untouched; the rest open */
	j = tjob(spe, VSEAL_OP_OPEN, n, &idx);
	for (i = 0; i < n; i++) {
		j->page[i] = saved[i];
		memcpy(j->page[i].tag, tags[i].tag, 16);
		j->page[i].src = (unsigned long)tpage(spe, 1, i);
		j->page[i].dst = (unsigned long)tpage(spe, 2, i);
		memset(tpage(spe, 2, i), 0xa5, VSEAL_PAGE);
	}
	j->page[0].tag[3] ^= 0x10;
	ret = spe_run_job(&spe->ctl, idx, SPE_PATH_RAM);
	bad = ret != SPE_RET_BADMSG || j->res[0].status != VSEAL_ST_BADTAG ||
	      memchr_inv(tpage(spe, 2, 0), 0xa5, VSEAL_PAGE);
	for (i = 1; i < n; i++)
		bad += j->res[i].status != VSEAL_ST_OK || memcmp(tpage(spe, 0, i), tpage(spe, 2, i), VSEAL_PAGE);
	say(spe, "changed tag: %d, page 0 %s, %u wrong: %s\n", ret,
	    j->res[0].status == VSEAL_ST_BADTAG ? "refused" : "ACCEPTED", bad, bad ? "FAIL" : "ok");
}

static void t_diff(struct ps3rsx_spe *spe, unsigned int n)
{
	struct vseal_job *j;
	unsigned int i, idx, bad = 0;
	int ret;

	n = clamp(n, 1u, (unsigned int)VSEAL_PAGES_MAX);
	j = tjob(spe, VSEAL_OP_TEST_DIFF, n, &idx);
	for (i = 0; i < n; i++) {
		get_random_bytes(tpage(spe, 0, i), VSEAL_PAGE);
		if (i == 1)
			memset(tpage(spe, 0, i), 0xff, VSEAL_PAGE);
		j->page[i].src = (unsigned long)tpage(spe, 0, i);
		j->page[i].dst = (unsigned long)tpage(spe, 1, i);
		get_random_bytes(&j->page[i].ctr, sizeof(j->page[i].ctr));
		get_random_bytes(j->page[i].ad, 16);
		get_random_bytes(j->page[i].tag, 16);
		if (i % 2)
			memset(j->page[i].ad, 0xff, 16);	/* r at its clamp maximum */
	}
	ret = spe_run_job(&spe->ctl, idx, SPE_PATH_RAM);
	for (i = 0; i < n; i++)
		bad += j->res[i].status != VSEAL_ST_OK;
	say(spe, "diff x%u: %d, %u pages where vector != scalar: %s\n", n, ret, bad, ret || bad ? "FAIL" : "ok");
}

static void t_slow(struct ps3rsx_spe *spe, unsigned int n, unsigned int ticks)
{
	struct spe_ctl *c = &spe->ctl;
	u64 r0 = c->restarts, e0 = c->extended;
	struct vseal_job *j;
	unsigned int i, idx;
	ktime_t t0;
	int ret;

	n = clamp(n, 1u, (unsigned int)VSEAL_PAGES_MAX);
	j = tjob(spe, VSEAL_OP_TEST_SLOW, n, &idx);
	j->arg = ticks;
	for (i = 0; i < n; i++) {
		j->page[i].src = (unsigned long)tpage(spe, 0, i);
		j->page[i].dst = (unsigned long)tpage(spe, 1, i);
	}
	t0 = ktime_get();
	ret = spe_run_job(c, idx, SPE_PATH_RAM);
	say(spe, "slow x%u (%u ticks a page): %d after %lld us, budget %u us, extended %llu, restarts %llu: %s\n", n,
	    ticks, ret, ktime_to_us(ktime_sub(ktime_get(), t0)), spe_budget_us(VSEAL_OP_SEAL, n, SPE_PATH_RAM),
	    c->extended - e0, c->restarts - r0, !ret && c->restarts == r0 ? "ok" : "FAIL");
}

/* a job that must end in a restart: hang (at the budget) or badaddr (at once, on the fault) */
static void t_restart(struct ps3rsx_spe *spe, unsigned int op, const char *name)
{
	struct spe_ctl *c = &spe->ctl;
	u64 r0 = c->restarts, e0 = c->errors;
	struct vseal_job *j;
	unsigned int idx;
	ktime_t t0;
	s64 us;
	int ret;

	j = tjob(spe, op, op == VSEAL_OP_TEST_BADADDR, &idx);
	if (op == VSEAL_OP_TEST_BADADDR)
		j->page[0].src = (unsigned long)spe->guard + PAGE_SIZE;	/* vmalloc's guard page: no translation */
	t0 = ktime_get();
	ret = spe_run_job(c, idx, SPE_PATH_RAM);
	us = ktime_to_us(ktime_sub(ktime_get(), t0));
	say(spe, "%s: %d after %lld us, restarts +%llu, errors +%llu, state %s: %s\n", name, ret, us,
	    c->restarts - r0, c->errors - e0, c->state == SPE_READY ? "ready (reloaded, known answer checked)" :
	    c->keep_pages ? "GONE, pages kept" : "GONE",
	    ret == SPE_RET_TIMEDOUT && c->state == SPE_READY &&
	    (op != VSEAL_OP_TEST_BADADDR || (c->errors > e0 && us < SPE_BUDGET_FLOOR_US)) ? "ok" : "FAIL");
}

static void t_noirq(struct ps3rsx_spe *spe)
{
	struct spe_ctl *c = &spe->ctl;
	unsigned int idx;
	u64 r0 = c->restarts;
	ktime_t t0;
	int ret;

	tjob(spe, VSEAL_OP_TEST_NOIRQ, 0, &idx);
	t0 = ktime_get();
	ret = spe_run_job(c, idx, SPE_PATH_VRAM);	/* (the VRAM path: waited for by interrupt) */
	say(spe, "noirq: %d after %lld us, restarts +%llu: %s\n", ret, ktime_to_us(ktime_sub(ktime_get(), t0)),
	    c->restarts - r0, !ret && c->restarts == r0 ? "ok" : "FAIL");
}

/* a spare word into the inbound mailbox; after a while: no answer, the descriptor's results untouched */
static int spare_word(struct spe_ctl *c, u32 word, unsigned int idx)
{
	struct vseal_job *j = &c->ring[idx];
	u64 done = READ_ONCE(j->done);

	if (!spe_hw_inbox_free(c->hw))
		return 0;
	spe_hw_inbox_write(c->hw, word);
	usleep_range(1000, 1500);
	return !spe_hw_ibox_count(c->hw) && READ_ONCE(j->done) == done;
}

static void t_stale(struct ps3rsx_spe *spe)
{
	struct spe_ctl *c = &spe->ctl;
	struct vseal_job *j;
	unsigned int idx;
	int a, b, d, e;

	tjob(spe, VSEAL_OP_ECHO, 0, &idx);
	a = !spe_run_job(c, idx, SPE_PATH_RAM);
	a = a && spare_word(c, vseal_word(c->ring[idx].seq, idx), idx);	/* the finished job's word again */
	/* a valid descriptor in the slot, then a word for another job number: dropped by the word check */
	j = tjob(spe, VSEAL_OP_ECHO, 0, &idx);
	j->magic = VSEAL_MAGIC;
	j->seq = spe_seq(c->gen, c->n + 3);
	mb();
	b = spare_word(c, vseal_word(spe_seq(c->gen, c->n + 7), idx), idx);
	j->magic = 0;
	/* words left in the inbound mailbox across a restart: a hang started by hand (so the SPU stops reading
	 * its mailbox), a spare word behind it, then an echo, which times out; the reloaded program finds the
	 * spare word and the echo's (if the reset kept them) and must drop both */
	j = tjob(spe, VSEAL_OP_TEST_HANG, 0, &idx);
	j->magic = VSEAL_MAGIC;
	j->seq = spe_seq(c->gen, ++c->n);
	mb();
	spe_hw_inbox_write(c->hw, vseal_word(j->seq, idx));
	usleep_range(100, 200);
	spe_hw_inbox_write(c->hw, vseal_word(spe_seq(c->gen, c->n + 2), (idx + 2) % VSEAL_JOBS));
	tjob(spe, VSEAL_OP_ECHO, 0, &idx);
	d = spe_run_job(c, idx, SPE_PATH_RAM) == SPE_RET_TIMEDOUT && c->state == SPE_READY;
	tjob(spe, VSEAL_OP_ECHO, 0, &idx);
	e = d && !spe_run_job(c, idx, SPE_PATH_RAM) && !spe_golden(c);
	say(spe, "stale: replayed word %s, unknown job number %s, word across a restart %s, after it %s: %s\n",
	    a ? "dropped" : "ANSWERED", b ? "dropped" : "ANSWERED", d ? "restarted" : "FAILED", e ? "ok" : "BROKEN",
	    a && b && d && e ? "ok" : "FAIL");
}

static void t_reload(struct ps3rsx_spe *spe)
{
	struct spe_ctl *c = &spe->ctl;
	unsigned int i;
	int ret, quiet, g;
	u64 done = 0;

	ret = spe_hw_reset(c->hw);
	if (ret) {
		c->state = SPE_GONE;
		c->keep_pages = 1;
		spe_hw_give_up(c->hw, 0);
		say(spe, "reload: the reset failed (%d): given up, pages kept: FAIL\n", ret);
		return;
	}
	memset(c->ring, 0, VSEAL_JOBS * VSEAL_PAGE);
	memset(c->status, 0, sizeof(*c->status));
	mb();
	c->gen++;
	c->n = 0;
	ret = spe_hw_load(c->hw);
	if (ret) {
		c->state = SPE_GONE;
		spe_hw_give_up(c->hw, 1);
		say(spe, "reload: the load failed (%d): FAIL\n", ret);
		return;
	}
	usleep_range(2000, 2500);
	for (i = 0; i < VSEAL_JOBS; i++)
		done |= READ_ONCE(c->ring[i].done);
	quiet = !spe_hw_ibox_count(c->hw) && !done && !READ_ONCE(c->status->progress) &&
		!READ_ONCE(c->status->last_seq) && !spe_hw_trouble(c->hw);
	c->state = SPE_READY;
	g = spe_golden(c);
	say(spe, "reload: after the load %s, known answer %s: %s\n", quiet ? "waiting in its mailbox read" :
	    "NOT WAITING (an answer, a write or a stop)", g ? "WRONG" : "ok", quiet && !g ? "ok" : "FAIL");
}

static ssize_t vseal_write(struct file *f, const char __user *buf, size_t len, loff_t *pos)
{
	struct ps3rsx_spe *spe = f->private_data;
	char cmd[48] = {};
	unsigned int a = 0, b = 0;

	if (!len || len >= sizeof(cmd) || copy_from_user(cmd, buf, len))
		return -EINVAL;
	if (mutex_lock_killable(&spe->lock))
		return -EINTR;
	spe->len = 0;
	spe->report[0] = 0;
	spe->ctl.test = 1;		/* the test ops and RAM-to-RAM seals: only for the hook's own jobs */
	if (spe->ctl.state != SPE_READY && strncmp(cmd, "stats", 5))
		say(spe, "the SPE isn't ready\n");
	else if (!strncmp(cmd, "golden", 6))
		say(spe, "golden: %s\n", spe_golden(&spe->ctl) ? "FAIL" : "ok");
	else if (sscanf(cmd, "echo %u", &a) == 1)
		t_echo(spe, a);
	else if (sscanf(cmd, "seal %u", &a) == 1)
		t_seal(spe, a);
	else if (sscanf(cmd, "diff %u", &a) == 1)
		t_diff(spe, a);
	else if (sscanf(cmd, "slow %u %u", &a, &b) == 2)
		t_slow(spe, a, b);
	else if (!strncmp(cmd, "hang", 4))
		t_restart(spe, VSEAL_OP_TEST_HANG, "hang");
	else if (!strncmp(cmd, "badaddr", 7))
		t_restart(spe, VSEAL_OP_TEST_BADADDR, "badaddr");
	else if (!strncmp(cmd, "noirq", 5))
		t_noirq(spe);
	else if (!strncmp(cmd, "stale", 5))
		t_stale(spe);
	else if (!strncmp(cmd, "reload", 6))
		t_reload(spe);
	else if (strncmp(cmd, "stats", 5))
		say(spe, "unknown command\n");
	spe->ctl.test = 0;
	stats_line(spe);
	mutex_unlock(&spe->lock);
	return len;
}

static ssize_t vseal_read(struct file *f, char __user *buf, size_t len, loff_t *pos)
{
	struct ps3rsx_spe *spe = f->private_data;
	ssize_t ret;

	if (mutex_lock_killable(&spe->lock))
		return -EINTR;
	ret = simple_read_from_buffer(buf, len, pos, spe->report, spe->len);
	mutex_unlock(&spe->lock);
	return ret;
}

static const struct file_operations vseal_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = vseal_write,
	.read = vseal_read,
	.llseek = default_llseek,
};

static int tpages_ok(void *arg, vs_u64 ea, unsigned int kind)
{
	struct ps3rsx_spe *spe = arg;

	return kind == SPE_WIN_RAM && ea >= (unsigned long)spe->tpages &&
	       ea - (unsigned long)spe->tpages < 3 * VSEAL_PAGES_MAX * VSEAL_PAGE;
}

static void spe_test_init(struct ps3rsx_spe *spe)
{
	struct spe_bounds *b = &spe->ctl.bounds;

	mutex_lock(&spe->lock);
	if (!b->nwin) {		/* the swap's bounds not set yet: the hook's own */
		b->win[0] = (struct spe_window){ (unsigned long)spe->tpages, 3 * VSEAL_PAGES_MAX * VSEAL_PAGE,
						 SPE_WIN_RAM };
		b->nwin = 1;
		b->page_ok = tpages_ok;
		b->page_ok_arg = spe;
	}
	mutex_unlock(&spe->lock);
	spe->file = debugfs_create_file("ps3rsx_vseal", 0600, spe->rsx->drm.primary->debugfs_root, spe,
					&vseal_fops);
	drm_info(&spe->rsx->drm, "TEST BUILD: vseal hook ready (debugfs ps3rsx_vseal)\n");
}
