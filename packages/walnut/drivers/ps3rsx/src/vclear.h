/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Freed VRAM is cleared before reuse (vclear.c): the rules, as pure code.
 *
 * A freed VRAM range stays allocated in its heap as a zombie until a GPU fill of it has passed:
 * M2MF copies from a 64 KiB zero area in GART (the top of the ring reserve), at most 16 lines
 * of 4 KiB per launch. A zombie is filled in pieces across batches (a cursor), and it is done
 * only when every piece was queued in one recovery generation and the last piece's sequence
 * number has passed; a recovery in between (the new ring never ran the old fills) starts it
 * again from the beginning. The fill is queued after everything submitted before the free and
 * the ring runs in order, so a done zombie also has no older GPU work left that writes it.
 *
 * The allocator waits for zombies only while they can be cleared (the GPU can fill, or the PPE
 * can clear the CPU-visible ones), for a bounded total, and never shrinks the VRAM swap again
 * while chunks it gave back are still zombies.
 *
 * Pure code without kernel dependencies, so test/vclear-test.c builds it on the host.
 */
#ifndef PS3RSX_VCLEAR_H
#define PS3RSX_VCLEAR_H

#define VCLEAR_LINE		4096u		/* bytes per M2MF line, as kcopy's */
#define VCLEAR_ZERO_SIZE	0x10000u	/* the zero area */
#define VCLEAR_MAX_LINES	(VCLEAR_ZERO_SIZE / VCLEAR_LINE)	/* lines per launch */
#define VCLEAR_WAIT_MS		1000		/* an allocation waits at most this long in all */
#define VCLEAR_WAIT_STEP_MS	200		/* and this long at a time */

/* One M2MF launch: lines of VCLEAR_LINE bytes at VRAM offset dst, from the zero area */
struct vclear_launch {
	u64 dst;
	u32 lines;
};

/* A zombie's fill progress */
struct vclear_cursor {
	u64 lo, hi;		/* the page-aligned part of the zombie the fill covers */
	u64 next;		/* everything below it is queued (in generation gen) */
	u32 gen;		/* the recovery generation the queued pieces belong to */
	u32 last_seq;		/* the reference of the last queued piece */
};

static inline void vclear_cursor_init(struct vclear_cursor *c, u64 start, u64 size)
{
	u64 lo = (start + VCLEAR_LINE - 1) & ~(u64)(VCLEAR_LINE - 1);
	u64 hi = (start + size) & ~(u64)(VCLEAR_LINE - 1);

	c->lo = lo;
	c->hi = hi > lo ? hi : lo;
	c->next = c->lo;
	c->gen = 0;
	c->last_seq = 0;
}

/* Every queued piece of the zombie is forgotten: it is filled again from the start */
static inline void vclear_cursor_restart(struct vclear_cursor *c)
{
	c->next = c->lo;
}

/*
 * The next piece of the zombie for a batch in generation gen: up to budget bytes, as at most
 * max launches into l[]. Pieces queued in another generation are filled again (the cursor goes
 * back to the start first). Returns the number of launches; *to is where the piece ends, which
 * vclear_queued() records once the batch is in the ring. Nothing outside [lo, hi) is written,
 * and no launch has 0 lines.
 */
static inline u32 vclear_take(struct vclear_cursor *c, u32 gen, u64 budget,
			      struct vclear_launch *l, u32 max, u64 *to)
{
	u64 at;
	u32 n = 0;

	if (c->next != c->lo && c->gen != gen)
		vclear_cursor_restart(c);
	at = c->next;
	while (n < max && at < c->hi && budget >= VCLEAR_LINE) {
		u64 lines = (c->hi - at) / VCLEAR_LINE;

		if (lines > VCLEAR_MAX_LINES)
			lines = VCLEAR_MAX_LINES;
		if (lines > budget / VCLEAR_LINE)
			lines = budget / VCLEAR_LINE;
		l[n].dst = at;
		l[n].lines = (u32)lines;
		n++;
		at += lines * VCLEAR_LINE;
		budget -= lines * VCLEAR_LINE;
	}
	*to = at;
	return n;
}

/* The piece up to to is in the ring, generation gen, reference seq */
static inline void vclear_queued(struct vclear_cursor *c, u64 to, u32 gen, u32 seq)
{
	c->next = to;
	c->gen = gen;
	c->last_seq = seq;
}

enum vclear_state {
	VCLEAR_MORE,		/* not all of it is queued yet */
	VCLEAR_WAIT,		/* all queued, the last piece not passed yet */
	VCLEAR_DONE,		/* filled: the range may go back to its heap */
	VCLEAR_REFILL,		/* queued before a recovery: fill it again */
};

/*
 * Where the zombie stands. passed: the GPU's reference had passed c->last_seq, read before
 * gen_now (the reference first, then a read barrier, then the generation: a recovery between
 * the two reads is then seen as a new generation, never as a fill that ran).
 */
static inline enum vclear_state vclear_state(const struct vclear_cursor *c, bool passed,
					     u32 gen_now)
{
	if (c->lo == c->hi)
		return VCLEAR_DONE;		/* no whole page: nothing a BO could use */
	if (c->next == c->lo)
		return VCLEAR_MORE;
	if (c->gen != gen_now)
		return VCLEAR_REFILL;
	if (c->next < c->hi)
		return VCLEAR_MORE;
	return passed ? VCLEAR_DONE : VCLEAR_WAIT;
}

enum vclear_alloc_step {
	VCLEAR_ALLOC_WAIT,	/* wait for zombies (bounded), then place again */
	VCLEAR_ALLOC_SHRINK,	/* take VRAM from the swap pool */
	VCLEAR_ALLOC_FAIL,	/* -ENOSPC (Mesa then uses GART) */
};

/*
 * A VRAM placement failed: what next. cpu/gpu: zombie bytes pending in the CPU-visible and
 * GPU-only heaps; cpu_ok/gpu_ok: whether they can be cleared now (the GPU fills, or the PPE for
 * CPU-visible ones); shrunk: zombie bytes from the swap pool still pending; waited_ms so far,
 * shrinks so far, size of the request.
 */
static inline enum vclear_alloc_step vclear_alloc_next(u64 cpu, bool cpu_ok, u64 gpu, bool gpu_ok,
						       u64 shrunk, u64 waited_ms, u32 shrinks,
						       u64 size)
{
	bool can_wait = waited_ms < VCLEAR_WAIT_MS && ((cpu && cpu_ok) || (gpu && gpu_ok));

	if (can_wait)
		return VCLEAR_ALLOC_WAIT;
	if (shrunk || !cpu_ok)
		return VCLEAR_ALLOC_FAIL;	/* never empty the pool for chunks that wait */
	if (shrinks >= (size + 0xfffff) / 0x100000 + 1)
		return VCLEAR_ALLOC_FAIL;
	return VCLEAR_ALLOC_SHRINK;
}

#endif
