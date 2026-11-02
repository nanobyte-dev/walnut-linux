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

/*
 * A primitive a client left open blocks the fill (nothing may go between BEGIN and END). The
 * worker ends it as a waiting submitter would only when it has been open a second and an
 * allocation is waiting for the clear; otherwise it waits for its owner (a game stalled at
 * that moment would lose its draw for nothing).
 */
static inline bool vclear_close_primitive(unsigned int open_ms, bool alloc_waiting)
{
	return open_ms >= 1000 && alloc_waiting;
}

/*
 * Same-owner reuse (vkeep.c). A freed VRAM BO whose data only its own file could have written is
 * kept for that file (still allocated in its heap, not cleared), and the file's next allocation of
 * the same size may take it back as it is: nothing crosses from one owner to another. Everything
 * else is cleared as before: a BO another file or a dma-buf holder could reach (shared, sticky),
 * dumb buffers, BOs without an owner or whose owner has closed, and kept ranges that expire, go
 * over the bounds, or meet memory pressure. A kept range never goes back to the free heap
 * without a fill: it is either taken whole by its owner or becomes a zombie.
 */
#define VCLEAR_KEEP_OWNER_BYTES	(32ULL << 20)	/* kept per owner, at most */
#define VCLEAR_KEEP_OWNER_N	32
#define VCLEAR_KEEP_TOTAL	(64ULL << 20)	/* kept in all (VRAM the swap could use) */
#define VCLEAR_KEEP_TOTAL_N	64		/* entries in all: the allocator's walk stays short */
#define VCLEAR_KEEP_AGE_MS	1000		/* kept no longer than this, drawing or not */

static inline bool vclear_may_keep(bool has_owner, bool owner_closed, bool shared, bool dumb,
				   bool reuse_off)
{
	return has_owner && !owner_closed && !shared && !dumb && !reuse_off;
}

/* Over a bound once this entry is kept: the oldest goes to the clear */
static inline bool vclear_keep_over(u64 owner_bytes, u32 owner_n, u64 total, u32 total_n)
{
	return owner_bytes > VCLEAR_KEEP_OWNER_BYTES || owner_n > VCLEAR_KEEP_OWNER_N ||
	       total > VCLEAR_KEEP_TOTAL || total_n > VCLEAR_KEEP_TOTAL_N;
}

/*
 * May owner take this kept range for a new BO of size bytes, aligned to align (a power of two),
 * CPU-mappable if mappable (cpu_limit: the end of the CPU-visible heap)? Only its own, whole,
 * the same size, once the GPU has passed the free (queued work of the old BO doesn't land in the
 * new one: a correctness rule here).
 */
static inline bool vclear_reuse_fits(bool same_owner, bool owner_closed, bool seq_passed,
				     u64 kept_start, u64 kept_size, u64 size, u64 align,
				     bool mappable, u64 cpu_limit)
{
	return same_owner && !owner_closed && seq_passed && kept_size == size &&
	       !(kept_start & (align - 1)) && (!mappable || kept_start + size <= cpu_limit);
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
