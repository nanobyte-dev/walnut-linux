// SPDX-License-Identifier: GPL-2.0
/*
 * Host test of src/vclear.h, the rules of the VRAM clear (vclear.c), on a model: a heap of 4 KiB
 * pages, BOs whose pages carry their owner's tag once the GPU wrote them, a GPU that runs its
 * ring in order up to where it is told (client writes, the clear's fills), recoveries that drop
 * what the ring hadn't run and bump the generation, and the allocator's steps (mem.c). The
 * worker below does what vclear.c's gpu_batch() and reap_vram() do, through the same functions.
 * Checked throughout: a page handed out reads zero; a live BO's page never gets another owner's
 * data (a write queued before the free landing after the reuse, P8); no launch has 0 lines or
 * more than 16, or writes outside its zombie. Cases:
 * - a fill that never runs (the GPU never gets there): the range is never handed out;
 * - a 64 MiB zombie with a recovery after its third batch: nothing of it handed out until all of
 *   it is filled in the new generation;
 * - an allocation with only zombies free waits and gets the memory, never -ENOSPC;
 * - a swap chunk given back (still holding swapped pages) is filled before a BO lands on it, and
 *   one 8 MiB request makes at most 9 shrinks;
 * - a GART range freed with a GPU write still queued isn't reused before that write ran;
 * - random frees, allocations, GPU progress and recoveries.
 * OLD_FREE_AT_ONCE (fix-off.sh): a freed range goes back at once, as before. fix-off.sh also
 * edits copies of vclear.h (VCLEAR_H): no generation check, a last launch that rounds up.
 *   make, then $(O)/vclear-test [ROUNDS] [SEED]
 */
#include "shim.h"
#ifndef VCLEAR_H
#define VCLEAR_H "../src/vclear.h"
#endif
#include VCLEAR_H

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define PAGE		4096u
#define NPAGES		(96 * 256)		/* a 96 MiB model heap */
#define MAXZ		4096
#define MAXOPS		200000
#define BATCH		(8u << 20)

enum { FREE, LIVE, ZOMBIE };
/* pages [0, NPAGES) are the VRAM heap, [NPAGES, 2 * NPAGES) the GART heap */
static u8 state[2 * NPAGES];
static int owner[2 * NPAGES];	/* the live BO's id */
static int data[2 * NPAGES];	/* what the page holds: 0 zero, else a writer's tag */
static const char *what = "";
static long pool[96];		/* the swap pool's chunks (256 pages), -1 once given back */
static int npool, shrinks;

struct zombie {
	bool used, gart, vswap;
	u64 start, size;
	u32 free_seq;
	struct vclear_cursor cur;
};
static struct zombie zs[MAXZ];

/* the GPU: its ring of ops, run in order */
struct op {
	u32 seq;
	int kind;		/* 0 a client write of tag to pages [a, b), 1 a fill of [a, b) */
	u64 a, b;
	int tag;
};
static struct op ring[MAXOPS];
static unsigned int ring_head, ring_tail;	/* ops [head, tail) not run yet */
static u32 seqno = 0x5e000000, ref = 0x5e000000, gen;

static void reset_model(void)
{
	memset(state, 0, sizeof(state));
	memset(owner, 0, sizeof(owner));
	memset(data, 0, sizeof(data));
	memset(zs, 0, sizeof(zs));
	ring_head = ring_tail = 0;
	gen = 0;
	npool = 0;
	shrinks = 0;
	seqno = ref = 0x5e000000;
}

static bool passed(u32 seq)
{
	return (int32_t)(ref - seq) >= 0;
}

static u32 queue(int kind, u64 a, u64 b, int tag)
{
	ring[ring_tail % MAXOPS] = (struct op){ ++seqno, kind, a, b, tag };
	ring_tail++;
	return seqno;
}

/* the GPU runs n more ops; returns how many it ran */
static unsigned int gpu_run(unsigned int n)
{
	unsigned int ran = 0;

	while (n-- && ring_head != ring_tail) {
		struct op *o = &ring[ring_head++ % MAXOPS];
		u64 p;

		for (p = o->a; p < o->b; p++) {
			if (o->kind == 1) {
				data[p] = 0;
				continue;
			}
			data[p] = o->tag;
			/* the write lands on a page another BO owns now: a leak (P8) */
			CHECK(state[p] != LIVE || owner[p] == o->tag,
			      "%s: page %llu of BO %d got BO %d's write (freed before it ran)", what,
			      (unsigned long long)p, owner[p], o->tag);
		}
		ref = o->seq;
		ran++;
	}
	return ran;
}

/* a recovery: the ring's unrun ops are gone, the generation moves on */
static void recover(void)
{
	ring_head = ring_tail;
	gen++;
}

static int zombie_add(u64 start, u64 size, bool gart, bool vswap)
{
	int i;

	for (i = 0; i < MAXZ && zs[i].used; i++)
		;
	if (i == MAXZ)
		return -1;
	memset(&zs[i], 0, sizeof(zs[i]));
	zs[i].used = true;
	zs[i].gart = gart;
	zs[i].vswap = vswap;
	zs[i].start = start;
	zs[i].size = size;
	zs[i].free_seq = seqno;
	vclear_cursor_init(&zs[i].cur, start * PAGE, size * PAGE);
	return i;
}

static void range_set(u64 start, u64 size, int st, int id)
{
	u64 p;

	for (p = start; p < start + size; p++) {
		state[p] = st;
		owner[p] = id;
	}
}

/* a BO's free (or a swap chunk's return): a zombie, or (as before) back at once */
static void range_free(u64 start, u64 size, bool gart, bool vswap)
{
#ifdef OLD_FREE_AT_ONCE
	(void)gart;
	(void)vswap;
	range_set(start, size, FREE, 0);
#else
	range_set(start, size, ZOMBIE, 0);
	CHECK(zombie_add(start, size, gart, vswap) >= 0, "%s: no zombie slot", what);
#endif
}

static void bo_free(u64 start, u64 size, bool gart)
{
	range_free(start, size, gart, false);
}

/* first fit; -1 if no room. A placed BO must read zero (VRAM; GART is CPU-cleared then) */
static long place(u64 size, int id, bool gart)
{
	u64 p, run = 0, base = gart ? NPAGES : 0;

	for (p = base; p < base + NPAGES; p++) {
		run = state[p] == FREE ? run + 1 : 0;
		if (run == size) {
			u64 s = p + 1 - size, q;

			if (!gart)
				for (q = s; q < s + size; q++)
					CHECK(data[q] == 0, "%s: BO %d got page %llu uncleared (tag %d)", what,
					      id, (unsigned long long)q, data[q]);
			else
				for (q = s; q < s + size; q++)
					data[q] = 0;	/* ps3rsx_gart_clear */
			range_set(s, size, LIVE, id);
			return (long)s;
		}
	}
	return -1;
}

/* the worker's batch (vclear.c gpu_batch), when the GPU is the worker's to use */
static void worker_batch(u64 budget)
{
	struct vclear_launch l[BATCH / VCLEAR_ZERO_SIZE];
	unsigned int nl = 0, i, j, k;
	int zi[64], nz = 0;
	u64 to[64];

	for (i = 0; i < MAXZ && nz < 64 && nl < sizeof(l) / sizeof(l[0]) && budget >= PAGE; i++) {
		struct zombie *z = &zs[i];
		enum vclear_state st;
		unsigned int n;
		u64 bytes = 0;

		if (!z->used || z->gart)
			continue;
		st = vclear_state(&z->cur, false, gen);
		if (st != VCLEAR_MORE && st != VCLEAR_REFILL)
			continue;
		n = vclear_take(&z->cur, gen, budget, l + nl, sizeof(l) / sizeof(l[0]) - nl, &to[nz]);
		for (j = nl; j < nl + n; j++) {
			u64 a = l[j].dst, b = a + (u64)l[j].lines * PAGE;

			CHECK(l[j].lines >= 1 && l[j].lines <= VCLEAR_MAX_LINES, "%s: launch of %u lines",
			      what, l[j].lines);
			CHECK(a % PAGE == 0 && a >= z->start * PAGE && b <= (z->start + z->size) * PAGE,
			      "%s: launch %llx-%llx outside its zombie %llx-%llx", what,
			      (unsigned long long)a, (unsigned long long)b,
			      (unsigned long long)(z->start * PAGE),
			      (unsigned long long)((z->start + z->size) * PAGE));
			bytes += b - a;
		}
		if (!n)
			continue;
		budget -= bytes < budget ? bytes : budget;
		nl += n;
		zi[nz++] = i;
	}
	if (!nl)
		return;
	/* one op per launch, in ring order, then the batch's reference */
	for (j = 0; j < nl; j++) {
		u64 a = l[j].dst / PAGE, b = a + l[j].lines;

		if (b > NPAGES)
			b = NPAGES;
		queue(1, a, b, 0);
	}
	for (k = 0; k < (unsigned int)nz; k++)
		vclear_queued(&zs[zi[k]].cur, to[k], gen, seqno);
}

/* the reap (vclear.c reap_vram and the GART reap) */
static void worker_reap(void)
{
	int i;

	for (i = 0; i < MAXZ; i++) {
		struct zombie *z = &zs[i];

		if (!z->used)
			continue;
		if (z->gart) {
			if (passed(z->free_seq)) {
				range_set(z->start, z->size, FREE, 0);
				z->used = false;
			}
			continue;
		}
		if (vclear_state(&z->cur, z->cur.next != z->cur.lo && passed(z->cur.last_seq), gen) ==
		    VCLEAR_DONE) {
			range_set(z->start, z->size, FREE, 0);
			z->used = false;
		}
	}
}

static void worker(void)
{
	worker_reap();
	worker_batch(BATCH);
}

static u64 zombie_pages(void)
{
	u64 n = 0;
	int i;

	for (i = 0; i < MAXZ; i++)
		if (zs[i].used && !zs[i].gart)
			n += zs[i].size;
	return n;
}

/* vswap shrink: one chunk (holding swapped pages, tag 999) given back */
static bool vswap_shrink(void)
{
	int i;

	for (i = 0; i < npool; i++)
		if (pool[i] >= 0) {
			range_free((u64)pool[i], 256, false, true);
			pool[i] = -1;
			shrinks++;
			return true;
		}
	return false;
}

static u64 shrunk_pages(void)
{
	u64 n = 0;
	int i;

	for (i = 0; i < MAXZ; i++)
		if (zs[i].used && zs[i].vswap)
			n += zs[i].size;
	return n;
}

/*
 * mem.c's vram_place_wait: steps of VCLEAR_WAIT_STEP_MS; gpu_ok says whether the worker and
 * the GPU run while it waits. Returns the placement or -1 (ENOSPC).
 */
static long alloc(u64 size, int id, bool gpu_ok, unsigned int gpu_ops_per_step)
{
	u64 waited = 0;
	u32 sh = 0;
	long at = place(size, id, false);

	while (at < 0) {
		u64 zp = zombie_pages();

		switch (vclear_alloc_next(zp * PAGE, gpu_ok, 0, false, shrunk_pages() * PAGE, waited, sh,
					  size * PAGE)) {
		case VCLEAR_ALLOC_WAIT: {
			/* the worker runs its batches back to back, the GPU gets so far meanwhile
			 * (64 KiB per op at 2 GB/s: 32 ops a millisecond); the wait ends when
			 * something was given back, or after a step */
			unsigned int ran = 0;
			u64 before = zp;

			for (int k = 0; gpu_ok && k < 64 && zombie_pages() == before; k++) {
				worker();
				ran += gpu_run(gpu_ops_per_step);
				worker_reap();
			}
			waited += zombie_pages() == before ? VCLEAR_WAIT_STEP_MS : 1 + ran / 32;
			break;
		}
		case VCLEAR_ALLOC_SHRINK:
			sh++;
			if (!vswap_shrink())
				return -1;
			break;
		case VCLEAR_ALLOC_FAIL:
			return -1;
		}
		at = place(size, id, false);
	}
	return at;
}

static void no_foreign_data(void)
{
	u64 p;

	for (p = 0; p < 2 * NPAGES; p++)
		if (state[p] == LIVE && data[p])
			CHECK(data[p] == owner[p], "%s: BO %d's page %llu holds BO %d's data", what,
			      owner[p], (unsigned long long)p, data[p]);
}

static void case_fill_never_runs(void)
{
	long a, b;

	what = "a fill that never runs";
	reset_model();
	a = place(NPAGES, 1, false);
	queue(0, a, a + NPAGES, 1);
	gpu_run(~0u);
	bo_free(a, NPAGES, false);
	/* the worker queues fills, the GPU never runs them */
	worker();
	worker();
	b = alloc(16, 2, false, 0);
	CHECK(b < 0, "%s: BO handed out at page %ld", what, b);
	b = alloc(16, 3, true, 0);	/* the worker runs, the GPU still doesn't */
	CHECK(b < 0, "%s: BO handed out at page %ld with the fill queued", what, b);
}

static void case_recovery_64m(void)
{
	u64 big = 64 * 256;
	long a, b;
	int round;

	what = "a 64 MiB zombie and a recovery";
	reset_model();
	a = place(big, 1, false);
	queue(0, a, a + big, 1);
	gpu_run(~0u);
	b = place(NPAGES - big, 9, false);	/* the rest of the heap is in use */
	bo_free(a, big, false);
	for (round = 0; round < 3; round++) {	/* three 8 MiB batches run */
		worker();
		gpu_run(~0u);
	}
	worker();		/* the fourth is queued, then the recovery drops it */
	recover();
	worker_reap();
	CHECK(place(1, 2, false) < 0, "%s: a page handed out after the recovery", what);
	for (round = 0; round < 7; round++) {	/* the new generation's first 7 of 8 */
		worker();
		gpu_run(~0u);
		worker_reap();
		CHECK(place(1, 2, false) < 0, "%s: a page handed out before all of it was filled again (round %d)",
		      what, round);
	}
	worker();
	gpu_run(~0u);
	worker_reap();
	CHECK(place(big, 2, false) == a, "%s: not handed out once filled", what);
	(void)b;
}

/* all of a zombie queued, then a recovery drops it; later work passes its reference */
static void case_recovery_queued(void)
{
	long a;

	what = "a zombie fully queued when the recovery comes";
	reset_model();
	a = place(NPAGES, 1, false);
	queue(0, a, a + NPAGES / 2, 1);
	gpu_run(~0u);
	bo_free(a, NPAGES / 2 > 2048 ? 2048 : NPAGES / 2, false);
	range_set(a + 2048, NPAGES - 2048, LIVE, 1);
	worker();			/* all 8 MiB in one batch */
	recover();
	queue(0, NPAGES - 1, NPAGES, 1);	/* the new ring runs on past the old reference */
	gpu_run(~0u);
	worker_reap();
	CHECK(place(2048, 2, false) < 0, "%s: handed out unfilled", what);
	worker();
	gpu_run(~0u);
	worker_reap();
	CHECK(place(2048, 2, false) == a, "%s: not handed out after its new fill", what);
}

static void case_alloc_waits(void)
{
	long a, b;

	what = "an allocation with only zombies free";
	reset_model();
	a = place(NPAGES, 1, false);
	queue(0, a, a + NPAGES, 1);
	gpu_run(~0u);
	bo_free(a, NPAGES, false);
	b = alloc(1024, 2, true, 64);
	CHECK(b >= 0, "%s: -ENOSPC while the fills progress", what);
}

static void case_vswap_chunk(void)
{
	long at;
	int i;

	what = "a swap chunk given back";
	reset_model();
	/* the heap's top quarter is the pool, its chunks full of swapped pages */
	npool = 24;
	shrinks = 0;
	for (i = 0; i < npool; i++) {
		u64 s = NPAGES - (u64)(i + 1) * 256, p;

		pool[i] = (long)s;
		range_set(s, 256, LIVE, 999);
		for (p = s; p < s + 256; p++)
			data[p] = 999;
	}
	place(NPAGES - npool * 256, 5, false);	/* everything else in use */
	at = alloc(8 * 256, 2, true, 8);
	CHECK(at >= 0, "%s: an 8 MiB BO didn't get swap chunks", what);
	CHECK(shrinks <= 9, "%s: %d shrinks for one 8 MiB request", what, shrinks);
}

static void case_gart(void)
{
	long a, b;

	what = "a GART range with a write queued";
	reset_model();
	a = place(64, 1, true);
	place(NPAGES - 64, 9, true);
	queue(0, a, a + 64, 1);		/* queued, not run */
	bo_free(a, 64, true);
	worker_reap();
	b = place(64, 2, true);
	CHECK(b < 0, "%s: reused before the write ran", what);
	gpu_run(~0u);
	worker_reap();
	b = place(64, 2, true);
	CHECK(b == a, "%s: not reused after", what);
	no_foreign_data();
}

static u64 rnd(u64 *s)
{
	*s ^= *s << 13;
	*s ^= *s >> 7;
	*s ^= *s << 17;
	return *s;
}

static void case_random(long rounds, u64 seed)
{
	struct { long at; u64 size; bool gart; } bos[256];
	int nbo = 0, id = 100;
	u64 s = seed * 0x9e3779b97f4a7c15ULL + 3;
	long r, got = 0;

	what = "random";
	reset_model();
	for (r = 0; r < rounds; r++) {
		switch (rnd(&s) % 8) {
		case 0: case 1: {	/* a new BO, written by the GPU */
			u64 size = 1 + rnd(&s) % (rnd(&s) % 4 ? 32 : 2048);
			bool gart = rnd(&s) % 4 == 0;
			long at = gart ? place(size, id, true) : alloc(size, id, true, rnd(&s) % 16);

			if (at >= 0 && nbo < 256) {
				bos[nbo].at = at;
				bos[nbo].size = size;
				bos[nbo++].gart = gart;
				queue(0, at, at + size, id);
				got++;
			} else if (at >= 0) {
				range_set(at, size, FREE, 0);
			}
			id++;
			break;
		}
		case 2: case 3:		/* a free, sometimes with a write of it still queued */
			if (nbo) {
				int k = rnd(&s) % nbo;

				if (rnd(&s) % 2)
					queue(0, bos[k].at, bos[k].at + bos[k].size, owner[bos[k].at]);
				bo_free(bos[k].at, bos[k].size, bos[k].gart);
				bos[k] = bos[--nbo];
			}
			break;
		case 4: case 5:
			gpu_run(rnd(&s) % 64);
			break;
		case 6:
			worker();
			break;
		case 7:
			if (rnd(&s) % 64 == 0)
				recover();
			worker_reap();
			break;
		}
		no_foreign_data();
	}
	printf("random: %ld rounds, %ld BOs placed, %u ops queued\n", rounds, got, ring_tail);
}

int main(int argc, char **argv)
{
	long rounds = argc > 1 ? atol(argv[1]) : 20000;
	u64 seed = argc > 2 ? strtoull(argv[2], NULL, 0) : 1;

	case_fill_never_runs();
	case_recovery_64m();
	case_recovery_queued();
	case_alloc_waits();
	case_vswap_chunk();
	case_gart();
	case_random(rounds, seed);
	printf("vclear-test: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
