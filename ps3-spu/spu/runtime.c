/* libspeq-spu.a: main and the claim loop of a resident SPU worker. The PPE starts it with
 * r3 = worker index, r4 = control block EA, r6 = local store EA (crt0 keeps it in
 * __ea_local_store). It claims jobs from the ring in the control block (speq-abi.h), runs
 * speq_ops[job.op], completes the job's done word, and sleeps in the event wait while there is
 * nothing to do; idle past ctl.idle_ticks it parks (stop 0x2110) and the PPE thread sleeps.
 * A queue in deadline order (ctl.order, speq-abi.h) is claimed by job, earliest deadline
 * first, instead of at head.
 *
 * Only hardware can tell (speq-bench P1, P4):
 * - that a PPE store to line 0 raises LLR_LOST on an SPU blocked in the event wait, and how
 *   fast; a store to another word of the line must too, one to the next line must not;
 * - that the decrementer counts under spufs, at the timebase rate. If it doesn't, workers
 *   never park (they still wake on stores); if LLR_LOST never comes, they wake only by the
 *   decrementer, so a job waits up to idle_ticks and then a park/unpark. */
#include "speq-spu.h"

/* The program's ABI word, read from the ELF by speq_open (section, not symbol: strip keeps it) */
const uint32_t speq_spu_abi __attribute__((section(".speq_abi"), used)) = SPEQ_ABI_VERSION;

static SPEQ_LOCAL struct speq_ctl ctl __attribute__((aligned(128)));
static SPEQ_LOCAL struct speq_job job __attribute__((aligned(128)));
static SPEQ_LOCAL struct speq_done dline __attribute__((aligned(128)));
static SPEQ_LOCAL struct speq_wline wl __attribute__((aligned(128)));
static SPEQ_LOCAL struct speq_job scan[SPEQ_SCAN] __attribute__((aligned(128)));
static SPEQ_LOCAL uint64_t dl[SPEQ_JOBS] __attribute__((aligned(128)));
static SPEQ_LOCAL uint64_t cb_ea;
static SPEQ_LOCAL unsigned me;
static SPEQ_LOCAL uint32_t tb_hz;

unsigned speq_worker(void) { return me; }
uint32_t speq_tb_hz(void) { return tb_hz; }

/* Our line in the control block: the held job and the counters */
static void put_wline(void)
{
	speq_put(&wl, cb_ea + SPEQ_WLINE_OFF(me), sizeof(wl), SPEQ_RT_TAG);
	speq_wait_tags(1u << SPEQ_RT_TAG);
}

/* One job less on its done word; the first error sticks. The last one clears the waiters bit
 * and, if it was set, wakes them: futex(FUTEX_WAKE_PRIVATE) through stop 0x2104, which the
 * kernel runs on our PPE thread (private futexes match: same mm). */
static void complete(uint64_t ea, int err)
{
	uint32_t was, left;

	if (!ea || (ea & (SPEQ_LINE - 1)))
		return;	/* speq_submit refuses these; never DMA to them */
	do {
		speq_getllar(&dline, ea);
		was = dline.pending;
		left = was & ~SPEQ_WAITERS;
		if (!left)
			return;	/* not ours to count (the PPE failed it already) */
		dline.pending = left == 1 ? 0 : was - 1;
		if (err && !dline.error)
			dline.error = err;
	} while (speq_putllc(&dline, ea));
	if (left == 1 && (was & SPEQ_WAITERS))
		speq_syscall(SPEQ_NR_futex, ea, SPEQ_FUTEX_WAKE_PRIVATE, 0x7fffffff, 0, 0, 0);
}

/* Idle too long: set our parked bit in the same reservation that saw no work, then stop.
 * The PPE thread futex-waits on tail_parked until a submit clears the bit, and reruns us
 * from here (or frees the context after a while; a fresh one starts at main). */
static void park(void)
{
	for (;;) {
		speq_getllar(&ctl, cb_ea);
		if (ctl.quit || ctl.head != ctl.tail_parked >> SPEQ_TAIL_SHIFT)
			return;
		ctl.tail_parked |= 1u << me;
		if (!speq_putllc(&ctl, cb_ea))
			break;
	}
	wl.parks++;
	put_wline();
	speq_stop_park();
}

static uint64_t slot_ea(uint32_t seq)
{
	return cb_ea + SPEQ_RING_OFF + (seq % SPEQ_JOBS) * SPEQ_LINE;
}

/* Deadline order: moves head past the claimed jobs at it. Anyone may (the PPE never does while
 * workers run); a lost reservation means someone else moved it, so look again. */
static void advance(void)
{
	uint32_t h, f;

	for (;;) {
		speq_getllar(&ctl, cb_ea);
		h = ctl.head;
		if (h == ctl.tail_parked >> SPEQ_TAIL_SHIFT)
			return;
		speq_get(&scan[0], slot_ea(h), 16, SPEQ_RT_TAG);
		speq_wait_tags(1u << SPEQ_RT_TAG);
		f = scan[0].flags;
		if (f != (h | SPEQ_JOB_CLAIMED))
			return;	/* not taken yet: it stays at head */
		ctl.head = (h + 1) & SPEQ_SEQ_MASK;
		speq_putllc(&ctl, cb_ea);
	}
}

/* Deadline order: claims into job the queued job with the earliest deadline (the earliest
 * queued on a tie) among the first SPEQ_SCAN from head. 1: claimed, *seqp its sequence number;
 * 0: none to claim (all taken, head not moved yet); -1: lost it to another claim */
static int claim_dl(uint32_t head, uint32_t tail, uint32_t *seqp)
{
	uint32_t n = (tail - head) & SPEQ_SEQ_MASK, i0 = head % SPEQ_JOBS, first, k, seq;
	uint64_t best_dl = 0, ea;
	int best = -1;

	if (n > SPEQ_SCAN)
		n = SPEQ_SCAN;
	first = SPEQ_JOBS - i0 < n ? SPEQ_JOBS - i0 : n;
	speq_get(scan, cb_ea + SPEQ_RING_OFF + i0 * SPEQ_LINE, first * SPEQ_LINE, SPEQ_RT_TAG);
	if (n > first)
		speq_get(&scan[first], cb_ea + SPEQ_RING_OFF, (n - first) * SPEQ_LINE, SPEQ_RT_TAG);
	speq_get(dl, cb_ea + SPEQ_DL_OFF, sizeof(dl), SPEQ_RT_TAG);
	speq_wait_tags(1u << SPEQ_RT_TAG);
	for (k = 0; k < n; k++) {
		seq = (head + k) & SPEQ_SEQ_MASK;
		if (scan[k].flags != seq)
			continue;	/* claimed */
		if (best < 0 || dl[seq % SPEQ_JOBS] < best_dl) {
			best = k;
			best_dl = dl[seq % SPEQ_JOBS];
		}
	}
	if (best < 0)
		return 0;
	seq = (head + best) & SPEQ_SEQ_MASK;
	ea = slot_ea(seq);
	/* the line itself, reserved: still this job, unclaimed? (the slot is reused only after
	 * head passed it, as job seq + SPEQ_JOBS) */
	speq_getllar(&job, ea);
	if (job.flags != seq)
		return -1;
	job.flags = seq | SPEQ_JOB_CLAIMED;
	if (speq_putllc(&job, ea))
		return -1;
	*seqp = seq;
	return 1;
}

int SPEQ_MAIN(unsigned long long speid, unsigned long long argp, unsigned long long envp)
{
	uint32_t head, tail, ev = 0, err, armed = 0, seq;
	int idle = 0, woke = 0, r;

	(void)envp;
	me = speid;
	cb_ea = argp;
	speq_get(&ctl, cb_ea, sizeof(ctl), SPEQ_RT_TAG);
	speq_wait_tags(1u << SPEQ_RT_TAG);
	if (ctl.abi != SPEQ_ABI_VERSION || me >= SPEQ_MAX_WORKERS)
		return SPEQ_EXIT_ABI;
	tb_hz = ctl.tb_hz;
	put_wline();
	/* Once: what the event wait wakes on. LLR_LOST: the reservation on line 0 is gone (a
	 * submit, a claim, quit). DECREMENTER: the idle timer. */
	speq_event_mask(SPEQ_LLR_LOST_EVENT | SPEQ_DECREMENTER_EVENT);

	for (;;) {
		/* stale events (a lost claim's reservation, a context restore) would make the
		 * wait below return at once; the state is read fresh right after anyway */
		speq_event_drain();
		speq_getllar(&ctl, cb_ea);
		if (ctl.quit)
			break;
		head = ctl.head;
		tail = ctl.tail_parked >> SPEQ_TAIL_SHIFT;
		if (head != tail && ctl.order == SPEQ_ORDER_DEADLINE) {
			r = claim_dl(head, tail, &seq);
			if (r < 0)
				wl.lost++;
			advance();	/* past ours, or past others' claims not moved yet */
			if (r < 0 || (r == 0 && ctl.head != head))
				continue;
			if (r == 0)
				tail = head;	/* nothing to take (can't stay so): wait as idle */
		} else if (head != tail) {
			/* Fetch the job before the claim: once head moves past it the PPE may
			 * refill the slot. A failed claim drops the copy. */
			speq_get(&job, slot_ea(head), sizeof(job), SPEQ_RT_TAG);
			speq_wait_tags(1u << SPEQ_RT_TAG);
			ctl.head = (head + 1) & SPEQ_SEQ_MASK;
			if (speq_putllc(&ctl, cb_ea)) {
				wl.lost++;
				continue;
			}
			seq = head;
		}
		if (head != tail) {
			wl.held = 1;
			wl.seq = seq;
			wl.done = job.done;
			wl.claims++;
			put_wline();
			err = job.op < speq_nops && speq_ops[job.op] ? speq_ops[job.op](&job) : -38;
			speq_wait_tags(SPEQ_OP_TAGS);	/* its output is out before done says so */
			complete(job.done, err);
			wl.held = 0;
			put_wline();
			idle = woke = 0;
			continue;
		}
		if (woke && !(ev & SPEQ_DECREMENTER_EVENT))
			wl.spurious++;	/* other SPUs' claims, the poke word, a context restore */
		if (!idle || ctl.idle_ticks != armed) {
			/* arm the idle timer once per idle stretch (spurious wakes don't extend
			 * it), and again when the PPE changes the idle time */
			armed = ctl.idle_ticks;
			speq_dec_write(armed ? armed : 0x7fffffff);
			idle = 1;
		} else if (armed && (int32_t)speq_dec_read() < 0) {
			park();
			idle = woke = 0;
			continue;
		}
		ev = speq_event_wait();
		wl.wakes++;
		if (ev & SPEQ_DECREMENTER_EVENT)
			wl.timer++;
		woke = 1;
	}
	wl.held = 0;
	put_wline();
	return 0;
}
