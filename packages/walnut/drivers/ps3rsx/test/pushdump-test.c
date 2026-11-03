// SPDX-License-Identifier: GPL-2.0
/*
 * The push dump's scanner and record format (src/pushdump_scan.h), on the host:
 *   pushdump-test		the scanner on synthetic streams, the record builders' layout
 *   pushdump-test DUMP		also writes a sample dump (every record type, with the
 *				builders pushdump.c uses) for pushdump-test.sh, which splits it
 *				with pushdump-split.py and runs the parts through validate-capture
 */
#include "shim.h"
#include "../src/pushdump_scan.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static int fails;

#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

#define H(n, sc, m)	(((u32)(n) << 18) | ((sc) << 13) | (m))		/* incrementing */
#define HN(n, sc, m)	(0x40000000u | H(n, sc, m))			/* non-incrementing */
#define SC 7								/* Mesa's 3D subchannel */

static const struct pd_chan_state zero_state;

static void scan(struct pd_scan *s, struct pd_range *r, u32 cap, const struct pd_chan_state *st,
		 const u32 *w, u32 n)
{
	pd_scan_begin(s, st, r, cap, 4096);
	pd_scan_push(s, w, n);
}

static const struct pd_range *find(const struct pd_scan *s, u8 what, u32 base)
{
	for (u32 i = 0; i < s->n; i++)
		if (s->r[i].what == what && s->r[i].base == base)
			return &s->r[i];
	return NULL;
}

/* A draw as Mesa sends it: bind 3D, a GART program, a u16 index buffer in GART, two batches */
static const u32 draw[] = {
	H(1, SC, 0), 0x31337000,
	H(1, SC, 0x08e4), 0x00100002,			/* FP at GART 0x100000 */
	H(2, SC, 0x181c), 0x00200000, 0x00000011,	/* index buffer GART 0x200000, u16 */
	H(1, SC, 0x1808), 5,				/* BEGIN */
	HN(2, SC, 0x1824), (2u << 24) | 0, (0u << 24) | 10,	/* indices 0-2 and 10 */
	H(1, SC, 0x1808), 0,				/* END */
};

static void test_scanner(void)
{
	struct pd_range r[PD_MAX_RANGES];
	const struct pd_range *x;
	struct pd_scan s;

	/* the draw: one program, one merged index range */
	scan(&s, r, PD_MAX_RANGES, &zero_state, draw, ARRAY_SIZE(draw));
	CHECK(s.n == 2, "draw: %u ranges", s.n);
	x = find(&s, PD_FP, 0x100000);
	CHECK(x && x->dom == PD_DOM_GART && x->lo == 0x100000 && x->hi == 0x100000 + 4096,
	      "draw: the program's range");
	x = find(&s, PD_INDEX, 0x200000);
	CHECK(x && x->dom == PD_DOM_GART && x->isize == 2 && x->lo == 0x200000 && x->hi == 0x200016,
	      "draw: the index range %llx-%llx", x ? (unsigned long long)x->lo : 0,
	      x ? (unsigned long long)x->hi : 0);
	CHECK(s.st.bound[SC] == 0x31337000 && s.st.idx_off == 0x200000 && s.st.idx_fmt == 0x11,
	      "draw: the state after it");

	/* the state carries into the next submit: a batch with no index buffer words */
	{
		struct pd_chan_state st = s.st;
		static const u32 more[] = {
			H(1, SC, 0x1808), 4, HN(1, SC, 0x1824), (255u << 24) | 0x1000,
			H(1, SC, 0x1808), 0,
		};

		scan(&s, r, PD_MAX_RANGES, &st, more, ARRAY_SIZE(more));
		x = find(&s, PD_INDEX, 0x200000);
		CHECK(s.n == 1 && x && x->lo == 0x200000 + 0x2000 && x->hi == 0x200000 + 0x2200,
		      "carried state: one batch of 256 from 0x1000");
	}

	/* no index buffer set yet: nothing; u32 indices in VRAM */
	{
		static const u32 w[] = {
			H(1, 3, 0), 0x31337000, HN(1, 3, 0x1824), 0x00000005,
			H(2, 3, 0x181c), 0x00001000, 0x00000000, HN(1, 3, 0x1824), (1u << 24) | 3,
		};

		scan(&s, r, PD_MAX_RANGES, &zero_state, w, ARRAY_SIZE(w));
		x = find(&s, PD_INDEX, 0x1000);
		CHECK(s.n == 1 && x && x->dom == PD_DOM_VRAM && x->isize == 4 && x->lo == 0x100c &&
		      x->hi == 0x1014, "u32 VRAM indices, the batch before the buffer ignored");
	}

	/* programs: location 1 is VRAM, 0 and 3 are no fetch; repeated binds merge */
	{
		static const u32 w[] = {
			H(1, SC, 0), 0x31337000,
			H(1, SC, 0x08e4), 0x00004001, H(1, SC, 0x08e4), 0x00005000,
			H(1, SC, 0x08e4), 0x00006003, H(1, SC, 0x08e4), 0x00004001,
		};

		scan(&s, r, PD_MAX_RANGES, &zero_state, w, ARRAY_SIZE(w));
		x = find(&s, PD_FP, 0x4000);
		CHECK(s.n == 1 && x && x->dom == PD_DOM_VRAM, "FP locations: %u ranges", s.n);
	}

	/* methods on other objects, and a rebind away from 3D, are not 3D methods */
	{
		static const u32 w[] = {
			H(1, 2, 0), 0x31337303, H(1, 2, 0x08e4), 0x00100002,
			H(1, SC, 0), 0x31337000, H(1, SC, 0), 0x31337303,
			H(2, SC, 0x181c), 0x00200000, 0x00000011, HN(1, SC, 0x1824), 0,
		};

		scan(&s, r, PD_MAX_RANGES, &zero_state, w, ARRAY_SIZE(w));
		CHECK(s.n == 0 && !s.st.idx_off_set, "non-3D objects: %u ranges", s.n);
	}

	/* an incrementing burst through 0x181c, 0x1820 and 0x1824 */
	{
		static const u32 w[] = {
			H(1, SC, 0), 0x31337000,
			H(3, SC, 0x181c), 0x00300000, 0x00000001, (3u << 24) | 4,
		};

		scan(&s, r, PD_MAX_RANGES, &zero_state, w, ARRAY_SIZE(w));
		x = find(&s, PD_INDEX, 0x300000);
		CHECK(s.n == 1 && x && x->dom == PD_DOM_GART && x->isize == 4 && x->lo == 0x300010 &&
		      x->hi == 0x300020, "incrementing burst into a batch");
	}

	/* more distinct ranges than fit: counted, none written past the array */
	{
		static u32 w[2 + 2 * 100];
		u32 n = 0;

		w[n++] = H(1, SC, 0);
		w[n++] = 0x31337000;
		for (u32 i = 0; i < 100; i++) {
			w[n++] = H(1, SC, 0x08e4);
			w[n++] = (0x1000 + i) << 8 | 2;
		}
		scan(&s, r, 8, &zero_state, w, n);
		CHECK(s.n == 8 && s.dropped == 92, "cap: %u kept, %u dropped", s.n, s.dropped);
		scan(&s, NULL, 0, &zero_state, w, n);	/* the state-only scan */
		CHECK(s.n == 0 && s.dropped == 100 && s.st.bound[SC] == 0x31337000, "state only");
	}

	/* a JMP or a short burst ends the scan (the validator never passes either) */
	{
		static const u32 w[] = {
			H(1, SC, 0), 0x31337000, 0x20001000, H(1, SC, 0x08e4), 0x00100002,
		};
		static const u32 shortw[] = { H(1, SC, 0), 0x31337000, H(4, SC, 0x08e4), 0x00100002 };

		scan(&s, r, PD_MAX_RANGES, &zero_state, w, ARRAY_SIZE(w));
		CHECK(s.n == 0, "JMP ends the scan");
		scan(&s, r, PD_MAX_RANGES, &zero_state, shortw, ARRAY_SIZE(shortw));
		CHECK(s.n == 0, "a short burst ends the scan");
	}
}

static void test_records(void)
{
	u8 b[256];

	pd_put32(b, 0x01020304);
	CHECK(b[0] == 1 && b[3] == 4 && pd_get32(b) == 0x01020304, "big-endian words");
	pd_rec_submit(b, 7, 3, 9, 11, 1234, PD_REJECTED, 2, 5, 4, "glxgears\0\0\0\0\0\0\0");
	CHECK(pd_get32(b) == PD_MAGIC && pd_get32(b + 4) == PD_SUBMIT &&
	      pd_get32(b + 8) == PD_SUBMIT_BYTES / 4 + 4 * PD_BO_BYTES / 4 && pd_get32(b + 12) == 7,
	      "submit header");
	CHECK(pd_get32(b + 16) == 3 && pd_get32(b + 36) == 2 && pd_get32(b + 44) == 4 &&
	      !memcmp(b + 48, "glxgears", 9), "submit fields");
	pd_rec_content(b, 1, 0, PD_FP, PD_DOM_VRAM, 0x4000, 4096, PD_READ_CPU, 5);
	CHECK(pd_get32(b + 8) == PD_CONTENT_BYTES / 4 + 2 && pd_get32(b + 40) == 5,
	      "content length rounds to words");
	pd_rec_bo_event(b, false, 1, 2, 3, 2, PD_DOM_GART, 0x123456789aULL, 0x1000);
	CHECK(pd_get32(b + 4) == PD_CLOSE && pd_get32(b + 32) == 0x12 &&
	      pd_get32(b + 36) == 0x3456789a, "bo event offset halves");
}

/* --- the sample dump ------------------------------------------------------------------------ */

static FILE *out;

static void put(const void *p, size_t n)
{
	if (fwrite(p, 1, n, out) != n)
		fails++;
}

static void put_words(const u32 *w, u32 n)
{
	for (u32 i = 0; i < n; i++) {
		u8 b[4];

		pd_put32(b, w[i]);
		put(b, 4);
	}
}

static void sample_push(u32 seq, u32 ch, u32 idx, const u32 *w, u32 n)
{
	u8 r[PD_PUSH_BYTES];

	pd_rec_push(r, seq, ch, idx, n);
	put(r, sizeof(r));
	put_words(w, n);
}

static void sample_submit(u32 seq, u32 ch, u32 verdict, u32 bad, u32 npush)
{
	u8 r[PD_SUBMIT_BYTES], bo[PD_BO_BYTES];

	pd_rec_submit(r, seq, ch, 100 + ch, 5, 4321, verdict, bad, npush, 2, "sample\0\0\0\0\0\0\0\0\0");
	put(r, sizeof(r));
	pd_rec_bo(bo, 1, 2, PD_DOM_GART, 0x100000, 0x10000, 4, 0);
	put(bo, sizeof(bo));
	pd_rec_bo(bo, 2, 2, PD_DOM_GART, 0x200000, 0x1000, 4, 0);
	put(bo, sizeof(bo));
}

static void write_sample(const char *path)
{
	static const u32 restore[] = { H(1, SC, 0x0300), 0 };
	static const u32 bad[] = { H(1, SC, 0), 0x31337000, H(1, SC, 0x1808), 0 };	/* END, none open */
	u8 r[64];

	out = fopen(path, "wb");
	if (!out) {
		perror(path);
		fails++;
		return;
	}
	pd_rec_start(r, 0, 0x900000, 0xfe00000, 0x4000000, 0xe000000, 0x200000);
	put(r, PD_START_BYTES);
	pd_rec_bo_event(r, true, 0, 5, 4321, 2, PD_DOM_GART, 0x100000, 0x10000);
	put(r, PD_BO_EVENT_BYTES);
	pd_rec_bo_event(r, true, 0, 5, 4321, 2, PD_DOM_GART, 0x200000, 0x1000);
	put(r, PD_BO_EVENT_BYTES);
	/* channel 1: an accepted draw with a restore before it */
	sample_submit(1, 1, PD_ACCEPTED, ~0u, 2);
	pd_rec_restore(r, 1, 1, ARRAY_SIZE(restore));
	put(r, PD_RESTORE_BYTES);
	put_words(restore, ARRAY_SIZE(restore));
	sample_push(1, 1, 0, draw, 4);			/* bind, program */
	sample_push(1, 1, 1, draw + 4, ARRAY_SIZE(draw) - 4 - 2);	/* its END comes later */
	pd_rec_content(r, 1, 1, PD_FP, PD_DOM_GART, 0x100000, 4096, PD_READ_CPU, 6);
	put(r, PD_CONTENT_BYTES);
	put("PROG!\0\0\0", 8);
	pd_rec_content(r, 1, 1, PD_INDEX, PD_DOM_GART, 0x200000, 0x16, PD_UNREADABLE, 0);
	put(r, PD_CONTENT_BYTES);
	/* channel 2: a rejected submit; records lost; ps3rsx ends channel 1's open primitive */
	sample_submit(2, 2, PD_REJECTED, 0, 1);
	sample_push(2, 2, 0, bad, ARRAY_SIZE(bad));
	pd_rec_gap(r, 3, 7);
	put(r, PD_GAP_BYTES);
	pd_rec_end(r, 3, 1, H(1, SC, 0x1808));
	put(r, PD_END_BYTES);
	pd_rec_bo_event(r, false, 4, 5, 4321, 2, PD_DOM_GART, 0x200000, 0x1000);
	put(r, PD_BO_EVENT_BYTES);
	fclose(out);
}

int main(int argc, char **argv)
{
	test_scanner();
	test_records();
	if (argc > 1)
		write_sample(argv[1]);
	if (fails) {
		printf("pushdump-test: %d FAILED\n", fails);
		return 1;
	}
	printf("pushdump-test: ok\n");
	return 0;
}
