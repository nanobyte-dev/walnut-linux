// SPDX-License-Identifier: GPL-2.0
/*
 * Host test of src/pushbuf.h, GEM_PUSHBUF's argument checks (submit.c): pushes whose offset and
 * length reach the u64 edge, totals that would wrap, the state restore in front of them in the
 * ring, and relocation offsets at the u32 edge. Whatever is accepted must lie inside its BO, the
 * total must be at most MAX_SUBMIT and the ring length exactly restore + total (all compared in
 * 128 bits); a relocation must patch the word it names, inside its push. What Mesa sends must
 * still pass, relocations included (NV30_PS3_RELOCS=1, as Armagetron's variant_relocs runs it).
 * Table cases, then random submits and relocations built from edge values.
 * OLD_PUSH_CHECKS, OLD_RELOC_U32 (fix-off.sh): submit.c's checks as they were, sums that could
 * wrap.
 *   make, then $(O)/pushbuf-test [ROUNDS] [SEED]
 */
#include "shim.h"
#include "../src/pushbuf.h"

typedef unsigned __int128 u128;

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct push {
	u64 offset, len, bo_size;
};

/* submit.c's loop over the pushes, then the ring length: 0 with *total and *ring_len, or an errno */
static int submit_args(const struct push *p, int n, u32 restore, u64 *total, u32 *ring_len)
{
	int i, ret;

	*total = 0;
#ifndef OLD_PUSH_CHECKS
	for (i = 0; i < n; i++) {
		ret = ps3rsx_pushbuf_args_check(p[i].offset, p[i].len, p[i].bo_size, total);
		if (ret)
			return ret;
	}
	return ps3rsx_pushbuf_ring_len(restore, *total, ring_len);
#else
	(void)ret;
	for (i = 0; i < n; i++) {
		if ((p[i].len & 3) || (p[i].offset & 3))
			return -EINVAL;
		if (p[i].offset + p[i].len > p[i].bo_size)
			return -EINVAL;
		*total += p[i].len;
	}
	if (*total > MAX_SUBMIT)
		return -E2BIG;
	*ring_len = restore + *total;	/* ps3rsx_ring_begin()'s u32 */
	return 0;
#endif
}

/* An accepted submit is safe: every push inside its BO, the total and the ring length exact */
static void check_safe(const char *what, const struct push *p, int n, u32 restore)
{
	u64 total = 0;
	u32 ring_len = 0;
	u128 sum = 0;
	int i, ret = submit_args(p, n, restore, &total, &ring_len);

	if (ret)
		return;
	for (i = 0; i < n; i++) {
		CHECK((u128)p[i].offset + p[i].len <= p[i].bo_size,
		      "%s: push %d at %llx, %llx bytes accepted past its BO of %llx", what, i,
		      (unsigned long long)p[i].offset, (unsigned long long)p[i].len,
		      (unsigned long long)p[i].bo_size);
		sum += p[i].len;
	}
	CHECK(sum <= MAX_SUBMIT && total == sum, "%s: total %llx accepted (the pushes add up to %s%llx)",
	      what, (unsigned long long)total, sum >> 64 ? "2^64 + " : "", (unsigned long long)sum);
	CHECK((u128)restore + sum == ring_len, "%s: ring length %x for a restore of %x and %llx of pushes",
	      what, ring_len, restore, (unsigned long long)total);
}

static void expect(const char *what, const struct push *p, int n, u32 restore, int want)
{
	u64 total;
	u32 ring_len;
	int ret = submit_args(p, n, restore, &total, &ring_len);

	CHECK(ret == want, "%s: %d, expected %d", what, ret, want);
	check_safe(what, p, n, restore);
}

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define BO	0x100000ULL	/* a 1 MiB GART push buffer, as Mesa's */

static void table_cases(void)
{
	const u64 E = ~0ULL;

	/* what Mesa sends: a few pushes from one or two push buffers, at most MAX_SUBMIT */
	struct push mesa[] = { { 0, 0x2000, BO }, { 0x2000, 0x10, BO }, { 0, 0x400, 2 * BO } };
	expect("Mesa's pushes", mesa, N(mesa), 0x300, 0);
	struct push full[] = { { 0, MAX_SUBMIT, BO } };
	expect("one push of MAX_SUBMIT", full, 1, 16 * 1024 * 4, 0);
	struct push to_end[] = { { BO - 0x100, 0x100, BO } };
	expect("a push up to the BO's end", to_end, 1, 0, 0);
	struct push empty[] = { { BO, 0, BO } };
	expect("an empty push at the BO's end", empty, 1, 0, 0);

	/* refused as before */
	struct push past[] = { { BO - 0x100, 0x104, BO } };
	expect("a push one word past the BO", past, 1, 0, -EINVAL);
	struct push odd[] = { { 2, 4, BO } };
	expect("an unaligned offset", odd, 1, 0, -EINVAL);
	struct push oddlen[] = { { 0, 6, BO } };
	expect("an unaligned length", oddlen, 1, 0, -EINVAL);
	struct push big[] = { { 0, MAX_SUBMIT, BO }, { 0, 4, BO } };
	expect("one word over MAX_SUBMIT", big, 2, 0, -E2BIG);

	/* sums at the u64 edge: the offset plus the length wraps, so does the total */
	struct push wrap_off[] = { { E - 3, 8, BO } };
	expect("offset 2^64-4, 8 bytes", wrap_off, 1, 0, -EINVAL);
	struct push wrap_len[] = { { 8, E - 7, BO } };
	expect("offset 8, 2^64-8 bytes", wrap_len, 1, 0, -EINVAL);
	struct push wrap_total[] = { { 8, E - 7, BO }, { 16, 0x10, BO } };
	expect("a total wrapping to 8", wrap_total, 2, 0, -EINVAL);
	struct push wrap_two[] = { { 4, E - 3, BO }, { 4, E - 3, BO }, { 0, 0x1000, BO } };
	expect("two pushes of 2^64-4 bytes", wrap_two, 3, 0, -EINVAL);

	/* the ring length: restore + total in the u32 ring_begin takes */
	struct push small[] = { { 0, 0x100, BO } };
	expect("a restore of 2^32-16", small, 1, 0xfffffff0u, -E2BIG);
	expect("a restore of 2^32-0x200", small, 1, 0xfffffe00u, 0);
	expect("a restore of 2^32-0x100", small, 1, 0xffffff00u, -E2BIG);
}

static u64 rnd(u64 *s)
{
	*s ^= *s << 13;
	*s ^= *s >> 7;
	*s ^= *s << 17;
	return *s;
}

/* an edge value: around 0, the BO's size, MAX_SUBMIT, 2^32, 2^63 or 2^64, or anything */
static u64 edge(u64 *s, u64 bo)
{
	static const u64 base[] = { 0, MAX_SUBMIT, 1ULL << 32, 1ULL << 63, 0 /* bo */, ~0ULL };
	u64 r = rnd(s), b = base[r % N(base)];
	int64_t d = (int64_t)((r >> 8) % 33) - 16;

	if (r % N(base) == 4)
		b = bo;
	if ((r >> 16) % 8 == 0)
		return rnd(s);
	return b + (u64)(d * 4) + ((r >> 24) % 16 == 0 ? (r >> 28) % 4 : 0);
}

static void random_cases(long rounds, u64 seed)
{
	long r, accepted = 0;
	u64 s = seed * 0x9e3779b97f4a7c15ULL + 1;

	for (r = 0; r < rounds; r++) {
		struct push p[8];
		int n = 1 + rnd(&s) % 8, i;
		u32 restore = rnd(&s) % 4 == 0 ? (u32)edge(&s, 0) : (u32)(rnd(&s) % (64 * 1024)) & ~3u;
		u64 total;
		u32 ring_len;
		char what[64];

		for (i = 0; i < n; i++) {
			u64 bo = rnd(&s) % 2 ? BO : ((rnd(&s) % 0x10000000) & ~0xfffULL);

			p[i].bo_size = bo;
			p[i].offset = rnd(&s) % 2 ? edge(&s, bo) : (rnd(&s) % (bo + 1)) & ~3ULL;
			p[i].len = rnd(&s) % 2 ? edge(&s, bo) : (rnd(&s) % (MAX_SUBMIT / n + 1)) & ~3ULL;
		}
		snprintf(what, sizeof(what), "random %ld", r);
		check_safe(what, p, n, restore);
		accepted += !submit_args(p, n, restore, &total, &ring_len);
	}
	CHECK(accepted > rounds / 50, "random: only %ld of %ld submits accepted", accepted, rounds);
	printf("random: %ld submits, %ld accepted\n", rounds, accepted);
}

/* apply_relocs()'s choice of the word a relocation patches in the push copied from push_off */
static bool reloc_word(u32 off, u64 push_off, u64 len, u64 *at)
{
#ifndef OLD_RELOC_U32
	return ps3rsx_reloc_word(off, push_off, len, at);
#else
	if (off < push_off || off + 4 > push_off + len)	/* off + 4: u32 */
		return false;
	*at = (off - push_off) / 4;
	return true;
#endif
}

/* A relocation that patches a word patches the one it names, inside the push */
static void check_reloc(const char *what, u32 off, u64 push_off, u64 len)
{
	u64 at = ~0ULL;

	if (!reloc_word(off, push_off, len, &at))
		return;
	CHECK(at < len / 4 && push_off + at * 4 == off,
	      "%s: relocation at %x patches word %llx of a push of %llx bytes at %llx", what, off,
	      (unsigned long long)at, (unsigned long long)len, (unsigned long long)push_off);
}

static void expect_reloc(const char *what, u32 off, u64 push_off, u64 len, bool want, u64 want_at)
{
	u64 at = ~0ULL;
	bool got = reloc_word(off, push_off, len, &at);

	CHECK(got == want && (!want || at == want_at), "%s: %s word %llx, expected %s word %llx", what,
	      got ? "patches" : "skips", (unsigned long long)at, want ? "patches" : "skips",
	      (unsigned long long)want_at);
	check_reloc(what, off, push_off, len);
}

/*
 * A Mesa submit with relocations (NV30_PS3_RELOCS=1): three pushes from one push BO, each with
 * relocations of its own words (libdrm: reloc_bo_offset = the word's byte offset in the BO). Each
 * is applied to its own push's copy and no other.
 */
static void mesa_relocs(void)
{
	static const struct { u64 off, len; } push[] = { { 0, 0x1000 }, { 0x1000, 0x40 }, { 0x2400, 0x800 } };
	static const u32 rel[] = { 0x0, 0x8, 0xffc, 0x1000, 0x103c, 0x2400, 0x2bfc };
	u32 copy[3][0x400];
	int i, k, hits[N(rel)] = { 0 };

	memset(copy, 0, sizeof(copy));
	for (i = 0; i < N(push); i++)
		for (k = 0; k < N(rel); k++) {
			u64 at;

			CHECK(ps3rsx_reloc_args_check(rel[k]) == 0, "Mesa relocation at %x refused", rel[k]);
			if (!reloc_word(rel[k], push[i].off, push[i].len, &at))
				continue;
			CHECK(at < push[i].len / 4, "Mesa relocation at %x: word %llx past push %d", rel[k],
			      (unsigned long long)at, i);
			if (at < push[i].len / 4)
				copy[i][at] = 0xfeed0000 + k;
			hits[k]++;
		}
	for (k = 0; k < N(rel); k++) {
		int p = rel[k] < 0x1000 ? 0 : rel[k] < 0x2400 ? 1 : 2;

		CHECK(hits[k] == 1 && copy[p][(rel[k] - push[p].off) / 4] == 0xfeed0000u + k,
		      "Mesa relocation at %x: applied %d times, push %d word %llx is %x", rel[k], hits[k], p,
		      (unsigned long long)((rel[k] - push[p].off) / 4), copy[p][(rel[k] - push[p].off) / 4]);
	}
}

static void reloc_cases(void)
{
	/* the u32 edge: offset + 4 wraps to 0..3 */
	expect_reloc("relocation at 2^32-4, push at 0", 0xfffffffcu, 0, 0x1000, false, 0);
	expect_reloc("relocation at 2^32-4, push at 0x100", 0xfffffffcu, 0x100, 0x1000, false, 0);
	expect_reloc("relocation at 2^32-4 in a push ending at 2^32", 0xfffffffcu, 0xfffff000u, 0x1000,
		     true, 0x3ff);
	expect_reloc("relocation at 2^32-8 in a push ending at 2^32", 0xfffffff8u, 0xfffff000u, 0x1000,
		     true, 0x3fe);
	expect_reloc("relocation at 2^32-4, push at 2^32", 0xfffffffcu, 1ULL << 32, 0x1000, false, 0);
	/* the push's own edges */
	expect_reloc("relocation at the push's first word", 0x100, 0x100, 0x10, true, 0);
	expect_reloc("relocation at the push's last word", 0x10c, 0x100, 0x10, true, 3);
	expect_reloc("relocation just past the push", 0x110, 0x100, 0x10, false, 0);
	expect_reloc("relocation just before the push", 0xfc, 0x100, 0x10, false, 0);
	expect_reloc("relocation in an empty push", 0x100, 0x100, 0, false, 0);
	/* not a whole word: refused up front, never applied */
	CHECK(ps3rsx_reloc_args_check(0x102) == -EINVAL, "relocation at 0x102 accepted");
	CHECK(ps3rsx_reloc_args_check(0xffffffffu) == -EINVAL, "relocation at 2^32-1 accepted");
	CHECK(ps3rsx_reloc_args_check(0x104) == 0, "relocation at 0x104 refused");
	expect_reloc("relocation at 0x102", 0x102, 0x100, 0x10, false, 0);
	mesa_relocs();
}

static void random_relocs(long rounds, u64 seed)
{
	long r;
	u64 s = seed * 0x2545f4914f6cdd1dULL + 7;

	for (r = 0; r < rounds; r++) {
		u64 push_off = rnd(&s) % 2 ? edge(&s, 1ULL << 32) : (rnd(&s) % (1ULL << 33)) & ~3ULL;
		u64 len = rnd(&s) % 2 ? edge(&s, MAX_SUBMIT) & 0xfffffffcULL : (rnd(&s) % MAX_SUBMIT) & ~3ULL;
		u32 off = rnd(&s) % 2 ? (u32)edge(&s, push_off) : (u32)(push_off + (rnd(&s) % (len + 64)));

		check_reloc("random relocation", off, push_off, len);
	}
}

int main(int argc, char **argv)
{
	long rounds = argc > 1 ? atol(argv[1]) : 1000000;
	u64 seed = argc > 2 ? strtoull(argv[2], NULL, 0) : 1;

	reloc_cases();
	table_cases();
	random_cases(rounds, seed);
	random_relocs(rounds, seed);
	printf("pushbuf-test: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
