// SPDX-License-Identifier: GPL-2.0
/*
 * The validator's address checks, slices a and b (validate.c's p4_method(), p4.h's set),
 * against rules spelled out here independently of validate.c:
 * - the set: inserts, duplicate handles, removes, finds at every edge, overlaps refused, and a
 *   random run against a list model;
 * - slot rules: every context DMA handle in every slot of every class;
 * - binds of the second M2MF and image from CPU refused;
 * - the fence label, query slice and render-enable forms of channels 0, 1 and 13;
 * - draws: render targets and zeta inside the file's buffers (target 0 and zeta whatever
 *   RT_ENABLE and the depth state say), GART targets over the kernel's ring and zero area, the
 *   kept result (one check while the surface and the set stay), a change between draws, none
 *   inside a primitive, a buffer leaving the set before a draw or a continued primitive;
 * - M2MF copies, scaled images into both surface kinds and clears, each at its edges: the last
 *   byte of a buffer accepted, one past refused, adjacent buffers not joined, the wrong domain,
 *   zero sizes, coordinates above 4096, overflowing values, unknown formats;
 * - the valid bits: a trigger needs the channel's own writes; after a lost channel none counts;
 * - coverage (H2): every field is restored by the shadow at a channel switch;
 * - classification (L4): every allowlisted method is a field, a trigger, or on a list here;
 * - built with -DPS3RSX_P4_COUNT (p4-test-count): violations counted and let through with their
 *   state, test_p4_enforce refusing per slice, and P7 inside an open primitive.
 * fix-off.sh builds it again with each rule off and expects its FAIL line.
 */
#include "shim.h"
#define PS3RSX_H	/* the driver header: not in userspace */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static char last_warn[256];
#define pr_warn(...) snprintf(last_warn, sizeof(last_warn), __VA_ARGS__)
#define pr_warn_ratelimited pr_warn

#ifndef P4_H
#define P4_H "../src/p4.h"
#endif
#include P4_H
bool ps3rsx_known_object(u32 handle);
int ps3rsx_validate(struct ps3rsx_vstate *vs, const u32 *w, u32 nwords);
void ps3rsx_validate_init(void);
#ifndef VALIDATE_C
#define VALIDATE_C "../src/validate.c"
#endif
#include VALIDATE_C
#define RESTORE_MAX_WORDS	(16 * 1024)
#ifndef SHADOW_C
#define SHADOW_C "../src/shadow.c"
#endif
#include SHADOW_C

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); \
	printf(" (%s)\n", last_warn); } } while (0)

#define H(n, sc, m)	(((u32)(n) << 18) | ((u32)(sc) << 13) | (m))
#define NI		0x40000000u

/* Subchannels as Mesa binds them */
#define S3D	7
#define SM2MF	2
#define S2D	3
#define SSWZ	4
#define SSIFM	5

/* The file's buffers: VRAM A and B adjacent, VRAM C, GART G */
#define A_LO	0x01000000u
#define A_SZ	0x00100000u
#define B_LO	(A_LO + A_SZ)
#define B_SZ	0x00010000u
#define C_LO	0x08000000u
#define C_SZ	0x00400000u
#define G_LO	0x00400000u
#define G_SZ	0x00100000u

static struct p4set set;

static void set_init(void)
{
	p4set_free(&set);
	p4set_insert(&set, P4_VRAM, A_LO, A_SZ);
	p4set_insert(&set, P4_VRAM, B_LO, B_SZ);
	p4set_insert(&set, P4_VRAM, C_LO, C_SZ);
	p4set_insert(&set, P4_GART, G_LO, G_SZ);
}

static void vs_init(struct ps3rsx_vstate *vs, u8 chan)
{
	memset(vs, 0, sizeof(*vs));
	vs->chan_id = chan;
	vs->set = &set;
}

/* A stream on a fresh channel 2: 0 or the error */
static int run(const u32 *w, u32 n)
{
	struct ps3rsx_vstate vs;

	vs_init(&vs, 2);
	last_warn[0] = 0;
	return ps3rsx_validate(&vs, w, n);
}

/* --- the set ------------------------------------------------------------------------------ */

static void test_set(void)
{
	struct p4set s = { 0 };
	int i;

	CHECK(!p4set_find(&s, P4_VRAM, 0, 1), "set: empty set finds a range");
	CHECK(p4set_insert(&s, P4_VRAM, 0x10000, 0x1000) == 0, "set: insert refused");
	CHECK(p4set_insert(&s, P4_VRAM, 0x10000, 0x1000) == 0, "set: second handle refused");
	CHECK(p4set_insert(&s, P4_VRAM, 0x10000, 0x2000) == -EINVAL, "set: same start other size");
	CHECK(p4set_insert(&s, P4_VRAM, 0x10800, 0x1000) == -EINVAL, "set: overlap at the end");
	CHECK(p4set_insert(&s, P4_VRAM, 0xf800, 0x1000) == -EINVAL, "set: overlap at the start");
	CHECK(p4set_insert(&s, P4_VRAM, 0x11000, 0x1000) == 0, "set: adjacent refused");
	CHECK(p4set_insert(&s, P4_VRAM, 0xf000, 0x1000) == 0, "set: adjacent below refused");
	CHECK(p4set_insert(&s, P4_VRAM, 0x20000, 0) == -EINVAL, "set: empty BO accepted");
	CHECK(p4set_find(&s, P4_VRAM, 0x10000, 0x11000), "set: whole BO not found");
	CHECK(p4set_find(&s, P4_VRAM, 0x10fff, 0x11000), "set: last byte not found");
	CHECK(!p4set_find(&s, P4_VRAM, 0x10fff, 0x11001), "set: range across two BOs found");
	CHECK(!p4set_find(&s, P4_VRAM, 0x10000, 0x10000), "set: empty range found");
	CHECK(!p4set_find(&s, P4_GART, 0x10000, 0x10001), "set: wrong domain found");
	CHECK(!p4set_find(&s, P4_VRAM, 0x12000, 0x12001), "set: past the last BO found");
	CHECK(!p4set_find(&s, P4_VRAM, 0xe000, 0xe001), "set: below the first BO found");
	CHECK(!p4set_find(&s, -1, 0x10000, 0x10001), "set: domain -1 found");
	CHECK(!p4set_remove(&s, P4_VRAM, 0x10000), "set: first of two handles removed the BO");
	CHECK(p4set_find(&s, P4_VRAM, 0x10000, 0x11000), "set: BO gone with a handle left");
	CHECK(p4set_remove(&s, P4_VRAM, 0x10000), "set: last handle kept the BO");
	CHECK(!p4set_find(&s, P4_VRAM, 0x10000, 0x10001), "set: removed BO still found");
	CHECK(!p4set_remove(&s, P4_VRAM, 0x10000), "set: removed twice");
	CHECK(p4set_find(&s, P4_VRAM, 0x11000, 0x12000) && p4set_find(&s, P4_VRAM, 0xf000, 0x10000),
	      "set: neighbours lost");
	p4set_free(&s);

	/* random inserts and removes against a list of live BOs, then every lookup's edges */
	{
		struct { u64 lo, hi; int h; bool live; } m[64];
		u32 seed = 7;

		for (i = 0; i < 64; i++)
			m[i] = (typeof(m[0])){ 0x100000ULL * i + 0x1000 * (i % 3),
					       0x100000ULL * i + 0x1000 * (i % 3) + 0x1000 * (1 + i % 7),
					       0, false };
		for (i = 0; i < 200000; i++) {
			int k;

			seed = seed * 1103515245 + 12345;
			k = (seed >> 16) % 64;
			if ((seed >> 8) & 1) {
				CHECK(p4set_insert(&s, P4_GART, m[k].lo, m[k].hi - m[k].lo) == 0,
				      "set: random insert %d refused", k);
				m[k].h++;
			} else if (m[k].h) {
				m[k].h--;
				CHECK(p4set_remove(&s, P4_GART, m[k].lo) == !m[k].h,
				      "set: random remove %d", k);
			}
			if ((i & 1023) == 0)
				for (int j = 0; j < 64; j++) {
					bool in = m[j].h > 0;

					CHECK(p4set_find(&s, P4_GART, m[j].lo, m[j].hi) == in &&
					      p4set_find(&s, P4_GART, m[j].hi - 1, m[j].hi) == in &&
					      !p4set_find(&s, P4_GART, m[j].lo, m[j].hi + 1) &&
					      !p4set_find(&s, P4_GART, m[j].lo - 1, m[j].lo + 1),
					      "set: random model, BO %d after %d steps", j, i);
				}
		}
		p4set_free(&s);
	}
}

/* --- slot rules and binds (slice a) --------------------------------------------------------- */

static const u32 handles7[] = { 0, 0xfeed0000, 0xfeed0001, 0x66604200, 0x66606660, 0x66616661,
				0x66626660 };

/* The rule, written out: which handles each slot of each class takes */
static bool slot_expect(int cls, u32 m, u32 h)
{
	bool v = h == 0xfeed0000, g = h == 0xfeed0001;

	switch (cls) {
	case VC_3D:
		switch (m) {
		case 0x180: return h == 0x66604200 || h == 0;
		case 0x184: case 0x19c: return v;
		case 0x188: case 0x1a0: return g;
		case 0x18c: case 0x194: case 0x198: case 0x1b4: case 0x1b8: return v || g;
		case 0x1a4: return h == 0x66606660;
		case 0x1a8: return h == 0x66626660;
		default: return h == 0;
		}
	case VC_M2MF:
	case VC_SURF2D:
		return m == 0x180 ? h == 0x66604200 : v || g;
	case VC_SWZ:
	case VC_SIFM:
		return m == 0x180 ? h == 0x66604200 : v || g;
	}
	return false;
}

static void test_slots(void)
{
	static const struct { int cls; u32 obj; u32 lo, hi; } c[] = {
		{ VC_3D, 0x31337000, 0x180, 0x1bc }, { VC_M2MF, 0x31337303, 0x180, 0x188 },
		{ VC_SURF2D, 0x313371c3, 0x180, 0x188 }, { VC_SWZ, 0x31337a73, 0x180, 0x184 },
		{ VC_SIFM, 0x3137af00, 0x180, 0x184 },
	};

	for (u32 k = 0; k < ARRAY_SIZE(c); k++)
		for (u32 m = c[k].lo; m <= c[k].hi; m += 4)
			for (u32 i = 0; i < ARRAY_SIZE(handles7); i++) {
				u32 w[4] = { H(1, 1, 0), c[k].obj, H(1, 1, m), handles7[i] };
				bool ok = slot_expect(c[k].cls, m, handles7[i]);
				int r = run(w, 4);

				CHECK((r == 0) == ok, "slot: class %d slot %03x handle %08x %s", c[k].cls, m,
				      handles7[i], ok ? "refused" : "accepted");
			}
	{
		u32 w[2] = { H(1, 1, 0), 0x3137c0de };

		CHECK(run(w, 2) != 0, "bind of 3137c0de accepted");
		w[1] = 0x31337808;
		CHECK(run(w, 2) != 0, "bind of 31337808 accepted");
	}
}

/* --- fence, report, render enable (slice a) ------------------------------------------------- */

static int run_chan(u8 chan, const u32 *w, u32 n)
{
	struct ps3rsx_vstate vs;

	vs_init(&vs, chan);
	last_warn[0] = 0;
	return ps3rsx_validate(&vs, w, n);
}

static void test_slots_own(void)
{
	static const u8 chans[] = { 0, 1, 13 };

	for (u32 k = 0; k < ARRAY_SIZE(chans); k++) {
		u8 ch = chans[k];
		u32 lab = 0x20 + 32 * ch, q = 0x1000 * ch;
		const u32 offs[] = { lab - 4, lab, lab + 2, lab + 28, lab + 32, 0xfffffffcu, 0 };

		for (u32 i = 0; i < ARRAY_SIZE(offs); i++) {
			bool ok = offs[i] >= lab && offs[i] + 4ULL <= lab + 32 && !(offs[i] & 3);
			u32 w[8] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x1a4), 0x66606660,
				     H(2, S3D, 0x1d6c), offs[i], 1 };

			CHECK((run_chan(ch, w, 7) == 0) == ok, "fence: channel %u offset %08x %s", ch,
			      offs[i], ok ? "refused" : "accepted");
		}
		/* the release without its offset, or without its DMA */
		{
			u32 w1[6] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x1a4), 0x66606660,
				      H(1, S3D, 0x1d70), 1 };
			u32 w2[6] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x1d6c), lab,
				      H(1, S3D, 0x1d74), 1 };

			CHECK(run_chan(ch, w1, 6) != 0, "fence: release with no offset accepted");
			CHECK(run_chan(ch, w2, 6) != 0, "fence: release with no DMA accepted");
		}
		/* reports: types 1-5, 16-aligned, in the slice */
		for (u32 t = 0; t < 8; t++) {
			const u32 o[] = { q, q + 0xff0, q + 0x1000, q + 8, q - 16 };

			for (u32 i = 0; i < ARRAY_SIZE(o); i++) {
				u32 off = o[i] & 0xffffff;
				bool ok = t >= 1 && t <= 5 && off >= q && off + 16 <= q + 0x1000 &&
					  !(off & 15) && o[i] < 0x1000000;
				u32 w[6] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x1a8), 0x66626660,
					     H(1, S3D, 0x1800), t << 24 | off };
				u32 r[6] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x1a8), 0x66626660,
					     H(1, S3D, 0x1e98), 0x02000000 | off };

				CHECK((run_chan(ch, w, 6) == 0) == ok, "query: channel %u type %u offset %x %s",
				      ch, t, off, ok ? "refused" : "accepted");
				if (t == 1) {
					bool rok = off >= q && off + 16 <= q + 0x1000 && !(off & 15);

					CHECK((run_chan(ch, r, 6) == 0) == rok,
					      "render enable: channel %u offset %x %s", ch, off,
					      rok ? "refused" : "accepted");
				}
			}
		}
		{
			u32 w[6] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x1800), 1u << 24 | q };
			u32 r[8] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x1e98), 0, H(1, S3D, 0x1e98),
				     0x01000000, H(1, S3D, 0x1e98), 0x03000000 | q };

			CHECK(run_chan(ch, w, 4) != 0, "query: report with no DMA accepted");
			CHECK(run_chan(ch, r, 6) == 0, "render enable: 0 or 0x01000000 refused");
			CHECK(run_chan(ch, r, 8) != 0, "render enable: mode 3 accepted");
		}
	}
}

/* --- copies (slice b) ------------------------------------------------------------------------ */

/* An M2MF copy as Mesa sends it: the DMAs, the eight-word launch */
static u32 mw[32];
static u32 m2mf(u32 din, u32 dout, u32 oin, u32 oout, u32 pin, u32 pout, u32 len, u32 lines,
		u32 fmt, u32 notify)
{
	u32 n = 0;

	mw[n++] = H(1, SM2MF, 0);
	mw[n++] = 0x31337303;
	mw[n++] = H(3, SM2MF, 0x180);
	mw[n++] = 0x66604200;
	mw[n++] = din;
	mw[n++] = dout;
	mw[n++] = H(8, SM2MF, 0x30c);
	mw[n++] = oin;
	mw[n++] = oout;
	mw[n++] = pin;
	mw[n++] = pout;
	mw[n++] = len;
	mw[n++] = lines;
	mw[n++] = fmt;
	mw[n++] = notify;
	return n;
}

#define V 0xfeed0000u
#define G 0xfeed0001u

static void test_m2mf(void)
{
	u32 n;

	n = m2mf(G, V, G_LO, A_LO, 0x1000, 0x1000, 0x1000, 16, 0x101, 0);
	CHECK(run(mw, n) == 0, "m2mf: a GART to VRAM copy refused");
	n = m2mf(V, G, A_LO, G_LO, 0x1000, 0x1000, 0x1000, 0x100, 0x101, 0);
	CHECK(run(mw, n) == 0, "m2mf: a copy of exactly A refused");
	n = m2mf(V, G, A_LO + 1, G_LO, 0x1000, 0x1000, 0x1000, 0x100, 0x101, 0);
	CHECK(run(mw, n) != 0, "m2mf: source one byte past A accepted");
	n = m2mf(V, V, A_LO, A_LO + A_SZ - 0x1000, 0x1000, 0x1000, 0x1000, 2, 0x101, 0);
	CHECK(run(mw, n) != 0, "m2mf: destination across A and B accepted");
	n = m2mf(V, V, A_LO, A_LO + A_SZ - 0x1000, 0x1000, 0x1000, 0x1000, 1, 0x101, 0);
	CHECK(run(mw, n) == 0, "m2mf: A's last page refused");
	n = m2mf(G, V, A_LO, A_LO, 0x1000, 0x1000, 0x1000, 1, 0x101, 0);
	CHECK(run(mw, n) != 0, "m2mf: VRAM offset through the GART DMA accepted");
	n = m2mf(V, V, 0, A_LO, 0x1000, 0x1000, 0x1000, 1, 0x101, 0);
	CHECK(run(mw, n) != 0, "m2mf: source in ps3fb's VRAM accepted");
	n = m2mf(G, V, 0x1000, A_LO, 0x1000, 0x1000, 0x1000, 1, 0x101, 0);
	CHECK(run(mw, n) != 0, "m2mf: source in the ring accepted");
	/* one line: the pitch adds nothing to the extent, so only the rule refuses it */
	n = m2mf(G, V, G_LO, A_LO, 0x80000000u, 0x1000, 0x1000, 1, 0x101, 0);
	CHECK(run(mw, n) != 0, "m2mf: pitch 2^31 accepted");
	n = m2mf(G, V, G_LO, A_LO, 0x1000, 0x1000, 0, 2, 0x101, 0);
	CHECK(run(mw, n) != 0, "m2mf: line length 0 accepted");
	n = m2mf(G, V, G_LO, A_LO, 0x1000, 0x1000, 0x1000, 0, 0x101, 0);
	CHECK(run(mw, n) != 0, "m2mf: line count 0 accepted");
	n = m2mf(G, V, G_LO, A_LO, 0x10, 0x10, 0x10, 2048, 0x101, 0);
	CHECK(run(mw, n) != 0, "m2mf: line count 2048 accepted");
	n = m2mf(G, V, G_LO, A_LO, 0x1000, 0x1000, 0x1000, 1, 0x102, 0);
	CHECK(run(mw, n) != 0, "m2mf: format 0x102 accepted");
	n = m2mf(G, V, G_LO, A_LO, 0x1000, 0x1000, 0x1000, 1, 0x101, 1);
	CHECK(run(mw, n) != 0, "m2mf: notify 1 accepted");
	n = m2mf(G, V, G_LO + G_SZ - 0x10, A_LO, 0x7fffffffu, 0x10, 0x10, 2047, 0x101, 0);
	CHECK(run(mw, n) != 0, "m2mf: a pitch reaching far past the buffer accepted");
	/* pitch smaller than the line (overlapping rows): the extent still counts the last line */
	n = m2mf(G, V, G_LO + G_SZ - 0x1100, A_LO, 0x100, 0x100, 0x1000, 2, 0x101, 0);
	CHECK(run(mw, n) == 0, "m2mf: overlapping rows inside G refused");
	n = m2mf(G, V, G_LO + G_SZ - 0x1000, A_LO, 0x100, 0x100, 0x1000, 2, 0x101, 0);
	CHECK(run(mw, n) != 0, "m2mf: overlapping rows past G accepted");

	/* the valid bits: a launch with a field another channel or the kernel left */
	{
		u32 w[] = { H(1, SM2MF, 0), 0x31337303, H(3, SM2MF, 0x180), 0x66604200, G, V,
			    H(1, SM2MF, 0x30c), G_LO, H(6, SM2MF, 0x314), 0x1000, 0x1000, 0x1000,
			    1, 0x101, 0 };

		CHECK(run(w, ARRAY_SIZE(w)) != 0 && strstr(last_warn, "not set by this channel"),
		      "m2mf: a launch without its own OFFSET_OUT accepted");
	}
	/* set in one submit, launched in the next; after a lost channel, nothing counts */
	{
		struct ps3rsx_vstate vs;
		u32 launch[] = { H(1, SM2MF, 0x328), 0 };

		vs_init(&vs, 2);
		n = m2mf(G, V, G_LO, A_LO, 0x1000, 0x1000, 0x1000, 1, 0x101, 0);
		mw[6] = H(7, SM2MF, 0x30c);		/* the burst without its launch */
		CHECK(ps3rsx_validate(&vs, mw, n - 1) == 0, "m2mf: the setup refused");
		CHECK(ps3rsx_validate(&vs, launch, 2) == 0, "m2mf: launch in a later submit refused");
		ps3rsx_vstate_forget(&vs);
		CHECK(ps3rsx_validate(&vs, launch, 2) != 0, "m2mf: launch after a lost channel accepted");
	}
	/* a non-incrementing burst of OFFSET_OUT words: the last one counts, as on the GPU */
	{
		u32 w[] = { H(1, SM2MF, 0), 0x31337303, H(3, SM2MF, 0x180), 0x66604200, G, V,
			    NI | H(3, SM2MF, 0x310), A_LO, 0, C_LO + C_SZ, H(1, SM2MF, 0x30c), G_LO,
			    H(6, SM2MF, 0x314), 0x1000, 0x1000, 0x1000, 1, 0x101, 0 };

		CHECK(run(w, ARRAY_SIZE(w)) != 0, "m2mf: non-incrementing OFFSET_OUT past A accepted");
	}
}

/* A scaled image from G into a surface 2D in A (swz: a swizzled surface in A) */
static u32 fw[64];
static u32 sifm(bool swz, u32 dfmt, u32 dpitch, u32 doff, u32 lu, u32 lv, u32 cfmt, u32 clip_pt,
		u32 clip_sz, u32 out_pt, u32 out_sz, u32 dudx, u32 dvdy, u32 size, u32 infmt,
		u32 off, u32 point)
{
	u32 n = 0;

	if (!swz) {
		fw[n++] = H(1, S2D, 0);
		fw[n++] = 0x313371c3;
		fw[n++] = H(3, S2D, 0x180);
		fw[n++] = 0x66604200;
		fw[n++] = V;
		fw[n++] = V;
		fw[n++] = H(4, S2D, 0x300);
		fw[n++] = dfmt;
		fw[n++] = dpitch << 16 | dpitch;
		fw[n++] = doff;
		fw[n++] = doff;
	} else {
		fw[n++] = H(1, SSWZ, 0);
		fw[n++] = 0x31337a73;
		fw[n++] = H(2, SSWZ, 0x180);
		fw[n++] = 0x66604200;
		fw[n++] = V;
		fw[n++] = H(2, SSWZ, 0x300);
		fw[n++] = dfmt | lu << 16 | lv << 24;
		fw[n++] = doff;
	}
	fw[n++] = H(1, SSIFM, 0);
	fw[n++] = 0x3137af00;
	fw[n++] = H(2, SSIFM, 0x180);
	fw[n++] = 0x66604200;
	fw[n++] = G;
	fw[n++] = H(1, SSIFM, 0x198);
	fw[n++] = swz ? 0x31337a73 : 0x313371c3;
	fw[n++] = H(8, SSIFM, 0x300);
	fw[n++] = cfmt;
	fw[n++] = 3;				/* SRCCOPY */
	fw[n++] = clip_pt;
	fw[n++] = clip_sz;
	fw[n++] = out_pt;
	fw[n++] = out_sz;
	fw[n++] = dudx;
	fw[n++] = dvdy;
	fw[n++] = H(4, SSIFM, 0x400);
	fw[n++] = size;
	fw[n++] = infmt;
	fw[n++] = off;
	fw[n++] = point;
	return n;
}

#define XY(x, y)	((u32)(y) << 16 | (x))

static void test_sifm(void)
{
	const u32 one = 1u << 20, inf = 0x10000 | 0x100;	/* center, pitch 256 */
	u32 n;

	/* a 64x64 A8R8G8B8 copy: source 256-byte rows of G, destination pitch 256 in A */
	n = sifm(false, 0xa, 256, A_LO, 0, 0, 3, 0, XY(64, 64), 0, XY(64, 64), one, one,
		 XY(64, 64), inf, G_LO, 0);
	CHECK(run(fw, n) == 0, "sifm: a 64x64 copy refused");
	/* the source at G's end: point sampling from the centre origin reads exactly the 64 x 64
	 * texels (the design's 7.6) */
	n = sifm(false, 0xa, 256, A_LO, 0, 0, 3, 0, XY(64, 64), 0, XY(64, 64), one, one,
		 XY(64, 64), inf, G_LO + G_SZ - 63 * 256 - 64 * 4, 0);
	CHECK(run(fw, n) == 0, "sifm: a source ending at G's last byte refused");
	n = sifm(false, 0xa, 256, A_LO, 0, 0, 3, 0, XY(64, 64), 0, XY(64, 64), one, one,
		 XY(64, 64), inf, G_LO + G_SZ - 63 * 256 - 64 * 4 + 4, 0);
	CHECK(run(fw, n) != 0, "sifm: a source one texel past G accepted");
	/* the steps sample past the declared size: the span counts */
	n = sifm(false, 0xa, 256, A_LO, 0, 0, 3, 0, XY(64, 64), 0, XY(64, 64), 8 * one, one,
		 XY(64, 64), inf, G_LO + G_SZ - 63 * 256 - 64 * 4, 0);
	CHECK(run(fw, n) != 0, "sifm: a step sampling past the source accepted");
	/* Neverball's copies as captured (2026-11-04): a 256x256 A8R8G8B8 source of pitch 1024
	 * filling its 256 KiB buffer exactly (G here), nearest; the same copy bilinear reads one
	 * texel row and column more, past the buffer */
	{
		const u32 nb_off = G_LO + G_SZ - 256 * 1024;

		n = sifm(false, 0xa, 1024, A_LO, 0, 0, 3, 0, XY(256, 256), 0, XY(256, 256), one, one,
			 XY(256, 256), 0x10000 | 1024, nb_off, 0);
		CHECK(run(fw, n) == 0, "sifm: Neverball's 256x256 nearest copy refused");
		n = sifm(false, 0xa, 1024, A_LO, 0, 0, 3, 0, XY(256, 256), 0, XY(256, 256), one, one,
			 XY(256, 256), 0x01020000 | 1024, nb_off, 0);
		CHECK(run(fw, n) != 0, "sifm: the same copy bilinear accepted");
		n = sifm(false, 0xa, 1024, A_LO, 0, 0, 3, 0, XY(256, 256), 0, XY(256, 256), one, one,
			 XY(256, 256), 0x10000 | 1024, nb_off, XY(16, 0));
		CHECK(run(fw, n) != 0, "sifm: the nearest copy from one texel further accepted");
	}
	/* destination: the clip or output rectangle reaching past A */
	n = sifm(false, 0xa, 256, A_LO + A_SZ - 63 * 256 - 64 * 4, 0, 0, 3, 0, XY(64, 64), 0,
		 XY(64, 64), one, one, XY(64, 64), inf, G_LO, 0);
	CHECK(run(fw, n) == 0, "sifm: a destination ending at A's last byte refused");
	n = sifm(false, 0xa, 256, A_LO + A_SZ - 63 * 256 - 64 * 4, 0, 0, 3, 0, XY(65, 64), 0,
		 XY(64, 64), one, one, XY(64, 64), inf, G_LO, 0);
	CHECK(run(fw, n) != 0, "sifm: a clip one pixel past A accepted");
	n = sifm(false, 0xa, 256, A_LO + A_SZ - 63 * 256 - 64 * 4, 0, 0, 3, 0, XY(64, 64), XY(0, 1),
		 XY(64, 64), one, one, XY(64, 64), inf, G_LO, 0);
	CHECK(run(fw, n) != 0, "sifm: an output point one row past A accepted");
	/* coordinates and sizes: at most 4096, sizes not 0, steps not negative */
	n = sifm(false, 0xa, 256, A_LO, 0, 0, 3, XY(0xffff, 0), XY(1, 1), 0, XY(64, 64), one, one,
		 XY(64, 64), inf, G_LO, 0);
	CHECK(run(fw, n) != 0, "sifm: a clip point of 0xffff accepted");
	/* 0x8000 is -32768 if the GPU reads it signed: inside C as unsigned, so only the 4096 rule
	 * refuses it */
	n = sifm(false, 0xa, 64, C_LO, 0, 0, 3, XY(0, 0x8000), XY(1, 1), XY(0, 0x8000), XY(1, 1),
		 one, one, XY(1, 1), inf, G_LO, 0);
	CHECK(run(fw, n) != 0, "sifm: an output point y of 0x8000 accepted");
	n = sifm(false, 0xa, 256, A_LO, 0, 0, 3, 0, XY(64, 64), 0, XY(0, 64), one, one,
		 XY(64, 64), inf, G_LO, 0);
	CHECK(run(fw, n) != 0, "sifm: an output width of 0 accepted");
	n = sifm(false, 0xa, 256, A_LO, 0, 0, 3, 0, XY(64, 64), 0, XY(64, 64), 0x80000000u, one,
		 XY(64, 64), inf, G_LO, 0);
	CHECK(run(fw, n) != 0, "sifm: a negative step accepted");
	n = sifm(false, 0xa, 256, A_LO, 0, 0, 0x55, 0, XY(64, 64), 0, XY(64, 64), one, one,
		 XY(64, 64), inf, G_LO, 0);
	CHECK(run(fw, n) != 0, "sifm: an unknown source format accepted");
	n = sifm(false, 0xa, 256, A_LO, 0, 0, 3, 0, XY(64, 64), 0, XY(64, 64), one, one,
		 XY(64, 64), 0x30000 | 0x100, G_LO, 0);
	CHECK(run(fw, n) != 0, "sifm: origin 3 accepted");
	n = sifm(false, 0x55, 256, A_LO, 0, 0, 3, 0, XY(64, 64), 0, XY(64, 64), one, one,
		 XY(64, 64), inf, G_LO, 0);
	CHECK(run(fw, n) != 0, "sifm: an unknown surface format accepted");
	/* the swizzled surface: the rectangle inside the 2^u x 2^v block, the block in the BO */
	n = sifm(true, 0xa, 0, A_LO, 6, 6, 3, 0, XY(64, 64), 0, XY(64, 64), one, one,
		 XY(64, 64), inf, G_LO, 0);
	CHECK(run(fw, n) == 0, "sifm: a 64x64 swizzled copy refused");
	n = sifm(true, 0xa, 0, A_LO, 5, 6, 3, 0, XY(64, 64), 0, XY(64, 64), one, one,
		 XY(64, 64), inf, G_LO, 0);
	CHECK(run(fw, n) != 0, "sifm: a rectangle outside its swizzled block accepted");
	n = sifm(true, 0xa, 0, A_LO + A_SZ - 64 * 64 * 4 + 4, 6, 6, 3, 0, XY(64, 64), 0,
		 XY(64, 64), one, one, XY(64, 64), inf, G_LO, 0);
	CHECK(run(fw, n) != 0, "sifm: a swizzled block past A accepted");
	n = sifm(true, 0xa, 0, A_LO, 13, 6, 3, 0, XY(64, 64), 0, XY(64, 64), one, one,
		 XY(64, 64), inf, G_LO, 0);
	CHECK(run(fw, n) != 0, "sifm: a swizzled log2 size of 13 accepted");
	/* the surface's own fields: a destination the channel never set */
	{
		u32 m = sifm(false, 0xa, 256, A_LO, 0, 0, 3, 0, XY(64, 64), 0, XY(64, 64), one, one,
			     XY(64, 64), inf, G_LO, 0);

		CHECK(run(fw + 11, m - 11) != 0 && strstr(last_warn, "not set by this channel"),
		      "sifm: into a surface not set by this channel accepted");
	}
}

/* --- clears (slice b) ------------------------------------------------------------------------- */

/* The surface as Mesa's nv30_validate_fb() writes it: w x h, linear, A8R8G8B8 + Z24S8 */
static u32 cw[64];
static u32 surface(u32 w, u32 h, u32 fmt, u32 pitch, u32 c0, u32 z, u32 rt_enable, u32 coord,
		   u32 win)
{
	u32 n = 0;

	cw[n++] = H(1, S3D, 0);
	cw[n++] = 0x31337000;
	cw[n++] = H(1, S3D, 0x194);
	cw[n++] = V;
	cw[n++] = H(1, S3D, 0x198);
	cw[n++] = V;
	cw[n++] = H(3, S3D, 0x200);
	cw[n++] = w << 16;
	cw[n++] = h << 16;
	cw[n++] = fmt;
	cw[n++] = H(4, S3D, 0x2b8);
	cw[n++] = win;
	cw[n++] = 0;
	cw[n++] = (w - 1) << 16;
	cw[n++] = (h - 1) << 16;
	cw[n++] = H(1, S3D, 0x22c);
	cw[n++] = pitch;
	cw[n++] = H(3, S3D, 0x20c);
	cw[n++] = pitch;
	cw[n++] = c0;
	cw[n++] = z;
	cw[n++] = H(1, S3D, 0x220);
	cw[n++] = rt_enable;
	cw[n++] = H(1, S3D, 0x1d88);
	cw[n++] = coord;
	return n;
}

static int clear(u32 n, u32 bits)
{
	cw[n++] = H(1, S3D, 0x1d94);
	cw[n++] = bits;
	return run(cw, n);
}

#define LIN	0x148		/* linear, Z24S8, A8R8G8B8 */

static void fb_order_cases(const char *what, int (*trigger)(u32 n));
static int clear_bits(u32 n);

static void test_clear(void)
{
	u32 n;

	/* 512 x 512 x 4 bytes, pitch 2048: exactly A's 1 MiB; zeta in C */
	CHECK(clear(surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 512, 0), 0xf3) == 0,
	      "clear: a 512x512 surface refused");
	CHECK(clear(surface(512, 513, LIN, 2048, A_LO, C_LO, 1, 513, 0), 0xf0) != 0,
	      "clear: one row past A accepted");
	CHECK(clear(surface(512, 512, LIN, 2048, A_LO + 64, C_LO, 1, 512, 0), 0xf0) != 0,
	      "clear: a colour target 64 bytes past A's start accepted");
	CHECK(clear(surface(512, 512, LIN, 2048, A_LO + 63, C_LO, 1, 512, 0), 0xf0) == 0,
	      "clear: an offset the hardware rounds down to A refused");
	CHECK(clear(surface(512, 512, LIN, 2048, A_LO, C_LO + C_SZ - 0x10000, 1, 512, 0), 0x3) != 0,
	      "clear: zeta past C accepted");
	CHECK(clear(surface(512, 512, LIN, 2048, A_LO + 0x10000, C_LO, 1, 512, 0), 0x3) == 0,
	      "clear: a depth clear checked the colour target");
	/* antialiasing scales the surface (4 samples: 2x2, 2 samples: 2x1) */
	CHECK(clear(surface(256, 256, LIN | 0x4000, 2048, A_LO, C_LO, 1, 256, 0), 0xf0) == 0,
	      "clear: a 256x256 4x surface refused");
	CHECK(clear(surface(256, 257, LIN | 0x4000, 2048, A_LO, C_LO, 1, 257, 0), 0xf0) != 0,
	      "clear: a 4x surface one row past A accepted");
	CHECK(clear(surface(512, 512, LIN | 0x3000, 2048, A_LO, C_LO, 1, 512, 0), 0xf0) != 0,
	      "clear: a 2x surface twice as wide as A's rows accepted");
	CHECK(run(cw, surface(512, 512, LIN | 0x6000, 2048, A_LO, C_LO, 1, 512, 0)) != 0,
	      "clear: AA mode 6 accepted");
	/* the window offset and the y-flip height reach rows past the clip */
	CHECK(clear(surface(512, 511, LIN, 2048, A_LO, C_LO, 1, 511, XY(0, 2)), 0xf0) != 0,
	      "clear: a window offset past A accepted");
	CHECK(clear(surface(512, 400, LIN, 2048, A_LO, C_LO, 1, 600, 0), 0xf0) != 0,
	      "clear: a coordinate height past A accepted");
	n = surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 512, 0);
	cw[14] = 512 << 16;
	CHECK(clear(n, 0xf0) != 0, "clear: window clip rows past A accepted");
	fb_order_cases("clear", clear_bits);
	/* RT_ENABLE: target 1 enabled without its offset and pitch (P19's question) */
	CHECK(clear(surface(512, 512, LIN, 2048, A_LO, C_LO, 0x13, 512, 0), 0xf0) != 0,
	      "clear: target 1 enabled with no offset accepted");
	/* swizzled: the whole 2^w x 2^h block */
	CHECK(clear(surface(512, 512, 0x09090248, 2048, A_LO, C_LO, 1, 512, 0), 0xf0) == 0,
	      "clear: a 512x512 swizzled surface refused");
	CHECK(clear(surface(512, 512, 0x0a090248, 2048, A_LO, C_LO, 1, 512, 0), 0xf0) != 0,
	      "clear: a 1024x512 swizzled block past A accepted");
	CHECK(run(cw, surface(512, 512, 0x0d090248, 2048, A_LO, C_LO, 1, 512, 0)) != 0,
	      "clear: RT_FORMAT log2 13 accepted");
	/* coordinates above 4096, unknown bits */
	CHECK(run(cw, surface(4097, 1, LIN, 2048, A_LO, C_LO, 1, 1, 0)) != 0,
	      "clear: a surface 4097 wide accepted");
	CHECK(clear(surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 512, 0), 0x100) != 0,
	      "clear: unknown clear bits accepted");
	CHECK(run(cw, surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 512 | 1u << 20, 0)) != 0,
	      "clear: coordinate conventions bit 20 accepted");
	{
		u32 w[] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x224), 1 };

		CHECK(run(w, 4) != 0, "clear: method 0x224 with data 1 accepted");
	}
	/* a clear with a field the channel never set */
	{
		u32 n = surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 512, 0) - 2;	/* no COORD */

		CHECK(clear(n, 0xf0) != 0 && strstr(last_warn, "not set by this channel"),
		      "clear: without its coordinate conventions accepted");
	}
}

/* --- draws: render targets and zeta (slice b) ------------------------------------------------ */

/*
 * The extent's clip semantics and the render target pitches. The orders of the surface clip,
 * the window offset and the y flip the RSX takes are unprobed, and the rules that took the
 * furthest one refused streams current Mesa sends: until the clip probe and the Mesa fixes
 * (the height masked in COORD, the PS3's swizzled buffers padded, the 16x2 block 32 wide), the
 * extent is the union of the surface clip, the window rectangle and the flip's height, and the
 * cases below that a returning rule would refuse are marked "(order uncovered)". Mesa's streams
 * those rules refused pass: a 4096-tall target (COORD 0x1000: the inverted origin bit with
 * height 0, Mesa ORs the height in unmasked) and 0013's 1-high linear rows at an unaligned
 * offset in the last 64 bytes of their buffer. Mesa's 16x2 swizzled block at a window offset
 * passes. cw's words: 11 the window offset, 13 and 14 the window clip, 16 zeta's pitch, 18
 * colour 0's.
 */
static void fb_order_cases(const char *what, int (*trigger)(u32 n))
{
	u32 n;

	n = surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 512, 0);
	cw[13] = 447 << 16;
	CHECK(trigger(n) == 0, "%s: a window clip narrower than the surface refused", what);
	n = surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 512, XY(64, 0));
	cw[13] = 447 << 16;
	CHECK(trigger(n) == 0,
	      "%s: a surface clip at a window offset refused (order uncovered)", what);
	n = surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 504, XY(0, 8));
	cw[14] = 503 << 16;
	CHECK(trigger(n) == 0,
	      "%s: surface clip rows at a window offset refused (order uncovered)", what);
	/* the origin at the bottom (COORD 0x1000, any value in bits 12-15) */
	CHECK(trigger(surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 0x1000 | 512, 0)) == 0,
	      "%s: a flipped 512x512 surface refused", what);
	CHECK(trigger(surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 0x1000 | 500, 0)) == 0,
	      "%s: clip rows past an inverted flip's height refused (order uncovered)", what);
	n = surface(512, 500, LIN, 2048, A_LO, C_LO, 1, 0x1000 | 500, 0);
	cw[14] = 509 << 16;
	CHECK(trigger(n) == 0,
	      "%s: window rows past an inverted flip's height refused (order uncovered)", what);
	CHECK(trigger(surface(512, 500, LIN, 2048, A_LO, C_LO, 1, 500, 0)) == 0,
	      "%s: a 500-row surface with its origin at the top refused", what);
	n = surface(512, 504, LIN, 2048, A_LO, C_LO, 1, 0x1000 | 512, XY(0, 8));
	CHECK(trigger(n) == 0,
	      "%s: a flip before the window's y offset refused (order uncovered)", what);
	/* Mesa's 4096-tall target: COORD 0x1000 (height 4096 unmasked), and with CENTER_INTEGER */
	CHECK(trigger(surface(16, 4096, LIN, 64, A_LO, C_LO, 1, 0x1000, 0)) == 0,
	      "%s: a 4096-tall target with COORD 0x1000 refused", what);
	CHECK(trigger(surface(16, 4096, LIN, 64, A_LO, C_LO, 1, 0x11000, 0)) == 0,
	      "%s: a 4096-tall target with COORD 0x11000 refused", what);
	/* Mesa 0013's 1-high levels as linear rows: the offset's remainder x in RT_HORIZ (w + x)
	 * and the window offset, the window clip w wide; a 1x1 RGBA8 level at A's end - 8 (x 14)
	 * and a 4x1 at A's end - 32 (x 8), pitch align(w * 4 + 64, 64) */
	n = surface(15, 1, LIN, 128, A_LO + A_SZ - 8, C_LO, 1, 1, XY(14, 0));
	cw[13] = 0;
	CHECK(trigger(n) == 0, "%s: a 1x1 linear row 8 bytes before A's end refused", what);
	n = surface(12, 1, LIN, 128, A_LO + A_SZ - 32, C_LO, 1, 1, XY(8, 0));
	cw[13] = 3 << 16;
	CHECK(trigger(n) == 0, "%s: a 4x1 linear row 32 bytes before A's end refused", what);
	n = surface(15, 1, LIN, 128, A_LO + A_SZ - 8, C_LO, 1, 1, XY(14, 0));
	cw[13] = 2 << 16;	/* columns 14-16: the last one past A */
	CHECK(trigger(n) != 0 && strstr(last_warn, "render target outside"),
	      "%s: a 1x1 linear row with its window past A accepted", what);
	/* pitches: multiples of 64 up to 0x10000, colour and zeta; a swizzled surface's is unused */
	CHECK(trigger(surface(16, 16, LIN, 0x10000, A_LO, C_LO, 1, 16, 0)) == 0,
	      "%s: pitch 0x10000 refused", what);
	CHECK(trigger(surface(16, 8, LIN, 0x10040, A_LO, C_LO, 1, 8, 0)) != 0 &&
	      strstr(last_warn, "pitch above 0x10000"), "%s: pitch 0x10040 accepted", what);
	CHECK(trigger(surface(512, 512, LIN, 2048 + 32, A_LO, C_LO, 1, 256, 0)) != 0 &&
	      strstr(last_warn, "not a multiple of 64"), "%s: pitch 2080 accepted", what);
	n = surface(16, 16, LIN, 2048, A_LO, C_LO, 1, 16, 0);
	cw[16] = 2048 + 4;
	CHECK(trigger(n) != 0 && strstr(last_warn, "not a multiple of 64"),
	      "%s: zeta pitch 2052 accepted", what);
	CHECK(trigger(surface(512, 512, 0x09090248, 100, A_LO, C_LO, 1, 512, 0)) == 0,
	      "%s: a swizzled surface's unused pitch refused", what);
	/* Mesa's small level at an unaligned offset: a 16x2 R5G6B5 block from the rounded-down
	 * offset, the remainder as the window's x offset, the window clip what is left of the
	 * block (0060); with the window clip the block's whole width it stays refused */
	n = surface(16, 2, 0x01040222, 4096, A_LO, A_LO, 1, 2, XY(8, 0));
	cw[13] = 7 << 16;
	CHECK(trigger(n) == 0, "%s: Mesa's 16x2 swizzled block at a window offset refused", what);
	CHECK(trigger(surface(16, 2, 0x01040222, 4096, A_LO, A_LO, 1, 2, XY(8, 0))) != 0 &&
	      strstr(last_warn, "outside a swizzled surface"),
	      "%s: a 16x2 block with a window past it accepted", what);
}

static int clear_bits(u32 n)
{
	return clear(n, 0xf3);
}

/* A draw after the surface in cw[0, n): BEGIN, a vertex batch, END */
static u32 draw_words(u32 n)
{
	cw[n++] = H(1, S3D, 0x1808);
	cw[n++] = 5;
	cw[n++] = NI | H(1, S3D, 0x1814);
	cw[n++] = 0x02000000;
	cw[n++] = H(1, S3D, 0x1808);
	cw[n++] = 0;
	return n;
}

static int draw(u32 n)
{
	return run(cw, draw_words(n));
}

/* The kernel's own GART: the ring (0x1000 up to RING_END), the zero area after it */
#define RING_AT		0x00001000u
#define ZERO_AT		0x001f0000u

static void test_draw(void)
{
	struct ps3rsx_vstate vs;
	u32 n, w[64];
	unsigned long c0;

	/* 512 x 512 x 4 bytes, pitch 2048: exactly A's 1 MiB; zeta in C */
	CHECK(draw(surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 512, 0)) == 0,
	      "draw: a 512x512 surface refused");
	CHECK(draw(surface(512, 513, LIN, 2048, A_LO, C_LO, 1, 513, 0)) != 0 &&
	      strstr(last_warn, "render target outside"), "draw: one row past A accepted");
	/* zeta is checked at every draw, whatever the depth and stencil state */
	CHECK(draw(surface(512, 512, LIN, 2048, A_LO, C_LO + C_SZ - 0x10000, 1, 512, 0)) != 0,
	      "draw: zeta past C accepted");
	/* RT_ENABLE 0 still writes target 0 */
	CHECK(draw(surface(512, 513, LIN, 2048, A_LO, C_LO, 0, 513, 0)) != 0,
	      "draw: RT_ENABLE 0 and target 0 past A accepted");
	/* target 1 enabled without its offset and pitch */
	CHECK(draw(surface(512, 512, LIN, 2048, A_LO, C_LO, 0x13, 512, 0)) != 0 &&
	      strstr(last_warn, "not set by this channel"), "draw: target 1 with no offset accepted");
	/* antialiasing, the window offset, the y-flip height, swizzled blocks: as for clears */
	CHECK(draw(surface(256, 257, LIN | 0x4000, 2048, A_LO, C_LO, 1, 257, 0)) != 0,
	      "draw: a 4x surface one row past A accepted");
	CHECK(draw(surface(512, 511, LIN, 2048, A_LO, C_LO, 1, 511, XY(0, 2))) != 0,
	      "draw: a window offset past A accepted");
	CHECK(draw(surface(512, 400, LIN, 2048, A_LO, C_LO, 1, 600, 0)) != 0,
	      "draw: a coordinate height past A accepted");
	fb_order_cases("draw", draw);
	CHECK(draw(surface(512, 512, 0x0a090248, 2048, A_LO, C_LO, 1, 512, 0)) != 0,
	      "draw: a 1024x512 swizzled block past A accepted");
	/* the kernel's GART: a target or zeta over the ring or the zero area */
	n = surface(64, 64, LIN, 256, G_LO, G_LO + 0x10000, 1, 64, 0);
	cw[3] = cw[5] = G;
	CHECK(draw(n) == 0, "draw: a target and zeta in G refused");
	n = surface(64, 64, LIN, 256, RING_AT, G_LO, 1, 64, 0);
	cw[3] = cw[5] = G;
	CHECK(draw(n) != 0, "draw: a GART target over the ring accepted");
	n = surface(64, 64, LIN, 256, G_LO, ZERO_AT, 1, 64, 0);
	cw[3] = cw[5] = G;
	CHECK(draw(n) != 0, "draw: GART zeta over the zero area accepted");
	n = surface(64, 64, LIN, 256, G_LO, G_LO + 0x10000, 1, 64, 0);
	cw[3] = G;		/* zeta's slot VRAM: G's offset is no VRAM buffer of the file */
	CHECK(draw(n) != 0, "draw: zeta through the wrong domain accepted");
	/* a draw on a fresh channel that never set its surface */
	{
		u32 d[] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x1808), 5, H(1, S3D, 0x1808), 0 };

		CHECK(run(d, ARRAY_SIZE(d)) != 0 && strstr(last_warn, "not set by this channel"),
		      "draw: a draw with no surface set accepted");
		n = surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 512, 0) - 2;	/* no COORD */
		CHECK(draw(n) != 0 && strstr(last_warn, "not set by this channel"),
		      "draw: a draw without its coordinate conventions accepted");
	}

	/* the kept result: one check for draws on an unchanged surface (the same values written
	 * again included); a field that changes value between draws is checked again */
	vs_init(&vs, 2);
	n = surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 512, 0);
	CHECK(ps3rsx_validate(&vs, cw, draw_words(n)) == 0, "draw: the kept case's first draw refused");
	c0 = p4_draw_checks;
	for (int i = 0; i < 10; i++)
		CHECK(ps3rsx_validate(&vs, cw + n, draw_words(n) - n) == 0, "draw: draw %d refused", i);
	CHECK(ps3rsx_validate(&vs, cw, draw_words(n)) == 0, "draw: the same surface again refused");
	CHECK(p4_draw_checks == c0, "draw: %lu checks for draws on an unchanged surface",
	      p4_draw_checks - c0);
	{
		u32 m[] = { H(1, S3D, 0x210), A_LO + 0x40000, H(1, S3D, 0x1808), 5, H(1, S3D, 0x1808), 0 };
		struct ps3rsx_vstate t = vs;

		last_warn[0] = 0;
		CHECK(ps3rsx_validate(&t, m, ARRAY_SIZE(m)) != 0,
		      "draw: a target moved past A between draws accepted");
		CHECK(p4_draw_checks == c0 + 1, "draw: a changed target not checked again");
	}
	/* no surface field changes inside a primitive: its BEGIN's check covers every vertex */
	{
		u32 m[] = { H(1, S3D, 0x1808), 5, H(1, S3D, 0x210), A_LO + 0x40000,
			    NI | H(1, S3D, 0x1814), 0x02000000, H(1, S3D, 0x1808), 0 };
		u32 same[] = { H(1, S3D, 0x1808), 5, H(1, S3D, 0x210), A_LO,
			       NI | H(1, S3D, 0x1814), 0x02000000, H(1, S3D, 0x1808), 0 };
		struct ps3rsx_vstate t = vs;

		last_warn[0] = 0;
		CHECK(ps3rsx_validate(&t, m, ARRAY_SIZE(m)) != 0 &&
		      strstr(last_warn, "inside an open primitive"),
		      "draw: a target changed inside a primitive accepted");
#ifndef PS3RSX_P4_COUNT		/* (there P7 refuses every state write inside a primitive) */
		t = vs;
		CHECK(ps3rsx_validate(&t, same, ARRAY_SIZE(same)) == 0,
		      "draw: a target rewritten with its value inside a primitive refused");
#else
		(void)same;
#endif
	}
	/* a buffer the targets were checked against leaves the file's set: the next draw (no field
	 * changed) is checked again and refused; back in the set, it passes */
	p4set_remove(&set, P4_VRAM, A_LO);
	{
		struct ps3rsx_vstate t = vs;

		last_warn[0] = 0;
		CHECK(ps3rsx_validate(&t, cw + n, draw_words(n) - n) != 0,
		      "draw: a draw after its target's buffer left the set accepted");
	}
	p4set_insert(&set, P4_VRAM, A_LO, A_SZ);
	CHECK(ps3rsx_validate(&vs, cw + n, draw_words(n) - n) == 0,
	      "draw: a draw with its buffer back refused");
	/* a primitive continued in a later submit after its buffer left the set */
	w[0] = H(1, S3D, 0x1808);
	w[1] = 5;
	w[2] = NI | H(1, S3D, 0x1814);
	w[3] = 0x02000000;
	CHECK(ps3rsx_validate(&vs, w, 4) == 0 && vs.prim_open, "draw: an open primitive refused");
	p4set_remove(&set, P4_VRAM, A_LO);
	{
		struct ps3rsx_vstate t = vs;

		last_warn[0] = 0;
		CHECK(ps3rsx_validate(&t, w + 2, 2) != 0,
		      "draw: a continued primitive after its buffer left the set accepted");
	}
	p4set_insert(&set, P4_VRAM, A_LO, A_SZ);
	w[4] = H(1, S3D, 0x1808);
	w[5] = 0;
	CHECK(ps3rsx_validate(&vs, w + 2, 4) == 0 && !vs.prim_open,
	      "draw: the primitive's rest refused");
	/* a lost channel: its fields no longer count, nor the kept result */
	ps3rsx_vstate_forget(&vs);
	last_warn[0] = 0;
	CHECK(ps3rsx_validate(&vs, cw + n, draw_words(n) - n) != 0,
	      "draw: a draw after a lost channel accepted");
	/* lost with a primitive open: the reset ended it, so the channel's state sent again draws
	 * (the primitive still taken as open refused every push from then on) */
	vs_init(&vs, 2);
	CHECK(ps3rsx_validate(&vs, cw, n) == 0 && ps3rsx_validate(&vs, w, 4) == 0 && vs.prim_open,
	      "draw: the lost primitive's BEGIN refused");
	ps3rsx_vstate_forget(&vs);
	CHECK(!vs.prim_open, "draw: a lost channel's primitive still open");
	last_warn[0] = 0;
	CHECK(ps3rsx_validate(&vs, cw, draw_words(n)) == 0,
	      "draw: a lost channel's state sent again and drawn refused");
	set_init();
}

/*
 * The surface state's field rules, as Mesa writes the fields and the clip probe measured them:
 * the surface clip's origin 0 (RT_HORIZ and RT_VERT's low halves), COORD_CONVENTIONS only the
 * height, the inverted origin (bit 12) and the integer centre (bit 16), the scissor's and the
 * viewport rectangle's origins below 0x8000. The extent counts the clip's origin even when a
 * rule lets one through (as the count-only build does): poked into the state here. cw's words
 * 7 and 8 are RT_HORIZ and RT_VERT.
 */
static void test_rt_fields(void)
{
	static const u32 coord_bad[] = { 0x2000, 0x4000, 0x8000, 0x20000, 0x40000, 0x80000 };
	static const u32 coord_ok[] = { 0x1000, 0x10000, 0x11000 };
	static const u16 origin_m[] = { 0x8c0, 0x8c4, 0xa00, 0xa04 };
	struct ps3rsx_vstate vs;
	u32 n, i;

	/* the surface clip origin */
	n = surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 512, 0);
	cw[7] |= 1;
	CHECK(run(cw, n) != 0 && strstr(last_warn, "surface clip origin"),
	      "clear: surface clip x origin 1 accepted");
	n = surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 512, 0);
	cw[8] |= 4;
	CHECK(run(cw, n) != 0 && strstr(last_warn, "surface clip origin"),
	      "clear: surface clip y origin 4 accepted");
	n = surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 512, 0);
	cw[7] |= 4096;		/* the old rule's largest origin */
	CHECK(run(cw, n) != 0 && strstr(last_warn, "surface clip origin"),
	      "clear: surface clip x origin 4096 accepted");
	CHECK(clear(surface(4096, 1, LIN, 0x4000, A_LO, C_LO, 1, 1, 0), 0xf3) == 0 &&
	      draw(surface(1, 4096, LIN, 64, A_LO, C_LO, 1, 0x1000, 0)) == 0,
	      "clear: surface clips 4096 wide or tall from 0 refused");

	/* COORD_CONVENTIONS */
	for (i = 0; i < ARRAY_SIZE(coord_bad); i++)
		CHECK(run(cw, surface(512, 512, LIN, 2048, A_LO, C_LO, 1, coord_bad[i] | 512, 0)) != 0 &&
		      strstr(last_warn, "coordinate conventions"), "coord: COORD %08x accepted",
		      coord_bad[i] | 512);
	for (i = 0; i < ARRAY_SIZE(coord_ok); i++) {
		CHECK(clear(surface(512, 512, LIN, 2048, A_LO, C_LO, 1, coord_ok[i] | 512, 0), 0xf3) == 0,
		      "coord: a clear with COORD %08x refused", coord_ok[i] | 512);
		CHECK(draw(surface(512, 512, LIN, 2048, A_LO, C_LO, 1, coord_ok[i] | 512, 0)) == 0,
		      "coord: a draw with COORD %08x refused", coord_ok[i] | 512);
	}

	/* the scissor's and the viewport rectangle's origins, alone, in a burst, and Mesa's */
	for (i = 0; i < ARRAY_SIZE(origin_m); i++) {
		u32 m = origin_m[i];
		u32 w[] = { H(1, S3D, 0), 0x31337000, H(1, S3D, m), 0 };

		w[3] = 0x8000;
		CHECK(run(w, 4) != 0 && strstr(last_warn, "origin at or above 0x8000"),
		      "origin: method %x data %08x accepted", m, w[3]);
		w[3] = 0x0100ffff;
		CHECK(run(w, 4) != 0, "origin: method %x data %08x accepted", m, w[3]);
		w[3] = 0x10007fff;
		CHECK(run(w, 4) == 0, "origin: method %x data %08x refused", m, w[3]);
		w[3] = 0x10000000;	/* Mesa's scissor off, 4096 from 0 */
		CHECK(run(w, 4) == 0, "origin: method %x data %08x refused", m, w[3]);
		w[3] = 0x00011000;	/* Mesa's largest origin, 4096 */
		CHECK(run(w, 4) == 0, "origin: method %x data %08x refused", m, w[3]);
	}
	{
		u32 inc[] = { H(1, S3D, 0), 0x31337000, H(2, S3D, 0xa00), 1920 << 16, 0x8000 | 1080 << 16 };
		u32 ni[] = { H(1, S3D, 0), 0x31337000, NI | H(2, S3D, 0x8c0), 0x10000000, 0x9000 };

		CHECK(run(inc, ARRAY_SIZE(inc)) != 0, "origin: viewport y 0x8000 in a burst accepted");
		CHECK(run(ni, ARRAY_SIZE(ni)) != 0,
		      "origin: scissor x 0x9000 in a non-incrementing burst accepted");
	}

	/* the extent counts the clip's origin: a 16x1 row filling A's last 64 bytes, its x or y
	 * origin poked past the field rule */
	for (i = 0; i < 2; i++) {
		int (*trigger)(u32 n) = i ? draw : clear_bits;
		const char *what = i ? "draw" : "clear";
		u32 k = surface(16, 1, LIN, 64, A_LO + A_SZ - 64, C_LO, 1, 1, 0);

		CHECK(trigger(k) == 0, "%s: a 16x1 row at A's end refused", what);
		vs_init(&vs, 2);
		CHECK(ps3rsx_validate(&vs, cw, k) == 0, "%s: the poked case's surface refused", what);
		fset(&vs, P4F_RT_HORIZ, 16 << 16 | 1);
		memcpy(cw, (u32[]){ H(1, S3D, 0x1d94), 0xf3 }, 8);
		k = i ? draw_words(0) : 2;
		last_warn[0] = 0;
		CHECK(ps3rsx_validate(&vs, cw, k) != 0 && strstr(last_warn, "render target outside"),
		      "%s: a surface clip x origin let through not in the extent", what);
		k = surface(16, 1, LIN, 64, A_LO + A_SZ - 64, C_LO, 1, 1, 0);
		vs_init(&vs, 2);
		CHECK(ps3rsx_validate(&vs, cw, k) == 0, "%s: the poked case's surface refused", what);
		fset(&vs, P4F_RT_VERT, 1 << 16 | 1);
		memcpy(cw, (u32[]){ H(1, S3D, 0x1d94), 0xf3 }, 8);
		k = i ? draw_words(0) : 2;
		last_warn[0] = 0;
		CHECK(ps3rsx_validate(&vs, cw, k) != 0 && strstr(last_warn, "render target outside"),
		      "%s: a surface clip y origin let through not in the extent", what);
	}
}

/* --- coverage (H2): every field is state the shadow restores ------------------------------ */

static const u32 class_obj[VC_N] = { 0, 0x31337000, 0x31337303, 0x313371c3, 0x31337a73,
				     0x3137af00 };

/* Fields valid only within a submit (their valid bits drop at each submit): none in a and b */
static const int within_submit[] = { -1 };

static void test_coverage(void)
{
	static u32 buf[RESTORE_MAX_WORDS];

	for (int f = 0; f < P4F_N; f++) {
		struct ps3rsx_shadow *sh = ps3rsx_shadow_new(), *hw = ps3rsx_shadow_new();
		u32 cls = p4_fields[f].cls, m = p4_fields[f].m, val = 0x1000 + f * 4;
		u32 w[4] = { H(1, 1, 0), class_obj[cls], H(1, 1, m), val };
		bool listed = false, found = false;
		u32 bound[8] = { 0 };
		int n;

		if (m >= 0x180 && m <= 0x1bc && m != 0x198)
			w[3] = 0xfeed0000;	/* a handle the shadow can hold */
		if (cls == VC_SIFM && m == 0x198)
			w[3] = 0x313371c3;
		val = w[3];
		for (u32 i = 0; i < ARRAY_SIZE(within_submit); i++)
			listed |= within_submit[i] == f;
		ps3rsx_shadow_push(sh, w, 4);
		n = ps3rsx_shadow_restore(sh, hw, buf, RESTORE_MAX_WORDS);
		/* the restore stream: binds, then state words on the bound subchannels */
		for (int i = 0; i < n;) {
			u32 h = buf[i], cnt = (h >> 18) & 0x7ff, sc = (h >> 13) & 7, mm = h & 0x1ffc;
			bool ni = h & NI;

			for (u32 k = 0; k < cnt && i + 1 + (int)k < n; k++) {
				u32 mk = ni ? mm : mm + 4 * k;

				if (mk == 0)
					bound[sc] = buf[i + 1 + k];
				else if (bound[sc] == class_obj[cls] && mk == m && buf[i + 1 + k] == val)
					found = true;
			}
			i += 1 + cnt;
		}
		CHECK(found || listed, "coverage: field %d (class %u method %03x) not restored by the "
		      "shadow at a channel switch", f, cls, m);
		ps3rsx_shadow_free(sh);
		ps3rsx_shadow_free(hw);
	}
}

/* --- classification (L4): every allowlisted method is known to the checks ------------------ */

struct rng { u16 lo, hi; };

/* 3D methods slice c1 checks (textures, programs, arrays, the index buffer, draws), those with
 * a value rule but no field (the scissor's and viewport rectangle's origins: test_rt_fields), and
 * those with no memory effect: state that neither names memory nor feeds an extent */
static const struct rng c1_3d[] = {
	{ 0x08e4, 0x08e4 }, { 0x0900, 0x097c }, { 0x1680, 0x16bc }, { 0x173c, 0x177c },
	{ 0x1808, 0x1824 }, { 0x1840, 0x187c }, { 0x1a00, 0x1bfc }, { 0x1da4, 0x1da4 },
	{ 0x1dac, 0x1db0 },
};
static const struct rng value_3d[] = { { 0x08c0, 0x08c4 }, { 0x0a00, 0x0a04 } };
static const struct rng none_3d[] = {
	{ 0x0100, 0x0100 }, { 0x0110, 0x0110 }, { 0x0300, 0x03bc }, { 0x0a08, 0x0a7c },
	{ 0x0b00, 0x0bfc }, { 0x142c, 0x142c }, { 0x1450, 0x1454 }, { 0x1478, 0x14fc },
	{ 0x1500, 0x15fc }, { 0x1710, 0x1718 }, { 0x17c8, 0x17cc },
	{ 0x1804, 0x1804 }, { 0x1828, 0x183c }, { 0x1880, 0x19fc }, { 0x1c00, 0x1cfc },
	{ 0x1d60, 0x1d64 }, { 0x1d78, 0x1d7c }, { 0x1d8c, 0x1d90 }, { 0x1db4, 0x1db8 },
	{ 0x1e40, 0x1e7c }, { 0x1e94, 0x1e94 }, { 0x1e9c, 0x1eac }, { 0x1ee0, 0x1ee8 },
	{ 0x1ef8, 0x1f7c }, { 0x1fc4, 0x1fd8 }, { 0x1ff0, 0x1ff4 },
};
static const struct rng none_m2mf[] = { { 0x100, 0x100 } };
static const struct rng none_sifm[] = { { 0x2fc, 0x2fc }, { 0x304, 0x304 } };

static bool in_rng(const struct rng *r, u32 n, u32 m)
{
	for (u32 i = 0; i < n; i++)
		if (m >= r[i].lo && m <= r[i].hi)
			return true;
	return false;
}

static void test_classification(void)
{
	for (int c = VC_3D; c < VC_N; c++)
		for (u32 m = 0x100; m < 0x2000; m += 4) {
			bool known;

			if (!test_bit(m / 4, allowed[c]))
				continue;
			known = field_of[c][m / 4] || p4_trigger(c, m) ||
				(c == VC_3D && (in_rng(c1_3d, ARRAY_SIZE(c1_3d), m) ||
						in_rng(value_3d, ARRAY_SIZE(value_3d), m) ||
						in_rng(none_3d, ARRAY_SIZE(none_3d), m))) ||
				(c == VC_M2MF && in_rng(none_m2mf, ARRAY_SIZE(none_m2mf), m)) ||
				(c == VC_SIFM && in_rng(none_sifm, ARRAY_SIZE(none_sifm), m));
			CHECK(known, "classification: class %d method %04x allowed but not classified",
			      c, m);
		}
}

/* --- the count-only build (-DPS3RSX_P4_COUNT) ---------------------------------------------- */

#ifdef PS3RSX_P4_NO_ENFORCE
/* The push dump build (Kbuild: PS3RSX_PUSH_DUMP brings PS3RSX_P4_COUNT and _NO_ENFORCE): every
 * address check counts, none refuses, and there is no switch to make one refuse */
static void test_dump(void)
{
	u64 total = 0, before = 0;
	u32 n;

	for (int r = 0; r < P4R_N; r++)
		before += p4_hits[r];
	{
		u32 w[] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x184), 0xfeed0001,
			    H(1, 1, 0), 0x3137c0de };

		CHECK(run(w, ARRAY_SIZE(w)) == 0, "dump: a slice a violation refused");
	}
	n = m2mf(V, G, A_LO + 1, G_LO, 0x1000, 0x1000, 0x1000, 0x100, 0x101, 0);
	CHECK(run(mw, n) == 0, "dump: a copy past A refused");
	n = sifm(false, 0xa, 256, A_LO, 0, 0, 3, 0, XY(64, 64), 0, XY(64, 64), 1u << 20, 1u << 20,
		 XY(64, 64), 0x10100, G_LO + G_SZ - 0x100, 0);
	CHECK(run(fw, n) == 0, "dump: a scaled image past G refused");
	CHECK(clear(surface(512, 513, LIN, 2048, A_LO, C_LO, 1, 513, 0), 0xf0) == 0,
	      "dump: a clear past A refused");
	{
		u32 p[] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x1808), 5, H(1, S3D, 0x0300), 1,
			    H(1, S3D, 0x1808), 0 };

		CHECK(run(p, ARRAY_SIZE(p)) == 0, "dump: P7 refused a state write");
	}
	for (int r = 0; r < P4R_N; r++)
		total += p4_hits[r];
	CHECK(total >= before + 6, "dump: the violations not counted (%llu)",
	      (unsigned long long)(total - before));
	/* the stable rules still refuse */
	{
		u32 w[] = { H(1, S3D, 0), 0x31337000, 0x20001000 };

		CHECK(run(w, 3) != 0, "dump: a JMP accepted");
	}
}
#endif

#if defined(PS3RSX_P4_COUNT) && !defined(PS3RSX_P4_NO_ENFORCE)
static void test_count(void)
{
	struct ps3rsx_vstate vs;
	u64 before = p4_hits[P4R_SLOT], b7 = p4_hits[P4R_P7];
	u32 w[] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x184), 0xfeed0001 };
	u32 n;

	test_p4_enforce = 0;
	vs_init(&vs, 2);
	CHECK(ps3rsx_validate(&vs, w, 4) == 0, "count: a wrong slot refused while counting");
	CHECK(p4_hits[P4R_SLOT] == before + 1, "count: the wrong slot not counted");
	CHECK(fvalid(&vs, P4F_3D_DMA + 1) && vs.f[P4F_3D_DMA + 1] == 0xfeed0001,
	      "count: the counted write's state not kept");
	test_p4_enforce = P4S_A;
	CHECK(run(w, 4) != 0, "count: slice a enforcing let a wrong slot through");
	test_p4_enforce = P4S_B;
	CHECK(run(w, 4) == 0, "count: slice b enforcing refused a slice a rule");
	n = m2mf(V, G, A_LO + 1, G_LO, 0x1000, 0x1000, 0x1000, 0x100, 0x101, 0);
	CHECK(run(mw, n) != 0, "count: slice b enforcing let a copy past A through");
	test_p4_enforce = 0;
	CHECK(run(mw, n) == 0, "count: a copy past A refused while counting");
	/* P7: state writes and binds inside an open primitive */
	{
		u32 p[] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x1808), 5, H(1, S3D, 0x1814), 0x02000000,
			    H(1, S3D, 0x1e40), 0, H(1, S3D, 0x0300), 1, H(1, S3D, 0x1808), 0 };
		u32 q[] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x1808), 5, H(1, 1, 0), 0x31337303,
			    H(1, S3D, 0x1808), 0 };
		u32 ok[] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x1808), 5,
			     NI | H(2, S3D, 0x1814), 0x02000000, 0x02000003, H(2, S3D, 0x1c00), 0, 0,
			     H(1, S3D, 0x100), 0, H(1, S3D, 0x1808), 0, H(1, S3D, 0x0300), 1 };

		CHECK(run(p, ARRAY_SIZE(p)) == 0, "p7: counted state write refused");
		CHECK(p4_hits[P4R_P7] == b7 + 1, "p7: a state write inside a primitive not counted");
		test_p4_enforce = P4S_P7;
		CHECK(run(p, ARRAY_SIZE(p)) != 0, "p7: a state write inside a primitive accepted");
		CHECK(run(q, ARRAY_SIZE(q)) != 0, "p7: a bind inside a primitive accepted");
		CHECK(run(ok, ARRAY_SIZE(ok)) == 0, "p7: vertex data, attributes and NOP refused");
	}
	/* the surface state's field rules count under slice b, the scissor's origin included */
	{
		u64 bf = p4_hits[P4R_FIELD];
		u32 f[] = { H(1, S3D, 0), 0x31337000, H(1, S3D, 0x8c0), 0x8000, H(1, S3D, 0x200),
			    16 << 16 | 1, H(1, S3D, 0x1d88), 0x2000 };

		test_p4_enforce = 0;
		CHECK(run(f, ARRAY_SIZE(f)) == 0 && p4_hits[P4R_FIELD] == bf + 3,
		      "count: a scissor origin, a surface clip origin and a COORD bit not counted");
		test_p4_enforce = P4S_B;
		CHECK(run(f, 4) != 0, "count: slice b enforcing let a scissor origin 0x8000 through");
	}
}
#endif
#ifndef PS3RSX_P4_COUNT
static void test_release_no_p7(void)
{
	u32 n = surface(512, 512, LIN, 2048, A_LO, C_LO, 1, 512, 0);
	u32 p[] = { H(1, S3D, 0x1808), 5, H(1, S3D, 0x0300), 1, H(1, S3D, 0x1808), 0 };

	memcpy(cw + n, p, sizeof(p));
	CHECK(run(cw, n + ARRAY_SIZE(p)) == 0, "release: P7 refused a state write (it is c1's)");
}
#endif

int main(void)
{
	ps3rsx_validate_init();
	ps3rsx_shadow_init();
	set_init();
#ifdef PS3RSX_P4_NO_ENFORCE
	test_set();
	test_dump();
	test_coverage();
	test_classification();
	p4set_free(&set);
	printf("p4-test (push dump build): %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
#elif defined(PS3RSX_P4_COUNT)
	test_p4_enforce = P4S_A | P4S_B | P4S_P7;	/* every slice refusing: the release's rules */
#endif
	test_set();
	test_slots();
	test_slots_own();
	test_m2mf();
	test_sifm();
	test_clear();
	test_draw();
	test_rt_fields();
	test_coverage();
	test_classification();
#if defined(PS3RSX_P4_COUNT) && !defined(PS3RSX_P4_NO_ENFORCE)
	test_count();
#elif !defined(PS3RSX_P4_COUNT)
	test_release_no_p7();
#endif
	p4set_free(&set);
	printf("p4-test%s: %d checks, %d failed\n",
#ifdef PS3RSX_P4_COUNT
	       " (count-only)",
#else
	       "",
#endif
	       checks, fails);
	return fails ? 1 : 0;
}
