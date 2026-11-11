/* SPDX-License-Identifier: GPL-2.0 */
/*
 * spe_test.h: the SPE client's debugfs hook, test builds only (PS3RSX_VSEAL_TEST:
 * DEB_BUILD_OPTIONS=ps3rsx-vseal, a ~dev version saying vseal). Included by spe.c.
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
 *   stats             the counters
 * The hook's pages (3 x 32) are allocated with the ring and kept like it: freed at unload only if the
 * SPE was seen quiet.
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
	say(spe, "state %s gen %u jobs %llu polled %llu interrupt %llu extended %llu restarts %llu (no-progress in a row %u) errors %llu stale irqs %llu progress %llu\n",
	    spe->state == SPE_READY ? "ready" : spe->state == SPE_GONE ? "GONE" : "off", spe->gen, spe->jobs,
	    spe->polled, spe->interrupted, spe->extended, spe->restarts, spe->health.noprogress_restarts,
	    spe->errors, spe->stale_irqs, READ_ONCE(spe->status->progress));
}

static struct vseal_job *tjob(struct ps3rsx_spe *spe, unsigned int op, unsigned int n, unsigned int *idx)
{
	struct vseal_job *j;

	*idx = spe->n % VSEAL_JOBS;
	j = &spe->ring[*idx];
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
		ret = run_job(spe, idx, SPE_PATH_RAM);
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
	struct vseal_job *j;
	static struct vseal_page saved[VSEAL_PAGES_MAX];	/* (spe->lock held) */
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
	ret = run_job(spe, idx, SPE_PATH_RAM);
	ts = ktime_to_ns(ktime_sub(ktime_get(), t0));
	if (ret) {
		say(spe, "seal: %d (status %u)\n", ret, j->status);
		return;
	}
	memcpy(saved, j->page, n * sizeof(saved[0]));
	for (i = 0; i < n; i++)
		bad += !memcmp(tpage(spe, 0, i), tpage(spe, 1, i), VSEAL_PAGE);	/* ciphertext = plaintext? */
	j = tjob(spe, VSEAL_OP_OPEN, n, &idx);
	for (i = 0; i < n; i++) {
		j->page[i] = saved[i];
		j->page[i].src = (unsigned long)tpage(spe, 1, i);
		j->page[i].dst = (unsigned long)tpage(spe, 2, i);
		j->page[i].status = 0;
		memset(tpage(spe, 2, i), 0xa5, VSEAL_PAGE);
	}
	t0 = ktime_get();
	ret = run_job(spe, idx, SPE_PATH_RAM);
	to = ktime_to_ns(ktime_sub(ktime_get(), t0));
	for (i = 0; i < n; i++)
		bad += j->page[i].status != VSEAL_ST_OK || memcmp(tpage(spe, 0, i), tpage(spe, 2, i), VSEAL_PAGE);
	say(spe, "seal x%u: %lld ns (%lld a page, %s), open %lld ns (%lld a page), %u pages wrong: %s\n", n, ts,
	    ts / n, spe_poll(VSEAL_OP_SEAL, n, SPE_PATH_RAM) ? "polled" : "interrupt", to, to / n, bad,
	    ret || bad ? "FAIL" : "ok");
	/* page 0's tag changed: refused, untouched; the rest open */
	j = tjob(spe, VSEAL_OP_OPEN, n, &idx);
	for (i = 0; i < n; i++) {
		j->page[i] = saved[i];
		j->page[i].src = (unsigned long)tpage(spe, 1, i);
		j->page[i].dst = (unsigned long)tpage(spe, 2, i);
		j->page[i].status = 0;
		memset(tpage(spe, 2, i), 0xa5, VSEAL_PAGE);
	}
	j->page[0].tag[3] ^= 0x10;
	ret = run_job(spe, idx, SPE_PATH_RAM);
	bad = j->page[0].status != VSEAL_ST_BADTAG || memchr_inv(tpage(spe, 2, 0), 0xa5, VSEAL_PAGE);
	for (i = 1; i < n; i++)
		bad += j->page[i].status != VSEAL_ST_OK || memcmp(tpage(spe, 0, i), tpage(spe, 2, i), VSEAL_PAGE);
	say(spe, "changed tag: page 0 %s, %u wrong: %s\n",
	    j->page[0].status == VSEAL_ST_BADTAG ? "refused" : "ACCEPTED", bad, bad ? "FAIL" : "ok");
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
	ret = run_job(spe, idx, SPE_PATH_RAM);
	for (i = 0; i < n; i++)
		bad += j->page[i].status != VSEAL_ST_OK;
	say(spe, "diff x%u: %d, %u pages where vector != scalar: %s\n", n, ret, bad, ret || bad ? "FAIL" : "ok");
}

static void t_slow(struct ps3rsx_spe *spe, unsigned int n, unsigned int ticks)
{
	u64 r0 = spe->restarts, e0 = spe->extended;
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
	ret = run_job(spe, idx, SPE_PATH_RAM);
	say(spe, "slow x%u (%u ticks a page): %d after %lld us, budget %u us, extended %llu, restarts %llu: %s\n", n,
	    ticks, ret, ktime_to_us(ktime_sub(ktime_get(), t0)), spe_budget_us(VSEAL_OP_SEAL, n, SPE_PATH_RAM),
	    spe->extended - e0, spe->restarts - r0, !ret && spe->restarts == r0 ? "ok" : "FAIL");
}

static void t_hang(struct ps3rsx_spe *spe)
{
	u64 r0 = spe->restarts;
	unsigned int idx;
	ktime_t t0;
	int ret;

	tjob(spe, VSEAL_OP_TEST_HANG, 0, &idx);
	t0 = ktime_get();
	ret = run_job(spe, idx, SPE_PATH_RAM);
	say(spe, "hang: %d after %lld us, restarts +%llu, state %s: %s\n", ret, ktime_to_us(ktime_sub(ktime_get(), t0)),
	    spe->restarts - r0, spe->state == SPE_READY ? "ready (reloaded, known answer checked)" : "GONE",
	    ret == -ETIMEDOUT && spe->state == SPE_READY ? "ok" : "FAIL");
}

static void t_noirq(struct ps3rsx_spe *spe)
{
	unsigned int idx;
	u64 r0 = spe->restarts;
	ktime_t t0;
	int ret;

	tjob(spe, VSEAL_OP_TEST_NOIRQ, 0, &idx);
	t0 = ktime_get();
	ret = run_job(spe, idx, SPE_PATH_VRAM);	/* (the VRAM path: waited for by interrupt) */
	say(spe, "noirq: %d after %lld us, restarts +%llu: %s\n", ret, ktime_to_us(ktime_sub(ktime_get(), t0)),
	    spe->restarts - r0, !ret && spe->restarts == r0 ? "ok" : "FAIL");
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
	if (spe->state != SPE_READY && strncmp(cmd, "stats", 5))
		say(spe, "the SPE isn't ready\n");
	else if (!strncmp(cmd, "golden", 6))
		say(spe, "golden: %s\n", spe_golden(spe) ? "FAIL" : "ok");
	else if (sscanf(cmd, "echo %u", &a) == 1)
		t_echo(spe, a);
	else if (sscanf(cmd, "seal %u", &a) == 1)
		t_seal(spe, a);
	else if (sscanf(cmd, "diff %u", &a) == 1)
		t_diff(spe, a);
	else if (sscanf(cmd, "slow %u %u", &a, &b) == 2)
		t_slow(spe, a, b);
	else if (!strncmp(cmd, "hang", 4))
		t_hang(spe);
	else if (!strncmp(cmd, "noirq", 5))
		t_noirq(spe);
	else if (strncmp(cmd, "stats", 5))
		say(spe, "unknown command\n");
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

static void spe_test_init(struct ps3rsx_spe *spe)
{
	struct spe_window w = { (unsigned long)spe->tpages, 3 * VSEAL_PAGES_MAX * VSEAL_PAGE, SPE_WIN_RAM };

	mutex_lock(&spe->lock);
	spe->win[0] = w;
	spe->nwin = 1;
	mutex_unlock(&spe->lock);
	spe->file = debugfs_create_file("ps3rsx_vseal", 0600, spe->rsx->drm.primary->debugfs_root, spe,
					&vseal_fops);
	drm_info(&spe->rsx->drm, "TEST BUILD: vseal hook ready (debugfs ps3rsx_vseal)\n");
}
