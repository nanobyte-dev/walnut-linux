// SPDX-License-Identifier: GPL-2.0
/*
 * Property fuzz of the address checks, slices a and b: random streams of the methods that carry
 * addresses (context DMAs, M2MF copies, scaled images into both surface kinds, clears, fences,
 * reports, render enable), with values drawn near the edges of a random buffer set, go through
 * the validator one push at a time on one channel. For every push it accepts, an independent
 * model of the GPU (written from the design, not from validate.c) replays the words and checks
 * each memory access they make: inside one of the file's buffers in the right domain, or in the
 * channel's own fence label or query slice, and computed only from words this channel wrote
 * (fields start out holding "the kernel's" values, and a lost channel puts them back). Any
 * access outside fails. Fixed seeds: p4-fuzz SEED STREAMS (make test runs a few).
 */
#include "shim.h"
#define PS3RSX_H
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static char last_warn[256];
#define pr_warn(...) snprintf(last_warn, sizeof(last_warn), __VA_ARGS__)
#define pr_warn_ratelimited pr_warn
#include "../src/p4.h"
bool ps3rsx_known_object(u32 handle);
int ps3rsx_validate(struct ps3rsx_vstate *vs, const u32 *w, u32 nwords);
void ps3rsx_validate_init(void);
#ifndef VALIDATE_C
#define VALIDATE_C "../src/validate.c"
#endif
#include VALIDATE_C

static u64 rs;
static u32 rnd(void)
{
	rs ^= rs << 13;
	rs ^= rs >> 7;
	rs ^= rs << 17;
	return (u32)(rs >> 16);
}
static u32 rn(u32 n) { return n ? rnd() % n : 0; }

/* --- the file's buffers --------------------------------------------------------------------- */

#define NBO 10
static struct { int dom; u64 lo, hi; } bo[NBO];
static struct p4set set;

static void make_set(void)
{
	u64 at[2] = { 0x900000, 0x200000 };	/* above ps3fb's VRAM and the GART ring */

	p4set_free(&set);
	for (int i = 0; i < NBO; i++) {
		int d = i < 6 ? P4_VRAM : P4_GART;
		u64 size = 0x1000ULL << rn(8);

		at[d] += rn(3) ? 0 : 0x1000ULL * (1 + rn(16));	/* adjacent, or a gap */
		bo[i].dom = d;
		bo[i].lo = at[d];
		bo[i].hi = at[d] + size;
		at[d] += size;
		p4set_insert(&set, d, bo[i].lo, size);
	}
}

/* The independent membership test: a plain scan of the list */
static bool inside(int dom, u64 lo, u64 len)
{
	if (!len)
		return false;
	for (int i = 0; i < NBO; i++)
		if (bo[i].dom == dom && lo >= bo[i].lo && lo + len <= bo[i].hi)
			return true;
	return false;
}

/* --- the GPU model -------------------------------------------------------------------------- */

enum { M3D, MM, M2D, MSW, MSF, NCLS };
static const u32 obj[NCLS] = { 0x31337000, 0x31337303, 0x313371c3, 0x31337a73, 0x3137af00 };
static const u32 subch[NCLS] = { 7, 2, 3, 4, 5 };

struct cell { u32 v; bool mine; };
static struct cell gpu[NCLS][0x2000 / 4];
static int chan = 3;
static unsigned long accepted, triggers, checked;
static int bad;

static void forget_all(void)
{
	for (int c = 0; c < NCLS; c++)
		for (int m = 0; m < 0x2000 / 4; m++)
			gpu[c][m] = (struct cell){ rnd(), false };	/* the kernel's leftovers */
}

#define G(c, m)		(gpu[c][(m) / 4].v)
#define MINE(c, m)	(gpu[c][(m) / 4].mine)

static void violation(const char *what)
{
	if (bad++ < 20)
		printf("FAIL: accepted stream %lu: %s\n", accepted, what);
}

static bool mine_all(int c, const u16 *m, int n)
{
	for (int i = 0; i < n; i++)
		if (!MINE(c, m[i]))
			return false;
	return true;
}

static int dom(u32 h)
{
	return h == 0xfeed0000 ? P4_VRAM : h == 0xfeed0001 ? P4_GART : -1;
}

/* An access through context DMA h: [lo, lo + len) must be one of the file's buffers */
static void access(u32 h, u64 lo, u64 len, const char *what)
{
	checked++;
	if (!inside(dom(h), lo, len))
		violation(what);
}

static void copy_m2mf(void)
{
	static const u16 used[] = { 0x180, 0x184, 0x188, 0x30c, 0x310, 0x314, 0x318, 0x31c, 0x320,
				    0x324 };
	u64 n = G(MM, 0x320), len = G(MM, 0x31c);

	if (!mine_all(MM, used, ARRAY_SIZE(used))) {
		violation("M2MF copy with a value this channel never wrote");
		return;
	}
	/* the GPU as the design takes it: unsigned 32-bit pitches, 1-byte increments */
	if (G(MM, 0x324) != 0x101 || !n || n > 2047 || ((G(MM, 0x314) | G(MM, 0x318)) >> 31)) {
		violation("M2MF copy of a form the RSX's behaviour isn't known for");
		return;
	}
	access(G(MM, 0x184), G(MM, 0x30c), G(MM, 0x314) * (n - 1) + len, "M2MF source");
	access(G(MM, 0x188), G(MM, 0x310), G(MM, 0x318) * (n - 1) + len, "M2MF destination");
	if (G(MM, 0x180) != 0x66604200)
		violation("M2MF notify through another DMA");
}

static u32 bpp_sifm(u32 f)
{
	static const u8 b[16] = { 0, 2, 2, 4, 4, 2, 2, 2, 1, 1, 2, 2, 4, 4, 0, 0 };

	return f < 16 ? b[f] : 0;
}

static u32 bpp_surf(u32 f)
{
	static const u8 b[16] = { 0, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0 };

	return f < 16 ? b[f] : 0;
}

static void copy_sifm(u32 point)
{
	static const u16 used[] = { 0x184, 0x198, 0x300, 0x308, 0x30c, 0x310, 0x314, 0x318, 0x31c,
				    0x400, 0x404, 0x408 };
	u32 size = G(MSF, 0x400), osz = G(MSF, 0x314), csz = G(MSF, 0x30c);
	u32 opt = G(MSF, 0x310), cpt = G(MSF, 0x308), bpp = bpp_sifm(G(MSF, 0x300));
	u64 w = size & 0xffff, h = size >> 16, ow = osz & 0xffff, oh = osz >> 16;
	u64 u1, v1, x1, y1;

	if (!mine_all(MSF, used, ARRAY_SIZE(used))) {
		violation("scaled image with a value this channel never wrote");
		return;
	}
	if (!bpp || !w || !h || !ow || !oh) {
		violation("scaled image of an unknown form");
		return;
	}
	/* the sampled span, one texel more for the filter, and the declared size: the larger */
	u1 = (point & 0xffff) / 16 + (G(MSF, 0x318) * ow >> 20) + 2;
	v1 = (point >> 16) / 16 + (G(MSF, 0x31c) * oh >> 20) + 2;
	if (u1 < w + 1)
		u1 = w + 1;
	if (v1 < h + 1)
		v1 = h + 1;
	access(G(MSF, 0x184), G(MSF, 0x408), (G(MSF, 0x404) & 0xffff) * (v1 - 1) + u1 * bpp,
	       "scaled image source");
	x1 = (opt & 0xffff) + ow;
	y1 = (opt >> 16) + oh;
	if ((cpt & 0xffff) + (csz & 0xffff) > x1)
		x1 = (cpt & 0xffff) + (csz & 0xffff);
	if ((cpt >> 16) + (csz >> 16) > y1)
		y1 = (cpt >> 16) + (csz >> 16);
	if (G(MSF, 0x198) == 0x313371c3) {
		static const u16 u2[] = { 0x188, 0x300, 0x304, 0x30c };
		u32 b = bpp_surf(G(M2D, 0x300));

		if (!mine_all(M2D, u2, 4) || !b) {
			violation("scaled image into a surface this channel never set");
			return;
		}
		access(G(M2D, 0x188), G(M2D, 0x30c), (G(M2D, 0x304) >> 16) * (y1 - 1) + x1 * b,
		       "scaled image destination");
	} else {
		static const u16 u2[] = { 0x184, 0x300, 0x304 };
		u32 f = G(MSW, 0x300), b = bpp_surf(f & 0xffff), lu = (f >> 16) & 0xff, lv = f >> 24;

		if (!mine_all(MSW, u2, 3) || !b || lu > 12 || lv > 12) {
			violation("scaled image into a swizzled surface this channel never set");
			return;
		}
		if (x1 > (1u << lu) || y1 > (1u << lv))
			violation("scaled image outside its swizzled block");
		access(G(MSW, 0x184), G(MSW, 0x304), (1ULL << lu << lv) * b, "swizzled destination");
	}
}

static void rt(u32 slot, u32 off, u32 pitch, u32 bpp, u64 cols, u64 rows, const char *what)
{
	u32 f = G(M3D, 0x208), aa = (f >> 12) & 0xf, type = (f >> 8) & 0xf;
	u64 xs = aa == 3 || aa == 4 || aa == 5 ? 2 : 1, ys = aa == 4 || aa == 5 ? 2 : 1;
	u64 lo = off & ~63u;

	if (aa && aa != 3 && aa != 4 && aa != 5) {
		violation("clear with an unknown antialiasing mode");
		return;
	}
	if (type == 2) {
		u32 lw = (f >> 16) & 0xff, lh = f >> 24;

		if (lw > 12 || lh > 12 || cols > (1u << lw) || rows > (1u << lh) || aa) {
			violation("clear outside a swizzled surface");
			return;
		}
		access(G(M3D, slot), lo, (1ULL << lw << lh) * bpp, what);
	} else if (type == 1) {
		access(G(M3D, slot), lo, pitch * (rows * ys - 1) + cols * xs * bpp, what);
	} else {
		violation("clear of an unknown surface type");
	}
}

static u32 bpp_rt(u32 c)
{
	switch (c) {
	case 1: case 2: case 3: case 0xa: return 2;
	case 4: case 5: case 8: case 0xd: case 0xe: case 0xf: case 0x10: return 4;
	case 9: return 1;
	case 0xb: return 8;
	default: return 16;
	}
}

static void clear(u32 bits)
{
	static const u16 used[] = { 0x200, 0x204, 0x208, 0x2b8, 0x2c0, 0x2c4, 0x1d88 };
	u32 hz = G(M3D, 0x200), vt = G(M3D, 0x204), wo = G(M3D, 0x2b8), f = G(M3D, 0x208);
	u64 cols, rows;

	if (!bits)
		return;
	if (!mine_all(M3D, used, ARRAY_SIZE(used))) {
		violation("clear with a surface value this channel never wrote");
		return;
	}
	cols = (hz & 0xffff) + (hz >> 16);
	if ((wo & 0xffff) + (G(M3D, 0x2c0) >> 16) + 1 > cols)
		cols = (wo & 0xffff) + (G(M3D, 0x2c0) >> 16) + 1;
	rows = (vt & 0xffff) + (vt >> 16);
	if ((wo >> 16) + (G(M3D, 0x2c4) >> 16) + 1 > rows)
		rows = (wo >> 16) + (G(M3D, 0x2c4) >> 16) + 1;
	if ((G(M3D, 0x1d88) & 0xfff) > rows)
		rows = G(M3D, 0x1d88) & 0xfff;
	if (bits & 0xf0) {
		static const u16 off[4] = { 0x210, 0x218, 0x288, 0x28c };
		static const u16 pit[4] = { 0x20c, 0x21c, 0x280, 0x284 };
		static const u16 slot[4] = { 0x194, 0x18c, 0x1b4, 0x1b8 };
		u32 en = 1 | (MINE(M3D, 0x220) ? G(M3D, 0x220) & 0xf : 0xf);

		if (!MINE(M3D, 0x220))
			violation("colour clear with RT_ENABLE this channel never wrote");
		for (int i = 0; i < 4; i++) {
			const u16 u[3] = { off[i], pit[i], slot[i] };

			if (!(en & (1u << i)))
				continue;
			if (!mine_all(M3D, u, 3)) {
				violation("colour clear of a target this channel never set");
				continue;
			}
			rt(slot[i], G(M3D, off[i]), G(M3D, pit[i]), bpp_rt(f & 0x1f), cols, rows,
			   "colour target");
		}
	}
	if (bits & 3) {
		static const u16 u[3] = { 0x214, 0x22c, 0x198 };
		u32 z = (f >> 5) & 7;

		if (!mine_all(M3D, u, 3)) {
			violation("depth clear of a zeta this channel never set");
			return;
		}
		rt(0x198, G(M3D, 0x214), G(M3D, 0x22c), z == 1 ? 2 : z == 2 ? 4 : 8, cols, rows,
		   "zeta");
	}
}

/* The accepted words, replayed */
static u32 bound[8];

static void model_word(u32 sc, u32 m, u32 v)
{
	int c;

	if (m == 0) {
		bound[sc] = v;
		return;
	}
	for (c = 0; c < NCLS && obj[c] != bound[sc]; c++)
		;
	if (c == NCLS)
		return;
	/* triggers first, with the state before them */
	if (c == MM && m == 0x328) {
		triggers++;
		copy_m2mf();
		if (v)
			violation("M2MF notify other than 0");
		return;
	}
	if (c == MSF && m == 0x40c) {
		triggers++;
		copy_sifm(v);
		return;
	}
	if (c == M3D) {
		u32 lab = 0x20 + 32 * chan, q = 0x1000 * chan, off = v & 0xffffff;

		switch (m) {
		case 0x1d70:
		case 0x1d74:
			triggers++;
			checked++;
			if (!MINE(M3D, 0x1d6c) || !MINE(M3D, 0x1a4) || G(M3D, 0x1a4) != 0x66606660 ||
			    G(M3D, 0x1d6c) < lab || G(M3D, 0x1d6c) + 4ULL > lab + 32)
				violation("semaphore release outside the channel's label");
			return;
		case 0x1800:
			triggers++;
			checked++;
			if (!MINE(M3D, 0x1a8) || G(M3D, 0x1a8) != 0x66626660 || off < q ||
			    off + 16 > q + 0x1000)
				violation("report outside the channel's slice");
			return;
		case 0x1e98:
			if (v >> 24 == 2) {
				checked++;
				if (!MINE(M3D, 0x1a8) || off < q || off + 16 > q + 0x1000)
					violation("render enable reading outside the slice");
			}
			break;
		case 0x1d94:
			triggers++;
			clear(v);
			return;
		}
	}
	gpu[c][m / 4] = (struct cell){ v, true };
}

static void model_push(const u32 *w, u32 n)
{
	for (u32 i = 0; i < n;) {
		u32 h = w[i], cnt = (h >> 18) & 0x7ff, sc = (h >> 13) & 7, m = h & 0x1ffc;
		bool ni = h & 0x40000000u;

		for (u32 k = 0; k < cnt; k++)
			model_word(sc, ni ? m : m + 4 * k, w[i + 1 + k]);
		i += 1 + cnt;
	}
}

/* --- the generator -------------------------------------------------------------------------- */

#define HD(n, sc, m)	(((u32)(n) << 18) | ((u32)(sc) << 13) | (m))

static u32 pw[512], pn;

static void put(int c, u32 m, const u32 *v, u32 n)
{
	pw[pn++] = HD(n, subch[c], m);
	for (u32 i = 0; i < n; i++)
		pw[pn++] = v[i];
}

static void put1(int c, u32 m, u32 v)
{
	put(c, m, &v, 1);
}

static u32 handle(void)
{
	static const u32 h[] = { 0, 0xfeed0000, 0xfeed0001, 0x66604200, 0x66606660, 0x66626660 };

	return rn(12) ? (rn(2) ? 0xfeed0000 : 0xfeed0001) : h[rn(ARRAY_SIZE(h))];
}

/* An offset at or near a buffer's edges in the domain of h, or anything */
static u32 offset(u32 h, u64 len)
{
	int d = dom(h), i;

	if (d < 0 || !rn(8))
		return rnd();
	do
		i = rn(NBO);
	while (bo[i].dom != d);
	switch (rn(5)) {
	case 0: return bo[i].lo;
	case 1: return bo[i].hi - len;
	case 2: return bo[i].hi - len + (rn(2) ? 1 : 64);
	case 3: return bo[i].lo - (rn(2) ? 1 : 64);
	default: return bo[i].lo + rn(bo[i].hi - bo[i].lo);
	}
}

/* A size: mostly 1 to max (max itself often), now and then 0, max + 1 or anything */
static u32 small(u32 max)
{
	switch (rn(16)) {
	case 0: return 0;
	case 1: return rnd();
	case 2: return max + 1;
	case 3: case 4: case 5: return max;
	default: return 1 + rn(max);
	}
}

static void gen_m2mf(void)
{
	u32 din = handle(), dout = handle(), lines = small(64), len = 1 + rn(0x2000);
	u32 pin = rn(4) ? len + rn(256) : small(0x10000), pout = rn(4) ? len : small(0x10000);
	u32 v[8] = { offset(din, (u64)pin * (lines ? lines - 1 : 0) + len),
		     offset(dout, (u64)pout * (lines ? lines - 1 : 0) + len), pin, pout,
		     rn(8) ? len : small(0x2000), lines, rn(10) ? 0x101 : rnd() & 0xffff,
		     rn(16) ? 0 : 1 };
	u32 d[3] = { rn(10) ? 0x66604200 : handle(), din, dout };

	if (rn(4))
		put(MM, 0x180, d, 3);
	if (rn(6)) {
		put(MM, 0x30c, v, 8);
	} else {				/* a partial setup, then the launch alone */
		u32 k = rn(8);

		put(MM, 0x30c + 4 * k, &v[k], 1);
		put1(MM, 0x328, 0);
	}
}

static void gen_sifm(void)
{
	bool swz = rn(2);
	u32 dh = handle(), sh = handle(), cf = rn(10) ? 3 : rn(16), w = small(128), h = small(128);
	u32 ow = rn(4) ? w : small(128), oh = rn(4) ? h : small(128), pitch = rn(4) ? w * 4 : small(1024);
	u32 dpitch = rn(4) ? ow * 4 : small(1024), lu = rn(4) ? 7 : rn(14), lv = rn(4) ? 7 : rn(14);

	if (!swz) {
		u32 d[3] = { 0x66604200, dh, dh }, s[4] = { rn(8) ? 0xa : rn(16),
				dpitch << 16 | dpitch, 0, offset(dh, (u64)dpitch * oh) };

		s[2] = s[3];
		if (rn(4))
			put(M2D, 0x180, d, 3);
		if (rn(4))
			put(M2D, 0x300, s, 4);
	} else {
		u32 d[2] = { 0x66604200, dh }, s[2] = { (rn(8) ? 0xa : rn(16)) | lu << 16 | lv << 24,
							   offset(dh, (4ULL << lu) << lv) };

		if (rn(4))
			put(MSW, 0x180, d, 2);
		if (rn(4))
			put(MSW, 0x300, s, 2);
	}
	{
		u32 d[2] = { 0x66604200, sh };
		u32 st[8] = { cf, 3, rn(6) ? 0 : rnd(), (rn(6) ? oh : small(128)) << 16 | ow,
			      rn(6) ? 0 : small(64) << 16 | small(64), oh << 16 | ow,
			      rn(6) ? 1u << 20 : rnd() >> rn(16), rn(6) ? 1u << 20 : rnd() >> rn(16) };
		u32 src[4] = { h << 16 | w, (rn(8) ? 0x10000 : rnd() & 0xff0000) | (pitch & 0xffff),
			       offset(sh, (u64)pitch * (h + 2) + 4ULL * (w + 2)),
			       rn(4) ? 0 : rnd() };

		if (rn(4))
			put(MSF, 0x180, d, 2);
		if (rn(4))
			put1(MSF, 0x198, swz ? 0x31337a73 : 0x313371c3);
		if (rn(4))
			put(MSF, 0x300, st, 8);
		put(MSF, 0x400, src, 4);
	}
}

static void gen_clear(void)
{
	u32 w = small(512), h = small(512), pitch = rn(4) ? w * 4 : small(0x4000), slot = handle();
	u32 fmt = (rn(6) ? 0x148 : rnd() & 0xffff) | (rn(4) ? 0 : (rnd() & 0x7) << 12);
	u32 surf[3] = { (rn(8) ? 0 : rn(16)) | w << 16, (rn(8) ? 0 : rn(16)) | h << 16, fmt };
	u32 win[4] = { rn(4) ? 0 : rn(8) ? rn(4) << 16 | rn(4) : rnd() & 0x00ff00ff, 0,
		       ((w ? w : 1) - 1) << 16,
		       ((h ? h : 1) - 1) << 16 };
	u32 c[3] = { pitch, offset(slot, (u64)pitch * h), offset(slot, (u64)pitch * h) };

	if (rn(4))
		put1(M3D, 0x194, slot);
	if (rn(4))
		put1(M3D, 0x198, slot);
	if (rn(4))
		put(M3D, 0x200, surf, 3);
	if (rn(4))
		put(M3D, 0x2b8, win, 4);
	if (rn(4))
		put1(M3D, 0x22c, rn(4) ? pitch : small(0x4000));
	if (rn(4))
		put(M3D, 0x20c, c, 3);
	if (rn(4))
		put1(M3D, 0x220, rn(4) ? 1 : rn(32));
	if (rn(4))
		put1(M3D, 0x1d88, rn(4) ? h : rnd() & 0x1fff);
	put1(M3D, 0x1d94, rn(4) ? 0xf3 : rnd() & 0x1ff);
}

static void gen_slots_own(void)
{
	u32 lab = 0x20 + 32 * chan, q = 0x1000 * chan;

	switch (rn(3)) {
	case 0: {
		u32 v[2] = { rn(4) ? lab + 4 * rn(8) : rnd() & 0xfffc, 1 };

		if (rn(4))
			put1(M3D, 0x1a4, rn(8) ? 0x66606660 : handle());
		put(M3D, 0x1d6c, v, 2);
		break;
	}
	case 1:
		if (rn(4))
			put1(M3D, 0x1a8, rn(8) ? 0x66626660 : handle());
		put1(M3D, 0x1800, (1 + rn(6)) << 24 | (rn(4) ? q + 16 * rn(256) : rnd() & 0xffffff));
		break;
	default:
		put1(M3D, 0x1e98, rn(3) ? 0x02000000 | (rn(4) ? q + 16 * rn(256) : rnd() & 0xffffff) :
					  rnd());
	}
}

int main(int argc, char **argv)
{
	u32 seed = argc > 1 ? strtoul(argv[1], NULL, 0) : 1;
	unsigned long streams = argc > 2 ? strtoul(argv[2], NULL, 0) : 20000, i;
	struct ps3rsx_vstate vs;

	rs = 0x9e3779b97f4a7c15ULL ^ seed;
	ps3rsx_validate_init();
	for (i = 0; i < streams; i++) {
		if (i % 500 == 0) {		/* a new file and channel, a new set */
			make_set();
			chan = rn(14);
			memset(&vs, 0, sizeof(vs));
			vs.chan_id = chan;
			vs.set = &set;
			forget_all();
			memset(bound, 0, sizeof(bound));
			pn = 0;
			for (int c = 0; c < NCLS; c++)
				put1(c, 0, obj[c]);
			if (!ps3rsx_validate(&vs, pw, pn))
				model_push(pw, pn);
		}
		if (!rn(200)) {			/* the GPU recovered under the channel */
			ps3rsx_vstate_forget(&vs);
			forget_all();
		}
		pn = 0;
		for (int k = rn(4) ? 1 : 2 + rn(3); k > 0; k--)
			switch (rn(4)) {
			case 0: gen_m2mf(); break;
			case 1: gen_sifm(); break;
			case 2: gen_clear(); break;
			default: gen_slots_own(); break;
			}
		{
			struct ps3rsx_vstate t = vs;

			if (!ps3rsx_validate(&t, pw, pn)) {
				vs = t;
				accepted++;
				model_push(pw, pn);
			}
		}
	}
	p4set_free(&set);
	printf("p4-fuzz seed %u: %lu streams, %lu accepted, %lu triggers, %lu accesses checked: %s\n",
	       seed, streams, accepted, triggers, checked, bad ? "FAILED" : "ok");
	if (!bad && (accepted < streams / 50 || checked < streams / 20)) {
		printf("FAIL: too few accepted streams to mean anything\n");
		return 1;
	}
	return bad ? 1 : 0;
}
