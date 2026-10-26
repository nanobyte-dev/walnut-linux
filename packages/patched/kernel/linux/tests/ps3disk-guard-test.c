/* ps3disk-guard-test: the write guard's range logic (local/0045, drivers/block/ps3disk_guard.h)
 * compiled in userspace: parsing of /sys/block/ps3db/write_guard/ranges, the seal's sort and merge,
 * the containment check queue_rq runs for every bio, and the policy decision before it.
 * Fixed cases first, then random ones against a bitmap of the sectors (seeded, reproducible).
 *
 *   ps3disk-guard-test [ROUNDS [SEED]]
 *
 * packages/patched/kernel/linux/tests/ps3disk-guard.sh extracts the header from the patch and builds this. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t u64;
#include "ps3disk_guard.h"

static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

static int parse(const char *s, u64 cap, u64 align, struct ps3disk_range *r, unsigned int room, unsigned int *n)
{
	return ps3disk_guard_parse(s, strlen(s), cap, align, r, room, n);
}

static void t_parse(void)
{
	struct ps3disk_range r[8];
	unsigned int n;
	int e;
	static const struct { const char *in; int err; unsigned int n; u64 s0, e0; } c[] = {
		{ "100 8\n", 0, 1, 100, 108 },
		{ "100 8", -EINVAL, 0, 0, 0 },				/* no final newline */
		{ "1 2\n123456 10", -EINVAL, 0, 0, 0 },		/* "123456 1024\n" cut by sysfs */
		{ "1 2\n1234", -EINVAL, 0, 0, 0 },
		{ "1 2\n", 0, 1, 1, 3 },				/* the same cut at a line end */
		{ "", 0, 0, 0, 0 },
		{ "\n\n", 0, 0, 0, 0 },
		{ "  \t\n", 0, 0, 0, 0 },
		{ " 0\t1 \n", 0, 1, 0, 1 },				/* blanks around and between */
		{ "1 2\n3 4\n\n5 6\n", 0, 3, 1, 3 },
		{ "999 1\n", 0, 1, 999, 1000 },				/* the last sector */
		{ "0 1000\n", 0, 1, 0, 1000 },				/* the whole region */
		{ "100\n", -EINVAL, 0, 0, 0 },				/* no length */
		{ "100 \n", -EINVAL, 0, 0, 0 },
		{ "100 0\n", -EINVAL, 0, 0, 0 },			/* empty range */
		{ "1000 1\n", -EINVAL, 0, 0, 0 },			/* starts at the end */
		{ "999 2\n", -EINVAL, 0, 0, 0 },			/* runs past it */
		{ "0 1001\n", -EINVAL, 0, 0, 0 },
		{ "1 2 3\n", -EINVAL, 0, 0, 0 },			/* three fields */
		{ "1,2\n", -EINVAL, 0, 0, 0 },
		{ "1 2,\n", -EINVAL, 0, 0, 0 },
		{ "-1 2\n", -EINVAL, 0, 0, 0 },
		{ "+1 2\n", -EINVAL, 0, 0, 0 },
		{ "0x10 2\n", -EINVAL, 0, 0, 0 },			/* decimal only */
		{ "1 0x2\n", -EINVAL, 0, 0, 0 },
		{ "1e3 2\n", -EINVAL, 0, 0, 0 },
		{ "a 1\n", -EINVAL, 0, 0, 0 },
		{ "1 2\r\n", -EINVAL, 0, 0, 0 },
		{ "1 2\n3\n", -EINVAL, 0, 0, 0 },			/* a bad line anywhere refuses all */
		{ "1 2\nx y\n", -EINVAL, 0, 0, 0 },
		{ "18446744073709551616 1\n", -EINVAL, 0, 0, 0 },	/* 2^64 */
		{ "99999999999999999999999 1\n", -EINVAL, 0, 0, 0 },
		{ "010 2\n", 0, 1, 10, 12 },				/* leading zeros are decimal, not octal */
	};
	for (unsigned int i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
		n = 12345;
		e = parse(c[i].in, 1000, 1, r, 8, &n);
		CHECK(e == c[i].err, "parse %d: \"%s\" gave %d, want %d", i, c[i].in, e, c[i].err);
		if (!e && !c[i].err) {
			CHECK(n == c[i].n, "parse %d: %u ranges, want %u", i, n, c[i].n);
			if (n && c[i].n)
				CHECK(r[0].start == c[i].s0 && r[0].end == c[i].e0, "parse %d: [%llu, %llu)", i,
				      (unsigned long long)r[0].start, (unsigned long long)r[0].end);
		}
	}
	/* a NUL inside the buffer is not a separator */
	e = ps3disk_guard_parse("1 2\0" "3 4\n", 8, 1000, 1, r, 8, &n);
	CHECK(e == -EINVAL, "embedded NUL: %d", e);
	/* the largest values: capacity 2^64 - 1 */
	e = parse("18446744073709551614 1\n", ~(u64)0, 1, r, 8, &n);
	CHECK(!e && n == 1 && r[0].end == ~(u64)0, "u64 max: %d", e);
	e = parse("18446744073709551614 2\n", ~(u64)0, 1, r, 8, &n);
	CHECK(e == -EINVAL, "u64 overflow of start + len: %d", e);
	e = parse("1 18446744073709551615\n", ~(u64)0, 1, r, 8, &n);
	CHECK(e == -EINVAL, "u64 len past the end: %d", e);
	/* room */
	e = parse("1 1\n2 1\n3 1\n", 1000, 1, r, 3, &n);
	CHECK(!e && n == 3, "room 3 of 3: %d", e);
	e = parse("1 1\n2 1\n3 1\n4 1\n", 1000, 1, r, 3, &n);
	CHECK(e == -ENOSPC, "room 3 of 4: %d", e);
	e = parse("", 1000, 1, r, 0, &n);
	CHECK(!e && n == 0, "room 0, empty: %d", e);
	e = parse("1 1\n", 1000, 1, r, 0, &n);
	CHECK(e == -ENOSPC, "room 0: %d", e);
	/* alignment to the device block (4096-byte blocks: 8 sectors) */
	e = parse("8 16\n", 1000, 8, r, 8, &n);
	CHECK(!e && n == 1, "aligned: %d", e);
	e = parse("4 8\n", 1000, 8, r, 8, &n);
	CHECK(e == -EINVAL, "start unaligned: %d", e);
	e = parse("8 4\n", 1000, 8, r, 8, &n);
	CHECK(e == -EINVAL, "length unaligned: %d", e);
	e = parse("8 8\n", 1000, 0, r, 8, &n);
	CHECK(e == -EINVAL, "align 0: %d", e);
	/* the kernel's bound on the ranges in one write: k ranges take at least 4k bytes */
	{
		char buf[4096];
		size_t len = 0;
		static struct ps3disk_range big[4096];

		while (len + 4 <= sizeof(buf)) {
			memcpy(buf + len, "0 1\n", 4);
			len += 4;
		}
		e = ps3disk_guard_parse(buf, len, 1000, 1, big, len / 4 + 1, &n);
		CHECK(!e && n == len / 4, "dense page: %d, %u ranges in %zu bytes", e, n, len);
		e = ps3disk_guard_parse(buf, len, 1000, 1, big, len / 4 - 1, &n);
		CHECK(e == -ENOSPC, "dense page, one short: %d", e);
	}
}

static void t_normalize(void)
{
	struct ps3disk_range r[] = {
		{ 50, 60 }, { 10, 20 }, { 20, 30 }, { 15, 18 }, { 100, 101 }, { 55, 70 }, { 10, 20 }, { 0, 1 },
	};
	u64 sec = 0;
	unsigned int n = ps3disk_guard_normalize(r, 8, &sec);

	/* [0,1) [10,30) [50,70) [100,101) */
	CHECK(n == 4, "normalize: %u ranges", n);
	CHECK(r[0].start == 0 && r[0].end == 1, "r0");
	CHECK(r[1].start == 10 && r[1].end == 30, "r1 [%llu, %llu)", (unsigned long long)r[1].start,
	      (unsigned long long)r[1].end);
	CHECK(r[2].start == 50 && r[2].end == 70, "r2");
	CHECK(r[3].start == 100 && r[3].end == 101, "r3");
	CHECK(sec == 1 + 20 + 20 + 1, "sectors %llu", (unsigned long long)sec);
	n = ps3disk_guard_normalize(r, 0, &sec);
	CHECK(n == 0 && sec == 0, "normalize empty");
}

static void t_contains(void)
{
	struct ps3disk_range r[] = { { 100, 200 }, { 200, 300 }, { 400, 500 } };
	u64 sec;
	unsigned int n = ps3disk_guard_normalize(r, 3, &sec);	/* [100,300) [400,500) */

	CHECK(n == 2, "adjacent ranges merge: %u", n);
	CHECK(ps3disk_guard_contains(r, n, 100, 200), "exactly the merged range");
	CHECK(ps3disk_guard_contains(r, n, 190, 20), "across the old boundary at 200 (a merged request)");
	CHECK(!ps3disk_guard_contains(r, n, 99, 2), "one sector before");
	CHECK(!ps3disk_guard_contains(r, n, 299, 2), "one sector after");
	CHECK(ps3disk_guard_contains(r, n, 299, 1), "the last sector");
	CHECK(!ps3disk_guard_contains(r, n, 300, 1), "the first sector after");
	CHECK(!ps3disk_guard_contains(r, n, 290, 120), "over the gap into the next range");
	CHECK(ps3disk_guard_contains(r, n, 400, 100), "the second range");
	CHECK(!ps3disk_guard_contains(r, n, 0, 1), "before every range");
	CHECK(!ps3disk_guard_contains(r, n, 500, 1), "after every range");
	CHECK(!ps3disk_guard_contains(r, n, 150, 0), "empty run");
	CHECK(!ps3disk_guard_contains(r, n, 150, ~(u64)0), "wrapping run");
	CHECK(!ps3disk_guard_contains(r, 0, 150, 1), "no ranges");
	{
		struct ps3disk_range top[] = { { ~(u64)0 - 8, ~(u64)0 } };

		CHECK(ps3disk_guard_contains(top, 1, ~(u64)0 - 8, 8), "at the top of u64");
		CHECK(!ps3disk_guard_contains(top, 1, ~(u64)0 - 8, 9), "past the top of u64");
	}
}

static void t_decide(void)
{
	/* reads and flushes without data pass everywhere, sealed or not */
	for (int p = 0; p < 3; p++)
		for (int s = 0; s < 2; s++)
			CHECK(ps3disk_guard_decide(p, s, true) == PS3DISK_GUARD_ALLOW, "harmless %d %d", p, s);
	CHECK(ps3disk_guard_decide(PS3DISK_GUARD_WRITABLE, false, false) == PS3DISK_GUARD_ALLOW, "writable");
	CHECK(ps3disk_guard_decide(PS3DISK_GUARD_READONLY, false, false) == PS3DISK_GUARD_REFUSE_READONLY, "ro");
	CHECK(ps3disk_guard_decide(PS3DISK_GUARD_READONLY, true, false) == PS3DISK_GUARD_REFUSE_READONLY,
	      "ro, sealed (no seal makes it writable)");
	CHECK(ps3disk_guard_decide(PS3DISK_GUARD_WHITELIST, false, false) == PS3DISK_GUARD_REFUSE_UNSEALED,
	      "whitelist before the seal");
	CHECK(ps3disk_guard_decide(PS3DISK_GUARD_WHITELIST, true, false) == PS3DISK_GUARD_CHECK_RANGES,
	      "whitelist after the seal");
	CHECK(ps3disk_guard_decide(7, true, false) == PS3DISK_GUARD_REFUSE_READONLY, "unknown policy fails closed");
}

/* xorshift64*: reproducible on every libc */
static u64 rs;
static u64 rnd(void)
{
	rs ^= rs >> 12;
	rs ^= rs << 25;
	rs ^= rs >> 27;
	return rs * 0x2545f4914f6cdd1dULL;
}

#define SPACE 4096

/* random registrations as text (several writes, as the boot tool sends them), the seal, then
 * random runs against a bitmap of the registered sectors */
static void t_random(unsigned int rounds)
{
	static struct ps3disk_range r[PS3DISK_GUARD_MAX_RANGES];
	static unsigned char bit[SPACE];
	char buf[4096];

	for (unsigned int round = 0; round < rounds; round++) {
		unsigned int total = 0, writes = 1 + rnd() % 4, maxlen = 1 + rnd() % 64;
		u64 align = (rnd() % 4) ? 1 : 8, sec = 0, want = 0;

		memset(bit, 0, sizeof(bit));
		for (unsigned int w = 0; w < writes; w++) {
			unsigned int k = rnd() % 40, n = 0;
			size_t len = 0;

			for (unsigned int i = 0; i < k; i++) {
				u64 s = rnd() % SPACE / align * align, l = (1 + rnd() % maxlen) * align;

				if (s + l > SPACE)
					l = (SPACE - s) / align * align;
				if (!l)
					continue;
				len += snprintf(buf + len, sizeof(buf) - len, "%s%llu%s%llu\n", rnd() % 2 ? "" : " ",
						(unsigned long long)s, rnd() % 2 ? " " : "\t", (unsigned long long)l);
				for (u64 x = s; x < s + l; x++)
					bit[x] = 1;
			}
			int e = ps3disk_guard_parse(buf, len, SPACE, align, r + total,
						    PS3DISK_GUARD_MAX_RANGES - total, &n);
			CHECK(!e, "round %u: parse %d", round, e);
			total += n;
		}
		unsigned int n = ps3disk_guard_normalize(r, total, &sec);

		for (unsigned int x = 0; x < SPACE; x++)
			want += bit[x];
		CHECK(sec == want, "round %u: %llu sectors, bitmap %llu", round, (unsigned long long)sec,
		      (unsigned long long)want);
		for (unsigned int i = 0; i < n; i++) {
			CHECK(r[i].start < r[i].end, "round %u: empty range %u", round, i);
			CHECK(!i || r[i].start > r[i - 1].end, "round %u: ranges %u, %u not sorted and apart",
			      round, i - 1, i);
		}
		for (unsigned int q = 0; q < 2000; q++) {
			u64 pos = rnd() % (SPACE + 16), l = rnd() % 130, x;
			bool in = l > 0 && pos + l <= SPACE;

			for (x = pos; in && x < pos + l; x++)
				in = bit[x];
			bool got = ps3disk_guard_contains(r, n, pos, l);

			CHECK(got == in, "round %u: [%llu, +%llu) gave %d, bitmap %d", round, (unsigned long long)pos,
			      (unsigned long long)l, got, in);
			if (failures > 20)
				return;
		}
	}
}

/* the kernel's limit: 65536 ranges, sorted in place without recursion */
static void t_many(void)
{
	static struct ps3disk_range r[PS3DISK_GUARD_MAX_RANGES];
	u64 sec;

	for (unsigned int i = 0; i < PS3DISK_GUARD_MAX_RANGES; i++) {
		u64 s = (u64)(PS3DISK_GUARD_MAX_RANGES - 1 - i) * 16;	/* reversed, 8 used of every 16 */

		r[i].start = s;
		r[i].end = s + 8;
	}
	unsigned int n = ps3disk_guard_normalize(r, PS3DISK_GUARD_MAX_RANGES, &sec);

	CHECK(n == PS3DISK_GUARD_MAX_RANGES && sec == 8ULL * PS3DISK_GUARD_MAX_RANGES, "many: %u", n);
	CHECK(ps3disk_guard_contains(r, n, 16ULL * 40000, 8), "many: inside");
	CHECK(!ps3disk_guard_contains(r, n, 16ULL * 40000 + 4, 8), "many: into the gap");
	CHECK(!ps3disk_guard_contains(r, n, 16ULL * 40000 + 8, 1), "many: the gap");
}

int main(int argc, char **argv)
{
	unsigned int rounds = argc > 1 ? strtoul(argv[1], NULL, 0) : 2000;

	rs = argc > 2 ? strtoull(argv[2], NULL, 0) : 0x9e3779b97f4a7c15ULL;
	if (!rs)
		rs = 1;
	t_parse();
	t_normalize();
	t_contains();
	t_decide();
	t_many();
	t_random(rounds);
	if (failures) {
		printf("%d failures\n", failures);
		return 1;
	}
	printf("ok: fixed cases, 65536 ranges, %u random rounds\n", rounds);
	return 0;
}
