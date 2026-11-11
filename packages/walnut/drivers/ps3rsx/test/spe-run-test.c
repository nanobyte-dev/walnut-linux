/* spe-run-test: the SPE client's running code (src/spe_run.c: the job, the wait, the drain, the restart, the
 * interrupt handlers' bodies) on the PC, against a fake SPE that runs the real SPU program (spu/vseal.c with
 * its test ops, on vseal-sim.h's MFC model) on a fake clock. The fake has the hardware's shape: a 4-entry
 * inbound mailbox the program reads one word at a time, a 1-entry outbound interrupt mailbox the program
 * blocks on while it is full, a class-2 mailbox status that is level-triggered (or edge-triggered, for the
 * finish-before-arm case), masked by spu_base's handler before it calls the callback; resets that can fail
 * or keep the inbound mailbox; class 0, 1 and 2 calls at chosen times.
 * Built with SPE_FIXOFF_GOLDEN_COUNTS, SPE_FIXOFF_RESET_FAIL_NODEV, SPE_FIXOFF_WAKE_IS_TIMEOUT,
 * SPE_FIXOFF_KEEP_RING or SPE_FIXOFF_RELEASED_IS_FAILURE it must fail. */
#define VSEAL_HOST
#define VSEAL_TEST_OPS
#include "../spu/vseal.c"
#include "../src/spe_run.c"
#include <errno.h>

static int fails, checks;

static void dump(void);
static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		printf("FAIL: %s\n", what);
		dump();
		fails++;
	}
}

/* everything the SPE may be pointed at, in one RAM window */
static struct {
	uint8_t ring[VSEAL_JOBS][VSEAL_PAGE];
	uint8_t status[VSEAL_PAGE];
	uint8_t pg[8][VSEAL_PAGE];
	uint8_t pg2[8][VSEAL_PAGE];
} A __attribute__((aligned(4096)));

static struct spe_ctl C;

static struct fake {
	vs_u64 now;
	vs_u32 inbox[4];
	int nin;
	vs_u32 ibox, ibox_next;
	int ibox_full, ibox_blocked;	/* blocked: the program waits to write ibox_next */
	int masked, stat, edge, in_irq;
	int busy, hung;
	vs_u64 busy_until, start;
	vs_u32 word;
	unsigned int op;
	int wakes;
	int trouble;
	int reset_ret, keep_inbox, finish_on_arm, golden_wrong;
	vs_u64 lat;
	long stop2_after, c0_after, c0err_after, stale_err_after, fault_at;
	vs_u32 stale_err_word;
	int resets, loads, gave_up, gave_up_quiet;
	vs_u64 next_tick;
} F;

static void fake_init(void)
{
	memset(&F, 0, sizeof(F));
	F.masked = 1;
	F.lat = 5;
	F.stop2_after = F.c0_after = F.c0err_after = F.stale_err_after = F.fault_at = -1;
	memset(&A, 0, sizeof(A));
	memset(&C, 0, sizeof(C));
	C.ring = (struct vseal_job *)A.ring;
	C.status = (struct vseal_status *)A.status;
	C.hw = &F;
	C.test = 1;
	C.bounds.win[0] = (struct spe_window){ (uintptr_t)&A, sizeof(A), SPE_WIN_RAM };
	C.bounds.nwin = 1;
	C.bounds.refused[0] = (struct spe_window){ (uintptr_t)A.ring, sizeof(A.ring), 0 };
	C.bounds.refused[1] = (struct spe_window){ (uintptr_t)A.status, sizeof(A.status), 0 };
	C.bounds.nrefused = 2;
}

static void deliver(void)
{
	if (F.in_irq)
		return;
	for (;;) {
		if (!F.edge && F.ibox_full)
			F.stat = 1;		/* level-triggered */
		if (F.masked || !F.stat)
			return;
		F.masked = 1;			/* spu_base masks the mailbox class, acks, then calls back */
		F.stat = 0;
		F.in_irq = 1;
		spe_irq_ibox(&C);
		F.in_irq = 0;
	}
}

static void ibox_push(vs_u32 v)
{
	if (F.ibox_full) {
		F.ibox_next = v;
		F.ibox_blocked = 1;
		return;
	}
	F.ibox = v;
	F.ibox_full = 1;
	F.stat = 1;
}

static int matches(vs_u32 word)
{
	struct vseal_job *j = &C.ring[word & 3];

	return j->magic == VSEAL_MAGIC && vseal_word(j->seq, word & 3) == word;
}

static void finish(void)
{
	F.busy = 0;
	vs_irq_count = 0;
	vseal_job(F.word);
	vs_drain();
	if (F.golden_wrong && F.op == VSEAL_OP_GOLDEN)
		C.ring[F.word & 3].res[0].tag[0] ^= 1;
	if (vs_irq_count)
		ibox_push(vs_irq_value);
}

/* the SPU takes its next word if it is free */
static void spu_next(void)
{
	while (!F.busy && !F.hung && !F.ibox_blocked && F.nin) {
		struct vseal_job *j;
		vs_u32 w = F.inbox[0];

		memmove(F.inbox, F.inbox + 1, --F.nin * sizeof(F.inbox[0]));
		j = &C.ring[w & 3];
		F.word = w;
		F.start = F.now;
		F.op = j->op;
		if (!matches(w)) {
			vseal_job(w);		/* dropped by the program itself */
			vs_drain();
			continue;
		}
		if (F.op == VSEAL_OP_TEST_HANG) {
			F.hung = 1;
		} else if (F.op == VSEAL_OP_TEST_BADADDR) {
			F.hung = 1;
			F.fault_at = F.now + 2;
		} else {
			F.busy = 1;
			F.busy_until = F.now + (F.op == VSEAL_OP_TEST_SLOW ? (vs_u64)j->npages * j->arg / 80 : F.lat);
			F.next_tick = F.now + 1000;
		}
	}
}

/* rel: an offset from the current job's start, or -1; fires once */
static int due(long *rel)
{
	if (*rel < 0 || !(F.busy || F.hung) || F.now != F.start + (vs_u64)*rel)
		return 0;
	*rel = -1;
	return 1;
}

static void fake_step(vs_u64 to, int stop_on_wake)
{
	while (F.now < to && !(stop_on_wake && F.wakes)) {
		F.now++;
		spu_next();
		if (F.busy && F.op == VSEAL_OP_TEST_SLOW && F.now >= F.next_tick) {
			C.status->progress++;	/* the program's per-page status puts */
			F.next_tick += 1000;
		}
		if (F.busy && F.now >= F.busy_until)
			finish();
		if (F.fault_at >= 0 && F.now == (vs_u64)F.fault_at) {
			F.fault_at = -1;
			spe_irq_stop(&C, 1, 0x40000000);	/* storage fault, PTE not found */
		}
		if (due(&F.stop2_after))
			spe_irq_stop(&C, 2, 0);		/* a stop call though the SPU runs */
		if (due(&F.c0_after))
			spe_irq_stop(&C, 0, 0);		/* class 0 with nothing pending */
		if (due(&F.c0err_after)) {
			F.trouble = 1;
			spe_irq_stop(&C, 0, 1);		/* class 0 with a pending error */
		}
		if (due(&F.stale_err_after)) {		/* a handler that read an older job's word */
			C.err_hw = 1;
			C.err_word = F.stale_err_word;
			spe_hw_wake(&F);
		}
		deliver();
		spu_next();
	}
}

static void fake_advance(vs_u64 to)
{
	fake_step(to, 0);
}

/* --- spe_run.h's hardware calls ----------------------------------------------------------------- */
vs_u64 spe_hw_now_us(void *hw) { return F.now; }
void spe_hw_mb(void *hw) {}
void spe_hw_rmb(void *hw) {}
void spe_hw_relax(void *hw) { fake_advance(F.now + 1); }
unsigned int spe_hw_inbox_free(void *hw) { return 4 - F.nin; }
void spe_hw_inbox_write(void *hw, vs_u32 word)
{
	if (F.nin < 4)
		F.inbox[F.nin++] = word;
}
unsigned int spe_hw_ibox_count(void *hw) { return F.ibox_full; }
vs_u32 spe_hw_ibox_pop(void *hw)
{
	vs_u32 v = F.ibox;

	F.ibox_full = 0;
	if (F.ibox_blocked) {
		F.ibox_blocked = 0;
		ibox_push(F.ibox_next);
	}
	return v;
}
void spe_hw_ibox_arm(void *hw)
{
	if (F.finish_on_arm && F.busy) {
		F.finish_on_arm = 0;
		finish();			/* the job ends just before the arm's clear */
	}
	F.stat = 0;
	F.masked = 0;
	deliver();
}
void spe_hw_ibox_rearm_irq(void *hw) { F.masked = 0; }
void spe_hw_ibox_clear(void *hw) { F.stat = 0; }
void spe_hw_sleep_prepare(void *hw) { F.wakes = 0; }
int spe_hw_sleep(void *hw, vs_u64 us)
{
	fake_step(F.now + us, 1);
	if (F.wakes) {
		F.wakes--;
		return 1;
	}
	return 0;
}
void spe_hw_wake(void *hw) { F.wakes++; }
int spe_hw_trouble(void *hw) { return F.trouble; }
int spe_hw_reset(void *hw)
{
	F.resets++;
	if (F.reset_ret)
		return F.reset_ret;
	F.busy = F.hung = 0;
	F.ibox_full = F.ibox_blocked = 0;
	F.stat = 0;
	F.masked = 1;
	F.trouble = 0;
	F.fault_at = -1;
	if (!F.keep_inbox)
		F.nin = 0;
	return 0;
}
int spe_hw_load(void *hw)
{
	struct vseal_param *p = PARAM;

	F.loads++;
	memset(vs_ls, 0, sizeof(vs_ls));
	p->magic = VSEAL_MAGIC;
	p->abi = VSEAL_ABI;
	p->ring_ea = (uintptr_t)C.ring;
	p->status_ea = (uintptr_t)C.status;
	memset(p->key, 0x42, 32);
	progress = 0;
	return 0;
}
void spe_hw_give_up(void *hw, int quiet)
{
	F.gave_up++;
	F.gave_up_quiet = quiet;
}

/* --- jobs ---------------------------------------------------------------------------------------- */
static unsigned int slot(void)
{
	return C.n % VSEAL_JOBS;
}

static struct vseal_job *mk(unsigned int op, unsigned int n)
{
	struct vseal_job *j = &C.ring[slot()];
	unsigned int i;

	memset(j, 0, sizeof(*j));
	j->op = op;
	j->npages = n;
	for (i = 0; i < n; i++) {
		j->page[i].src = (uintptr_t)A.pg[i];
		j->page[i].dst = (uintptr_t)A.pg2[i];
	}
	return j;
}

static int run(unsigned int op, unsigned int n, enum spe_path path)
{
	mk(op, n);
	return spe_run_job(&C, slot(), path);
}

static void fresh(void)
{
	fake_init();
	check(spe_start(&C) == 0 && C.state == SPE_READY && F.loads == 1 && C.gen == 1, "start: loaded, known answer");
}

static void dump(void)
{
	printf("  now %llu state %d gen %u n %u jobs %llu polled %llu irq %llu ext %llu restarts %llu stale %llu errors %llu spurious %llu early %llu resets %d loads %d\n",
	       (unsigned long long)F.now, C.state, C.gen, C.n, (unsigned long long)C.jobs, (unsigned long long)C.polled,
	       (unsigned long long)C.interrupted, (unsigned long long)C.extended, (unsigned long long)C.restarts,
	       (unsigned long long)C.stale, (unsigned long long)C.errors, (unsigned long long)C.spurious,
	       (unsigned long long)C.early_wakes, F.resets, F.loads);
}

int main(void)
{
	vs_u64 t0;
	int ret, i;

	check(SPE_RET_KEEP == -EHWPOISON && SPE_RET_NODEV == -ENODEV && SPE_RET_TIMEDOUT == -ETIMEDOUT &&
	      SPE_RET_INVAL == -EINVAL && SPE_RET_IO == -EIO && SPE_RET_BADMSG == -EBADMSG, "the return codes are errnos");

	/* polled and interrupt-waited jobs */
	fresh();
	for (i = 0; i < 8; i++)
		memset(A.pg[i], i, VSEAL_PAGE);
	ret = run(VSEAL_OP_SEAL, 1, SPE_PATH_RAM);
	check(ret == 0 && C.polled == 2 && memcmp(A.pg[0], A.pg2[0], VSEAL_PAGE), "a one-page seal is polled");
	check(!F.ibox_full && !F.stat && !F.ibox_blocked, "after a polled job: no mailbox entry, no latched status");
	F.lat = 300;
	t0 = F.now;
	ret = run(VSEAL_OP_SEAL, 8, SPE_PATH_RAM);
	check(ret == 0 && C.interrupted == 1 && F.now - t0 < 1000, "an eight-page seal ends on its interrupt");
	check(C.ring[(C.n - 1) % VSEAL_JOBS].magic == 0, "a finished descriptor is cleared");
	t0 = F.now;
	ret = run(VSEAL_OP_TEST_NOIRQ, 0, SPE_PATH_VRAM);
	check(ret == 0 && C.restarts == 0 && F.now - t0 >= SPE_BUDGET_FLOOR_US, "no interrupt: done on the done word at the budget");
	/* a job that finishes just before the arm, the status edge-triggered so the arm's clear loses it */
	F.edge = 1;
	F.finish_on_arm = 1;
	F.lat = 200;
	t0 = F.now;
	ret = run(VSEAL_OP_SEAL, 1, SPE_PATH_RAM);
	check(ret == 0 && F.now - t0 < 300 && !F.ibox_full, "finished before the arm: seen by the re-read after it");
	F.edge = 0;
	F.lat = 5;

	/* a stale entry in the interrupt mailbox: popped, counted, the job's own still found */
	F.ibox = 0x12345;
	F.ibox_full = 1;
	ret = run(VSEAL_OP_SEAL, 1, SPE_PATH_RAM);
	check(ret == 0 && C.stale >= 1 && !F.ibox_full && !F.ibox_blocked, "polled: a stale entry drained");
	F.ibox = 0x12346;
	F.ibox_full = 1;
	F.lat = 500;
	t0 = F.now;
	ret = run(VSEAL_OP_SEAL, 8, SPE_PATH_RAM);
	check(ret == 0 && C.stale >= 2 && !F.ibox_full && F.now - t0 < 1000, "interrupt: a stale entry popped by the handler");
	F.lat = 5;

	/* replays and calls with no job */
	{
		vs_u32 old = vseal_word(C.ring[(C.n - 1) % VSEAL_JOBS].seq, (C.n - 1) % VSEAL_JOBS);
		vs_u64 d = C.ring[(C.n - 1) % VSEAL_JOBS].done;

		spe_hw_inbox_write(&F, old);
		fake_advance(F.now + 100);
		check(!F.ibox_full && C.ring[(C.n - 1) % VSEAL_JOBS].done == d, "a finished job's word again: dropped");
		spe_irq_stop(&C, 2, 0);
		spe_irq_stop(&C, 0, 1);
		check(!C.err_word && !F.wakes, "a stop call with no job waiting: ignored");
	}

	/* early wakes and errors */
	F.lat = 5000;
	F.stop2_after = 100;
	ret = run(VSEAL_OP_SEAL, 8, SPE_PATH_RAM);
	check(ret == 0 && C.restarts == 0 && C.spurious >= 1 && C.early_wakes >= 1,
	      "a stop call the SPU doesn't confirm: back to sleep for the rest");
	F.stop2_after = -1;
	F.c0_after = 100;
	{
		vs_u64 s0 = C.spurious, w0 = C.early_wakes;

		ret = run(VSEAL_OP_SEAL, 8, SPE_PATH_RAM);
		check(ret == 0 && C.restarts == 0 && C.spurious == s0 + 1 && C.early_wakes == w0,
		      "class 0 with nothing pending: ignored, no wake");
	}
	F.c0_after = -1;
	F.stale_err_after = 100;
	F.stale_err_word = vseal_word(spe_seq(C.gen, C.n), (C.n - 1) % VSEAL_JOBS);
	ret = run(VSEAL_OP_SEAL, 8, SPE_PATH_RAM);
	check(ret == 0 && C.restarts == 0 && C.errors == 0, "an error tagged with an older job's word: ignored");
	F.stale_err_after = -1;
	F.lat = 5;
	t0 = F.now;
	mk(VSEAL_OP_TEST_BADADDR, 1);
	C.ring[slot()].page[0].src = 0x7000;	/* (the fake faults on any address) */
	ret = spe_run_job(&C, slot(), SPE_PATH_RAM);
	check(ret == SPE_RET_TIMEDOUT && C.errors == 1 && C.restarts == 1 && C.state == SPE_READY && F.now - t0 < 1000,
	      "a DMA fault (class 1): restarted at once, ready again");
	F.lat = 5000;
	F.c0err_after = 50;
	t0 = F.now;
	ret = run(VSEAL_OP_SEAL, 8, SPE_PATH_RAM);
	check(ret == SPE_RET_TIMEDOUT && C.errors == 2 && C.state == SPE_READY && F.now - t0 < SPE_BUDGET_FLOOR_US / 2,
	      "a class 0 error: restarted at once");
	F.c0err_after = -1;
	F.lat = 5;

	/* the descriptor check */
	{
		vs_u64 j0 = C.jobs;
		struct vseal_job *j = mk(VSEAL_OP_SEAL, 1);

		j->page[0].dst = (uintptr_t)A.ring[2];
		check(spe_run_job(&C, slot(), SPE_PATH_RAM) == SPE_RET_INVAL && C.jobs == j0,
		      "a page in the ring (inside the RAM window): refused, nothing ran");
		j = mk(VSEAL_OP_OPEN, 1);
		j->page[0].dst = (uintptr_t)A.status;
		check(spe_run_job(&C, slot(), SPE_PATH_RAM) == SPE_RET_INVAL, "the status line: refused");
		check(spe_run_job(&C, VSEAL_JOBS, SPE_PATH_RAM) == SPE_RET_INVAL, "an index past the ring: refused");
		j = mk(VSEAL_OP_OPEN, 1);
		memset(A.pg2[0], 0xa5, VSEAL_PAGE);
		check(spe_run_job(&C, slot(), SPE_PATH_RAM) == SPE_RET_BADMSG && A.pg2[0][17] == 0xa5,
		      "an open with a wrong tag: -EBADMSG, nothing written");
	}

	/* hang: restarted at the budget, reloaded, the known answer again */
	fresh();
	t0 = F.now;
	ret = run(VSEAL_OP_TEST_HANG, 0, SPE_PATH_RAM);
	check(ret == SPE_RET_TIMEDOUT && C.restarts == 1 && F.loads == 2 && C.gen == 2 && C.state == SPE_READY &&
	      F.now - t0 >= SPE_BUDGET_FLOOR_US && F.now - t0 < 2 * SPE_BUDGET_FLOOR_US, "hang: restarted within its budget");
	/* the known answer after a restart doesn't count as progress */
	ret = run(VSEAL_OP_TEST_HANG, 0, SPE_PATH_RAM);
	check(ret == SPE_RET_TIMEDOUT && C.state == SPE_READY, "a second hang: restarted");
	ret = run(VSEAL_OP_TEST_HANG, 0, SPE_PATH_RAM);
	check(ret == SPE_RET_NODEV && C.state == SPE_GONE && F.gave_up == 1 && F.gave_up_quiet && !C.keep_pages,
	      "three hangs, known answers between: given up quiet");
	check(run(VSEAL_OP_SEAL, 1, SPE_PATH_RAM) == SPE_RET_NODEV, "given up: -ENODEV");
	fresh();
	for (i = 0; i < 5; i++)
		ret = i % 2 ? run(VSEAL_OP_SEAL, 1, SPE_PATH_RAM) : run(VSEAL_OP_TEST_HANG, 0, SPE_PATH_RAM);
	check(C.state == SPE_READY && C.health.noprogress_restarts == 1, "hangs with good seals between: not gone");

	/* a reset that fails: keep every page */
	fresh();
	F.reset_ret = -5;
	ret = run(VSEAL_OP_TEST_HANG, 0, SPE_PATH_RAM);
	check(ret == SPE_RET_KEEP && C.keep_pages && C.state == SPE_GONE && F.gave_up == 1 && !F.gave_up_quiet,
	      "a failed reset: -EHWPOISON, pages kept, given up unquiet");
	check(run(VSEAL_OP_SEAL, 1, SPE_PATH_RAM) == SPE_RET_KEEP, "and -EHWPOISON from then on");

	/* a restart that raced the release (a late interrupt in its grace period): the reset says -EINVAL */
	fresh();
	F.reset_ret = -22;
	ret = run(VSEAL_OP_TEST_HANG, 0, SPE_PATH_RAM);
	check(ret == SPE_RET_NODEV && !C.keep_pages && C.state == SPE_GONE && F.gave_up == 0 && C.restarts == 0 &&
	      C.health.noprogress_restarts == 0, "a reset refused as released: stopped quietly, nothing counted, pages free");

	/* slow but progressing: waited for, not restarted */
	fresh();
	mk(VSEAL_OP_TEST_SLOW, 2);
	C.ring[slot()].arg = 3 * spe_budget_us(VSEAL_OP_TEST_SLOW, 2, SPE_PATH_RAM) * 80 / 2;
	ret = spe_run_job(&C, slot(), SPE_PATH_RAM);
	check(ret == 0 && C.restarts == 0 && C.extended >= 2, "a slow job that progresses: extended, done");

	/* words left in the inbound mailbox across a restart */
	fresh();
	F.keep_inbox = 1;
	{
		struct vseal_job *j = mk(VSEAL_OP_TEST_HANG, 0);
		vs_u32 w;

		j->magic = VSEAL_MAGIC;
		j->seq = spe_seq(C.gen, ++C.n);
		w = vseal_word(j->seq, (C.n - 1) % VSEAL_JOBS);
		spe_hw_inbox_write(&F, w);
		fake_advance(F.now + 10);
		spe_hw_inbox_write(&F, w);	/* the hung job's word again, behind it */
		spe_hw_inbox_write(&F, vseal_word(spe_seq(C.gen, C.n + 5), 2));
		ret = run(VSEAL_OP_ECHO, 0, SPE_PATH_RAM);
		check(ret == SPE_RET_TIMEDOUT && C.restarts == 1 && C.state == SPE_READY,
		      "words across a restart: the reloaded program drops them (one restart)");
		check(run(VSEAL_OP_ECHO, 0, SPE_PATH_RAM) == 0 && spe_golden(&C) == 0 && !F.ibox_full,
		      "and works after");
	}

	/* a wrong known answer */
	fake_init();
	F.golden_wrong = 1;
	check(spe_start(&C) == SPE_RET_IO && C.state == SPE_GONE && F.gave_up == 1 && F.gave_up_quiet,
	      "a wrong known answer: given up quiet");

	check(!vs_overflow, "the MFC model never overflowed");
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
