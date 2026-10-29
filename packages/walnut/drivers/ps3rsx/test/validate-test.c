// SPDX-License-Identifier: GPL-2.0
/*
 * The validator (validate.c's fast path and check_method()) against the streams that raised
 * graph exceptions on the RSX (docs/RSX.md, "Errors and what wedges the GPU"):
 * - vertex formats: every type and size in every VTXFMT slot, as one write, an incrementing burst
 *   and a non-incrementing one, against the rule spelled out here independently: type 0 is
 *   refused (the RSX lacks B8G8R8A8_UNORM), type 7 (UB256) only with 4 components or disabled
 *   (size 0; 3 components raised 0x106 with Mesa 0043), everything else passes; plus the words
 *   Mesa sends: its float, short and byte formats, 0043's UB256 x4;
 * - texture formats: X16 (0x14, mpv's dither texture: 0x105) refused in every fragment unit,
 *   every other format passes; mpv's first frame as captured (fixtures/) refused, and passed with
 *   its dither texture as L8 and as Mesa 0052 sends it; WebKit's composited frame (fixtures/,
 *   GPU compositing in Badwolf) passes;
 * - texture offsets not 128-byte aligned (Neverball's +0xaa0 blit source: 0x105), swizzled render
 *   targets 1 pixel wide (0x106), M2MF methods on the surface-2D object (0x109);
 * - the vertex texture units (0x900-0x97c, task #16): every format byte and every low byte
 *   (location, cubic, border, dimension) of FORMAT, and good and bad words of each of the eight
 *   methods in each unit, as one write, in a burst and non-incrementing; Mesa's CONTROL0 = 0 and
 *   libgcm-style setups pass;
 * - the primitive order: BEGIN only with no primitive open, END and vertex data (elements,
 *   batches, inline data) only inside one, across pushes as Mesa splits draws;
 * - recovery (recover_cases(): submit.c's handling of a rejected push, modelled per submit):
 *   reject then recover, a rejected state push followed by a draw relying on it, a draw split
 *   across a rejected submit, repeated rejections, a client that doesn't recover, mpv's first
 *   frame cancelled and sent again from Mesa 0052. What reaches the "GPU" (the accepted
 *   pushes and the kernel's own END, in order) must pass the validator as one stream, and its
 *   state (shadow.c) must be only what accepted pushes set.
 * Built in userspace with inc/ (kernel stand-ins) by the Makefile here.
 */
#include "shim.h"
#define PS3RSX_H	/* the driver header: not in userspace */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static int verbose;
static char last_warn[256];
#define pr_warn(...) do { snprintf(last_warn, sizeof(last_warn), __VA_ARGS__); \
			  if (verbose) printf("%s", last_warn); } while (0)
#define pr_warn_ratelimited pr_warn

struct ps3rsx_vstate {
	u32 bound[8];
	u8 cls[8];
	bool prim_open;
};
bool ps3rsx_known_object(u32 handle);
int ps3rsx_validate(struct ps3rsx_vstate *vs, const u32 *w, u32 nwords);
void ps3rsx_validate_init(void);

#ifndef VALIDATE_C
#define VALIDATE_C "../src/validate.c"
#endif
#include VALIDATE_C

#define RESTORE_MAX_WORDS	(16 * 1024)	/* as in ps3rsx.h */
#ifndef SHADOW_C
#define SHADOW_C "../src/shadow.c"
#endif
#include SHADOW_C

#include "fixtures/mpv-vo-gpu-0x105.h"
#include "fixtures/webkit-compositing.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static bool expect_ok(u32 data)
{
	unsigned type = data & 0xf, size = (data >> 4) & 0xf;

	if (type == 0)
		return false;
	if (type == 7)
		return size == 0 || size == 4;
	return true;
}

#define HDR(n, m) (((u32)(n) << 18) | (7u << 13) | (m))
#define HDRS(n, sc, m) (((u32)(n) << 18) | ((u32)(sc) << 13) | (m))
#define NONINC 0x40000000u

/* a stream through the validator with a fresh channel state; 0 or the error */
static int run(const u32 *w, u32 n)
{
	struct ps3rsx_vstate vs;

	memset(&vs, 0, sizeof(vs));
	last_warn[0] = 0;
	return ps3rsx_validate(&vs, w, n);
}

/* mpv's binds, then its first frame (patched or not): one channel, as the driver runs them */
static int run_mpv(const u32 *frame, u32 n)
{
	struct ps3rsx_vstate vs;
	int r;

	memset(&vs, 0, sizeof(vs));
	r = ps3rsx_validate(&vs, mpv_init, ARRAY_SIZE(mpv_init));
	if (r)
		return 1000 + r;
	last_warn[0] = 0;
	return ps3rsx_validate(&vs, frame, n);
}

/* bind the 3D object on subchannel 7, then the words */
static int push(const u32 *words, u32 n)
{
	struct ps3rsx_vstate vs;
	u32 w[64];

	memset(&vs, 0, sizeof(vs));
	w[0] = HDR(1, 0);
	w[1] = 0x31337000;
	memcpy(&w[2], words, n * 4);
	return ps3rsx_validate(&vs, w, n + 2);
}

/*
 * The vertex texture units (0x900 + 0x20 * unit): the rule as libgcm writes the words, spelled
 * out by example here rather than from validate.c.
 */
struct vtcase { u32 reg, data; bool ok; const char *what; };

static const struct vtcase vtcases[] = {
	/* OFFSET: 128-byte aligned, below 256 MiB */
	{ 0x00, 0x00000000, true, "offset 0" },
	{ 0x00, 0x01943000, true, "offset +0x1943000" },
	{ 0x00, 0x0fffff80, true, "offset 256 MiB - 128" },
	{ 0x00, 0x01943aa0, false, "offset +0xaa0 (Neverball's)" },
	{ 0x00, 0x00000040, false, "offset 64-byte aligned" },
	{ 0x00, 0x10000000, false, "offset 256 MiB" },
	{ 0x00, 0xfffff000, false, "offset 4 GiB - 4 KiB" },
	/* FORMAT: DMA, border colour, 2D, X32_FLOAT or W32_Z32_Y32_X32_FLOAT, 1 to 13 levels */
	{ 0x04, 0x00019b29, true, "RGBA32F swizzled, VRAM, 1 level" },
	{ 0x04, 0x0001bb29, true, "RGBA32F linear, VRAM" },
	{ 0x04, 0x00019c2a, true, "X32F swizzled, XDR" },
	{ 0x04, 0x0007bc2a, true, "X32F linear, XDR, 7 levels" },
	{ 0x04, 0x000d9b29, true, "13 levels (4096 down to 1)" },
	{ 0x04, 0x00009b29, false, "0 levels" },
	{ 0x04, 0x000e9b29, false, "14 levels" },
	{ 0x04, 0x01019b29, false, "a base size in bits 24-27 (NV30's)" },
	{ 0x04, 0x00019b28, false, "location 0" },
	{ 0x04, 0x00019b2b, false, "location 3" },
	{ 0x04, 0x00019b2d, false, "cubic" },
	{ 0x04, 0x00019b21, false, "border texels (bit 3 clear)" },
	{ 0x04, 0x00019b19, false, "1D" },
	{ 0x04, 0x00019b39, false, "3D" },
	{ 0x04, 0x0001db29, false, "RGBA32F unnormalized (UN, 0x40)" },
	{ 0x04, 0x00019a29, false, "RGBA16F (0x9a)" },
	{ 0x04, 0x00019f29, false, "Y16_X16_FLOAT (0x9f)" },
	{ 0x04, 0x00018529, false, "A8R8G8B8 (0x85)" },
	{ 0x04, 0x00019429, false, "X16 (0x94, mpv's 0x105)" },
	{ 0x04, 0x00000000, false, "the word 0" },
	/* ADDRESS: wrap S and T 1 to 8, nothing else */
	{ 0x08, 0x00000101, true, "wrap/wrap" },
	{ 0x08, 0x00000303, true, "clamp to edge" },
	{ 0x08, 0x00000808, true, "mirror once clamp" },
	{ 0x08, 0x00000102, true, "mirror S, wrap T" },
	{ 0x08, 0x00000000, false, "wrap 0" },
	{ 0x08, 0x00000100, false, "wrap S 0" },
	{ 0x08, 0x00000009, false, "wrap T 0, S 9" },
	{ 0x08, 0x00000901, false, "wrap T 9" },
	{ 0x08, 0x00010101, false, "a wrap R" },
	{ 0x08, 0x00000131, false, "aniso mip optimization bits" },
	{ 0x08, 0x70000101, false, "a depth compare" },
	{ 0x08, 0x0000f101, false, "unsigned remap bits" },
	/* CONTROL0: enable, LODs; no anisotropy, no alpha kill */
	{ 0x0c, 0x00000000, true, "disabled (Mesa's nv40_verttex.c)" },
	{ 0x0c, 0x80000000, true, "enabled, LOD 0..0" },
	{ 0x0c, 0x8007ff80, true, "enabled, LOD 0..15.99" },
	{ 0x0c, 0xfff80000, true, "min LOD 15.99" },
	{ 0x0c, 0x80000010, false, "anisotropy 2x" },
	{ 0x0c, 0x80000004, false, "alpha kill" },
	{ 0x0c, 0x80000001, false, "bit 0" },
	/* CONTROL3: pitch at most 65536, depth 0 or 1 */
	{ 0x10, 0x00000000, true, "pitch 0 (swizzled)" },
	{ 0x10, 0x00100400, true, "pitch 1024, depth 1" },
	{ 0x10, 0x00010000, true, "pitch 65536 (4096 RGBA32F)" },
	{ 0x10, 0x00010010, false, "pitch 65552" },
	{ 0x10, 0x000fffff, false, "pitch 1 MiB - 1" },
	{ 0x10, 0x00200400, false, "depth 2" },
	/* FILTER: only the LOD bias */
	{ 0x14, 0x00000000, true, "no bias" },
	{ 0x14, 0x00001fff, true, "bias -1/256" },
	{ 0x14, 0x00002000, false, "bit 13" },
	{ 0x14, 0x01012000, false, "a fragment unit's filter word" },
	/* IMAGE_RECT: width (high) and height (low) 1 to 4096 */
	{ 0x18, 0x00010001, true, "1x1" },
	{ 0x18, 0x00400040, true, "64x64" },
	{ 0x18, 0x10001000, true, "4096x4096" },
	{ 0x18, 0x10000001, true, "4096x1" },
	{ 0x18, 0x00000040, false, "width 0" },
	{ 0x18, 0x00400000, false, "height 0" },
	{ 0x18, 0x10010001, false, "width 4097" },
	{ 0x18, 0x00011001, false, "height 4097" },
	{ 0x18, 0xffffffff, false, "65535x65535" },
	/* BORDER_COLOR: any colour */
	{ 0x1c, 0x00000000, true, "border 0" },
	{ 0x1c, 0xffffffff, true, "border white" },
	{ 0x1c, 0x12345678, true, "border anything" },
};

/* a whole unit as libgcm sets one up, all eight words: RGBA32F linear 64x64 in VRAM */
static const u32 vt_good[8] = { 0x01943000, 0x0001bb29, 0x00000303, 0x80000000, 0x00000400,
				0x00000000, 0x00400040, 0x00000000 };

static void vtxtex_cases(void)
{
	for (u32 unit = 0; unit < 4; unit++) {
		const u32 base = 0x900 + 0x20 * unit;
		u32 burst[9];
		int r;

		/* the good unit in one burst, and every word as its own write */
		burst[0] = HDR(8, base);
		memcpy(&burst[1], vt_good, sizeof(vt_good));
		r = push(burst, 9);
		CHECK(r == 0, "vertex unit %u: libgcm-style setup refused (%s)", unit, last_warn);
		for (u32 k = 0; k < 8; k++) {
			u32 one[2] = { HDR(1, base + 4 * k), vt_good[k] };

			CHECK(push(one, 2) == 0, "vertex unit %u: good word %u refused", unit, k);
		}

		for (u32 i = 0; i < ARRAY_SIZE(vtcases); i++) {
			const struct vtcase *c = &vtcases[i];
			const u32 m = base + c->reg;
			u32 one[2] = { HDR(1, m), c->data };
			u32 ni[4] = { NONINC | HDR(3, m), vt_good[c->reg / 4], c->data,
				      vt_good[c->reg / 4] };

			r = push(one, 2);
			CHECK((r == 0) == c->ok, "vertex unit %u %03x %08x (%s): %d, expected %s",
			      unit, m, c->data, c->what, r, c->ok ? "accept" : "refuse");
			if (r && !c->ok)
				CHECK(strstr(last_warn, "vertex texture") != NULL,
				      "vertex unit %u %s: refused for '%s'", unit, c->what, last_warn);
			memcpy(&burst[1], vt_good, sizeof(vt_good));
			burst[1 + c->reg / 4] = c->data;
			r = push(burst, 9);
			CHECK((r == 0) == c->ok, "vertex unit %u %s in a burst: %d", unit, c->what, r);
			r = push(ni, 4);
			CHECK((r == 0) == c->ok, "vertex unit %u %s non-incrementing: %d", unit,
			      c->what, r);
		}

		/* FORMAT: every format byte (only 0x9b, 0x9c, swizzled or linear), and every low
		 * byte (only DMA0 or DMA1 with border colour, 2D) */
		for (u32 b = 0; b < 0x100; b++) {
			const bool fmt_ok = b == 0x9b || b == 0x9c || b == 0xbb || b == 0xbc;
			const bool low_ok = b == 0x29 || b == 0x2a;
			u32 f1[2] = { HDR(1, base + 4), 0x00010029 | b << 8 };
			u32 f2[2] = { HDR(1, base + 4), 0x00019b00 | b };

			CHECK((push(f1, 2) == 0) == fmt_ok, "vertex unit %u: format byte %02x", unit, b);
			CHECK((push(f2, 2) == 0) == low_ok, "vertex unit %u: format low byte %02x",
			      unit, b);
		}
	}

	/* the fragment units keep their own rules: an A8R8G8B8 texture and a fragment filter
	 * word still pass there */
	{
		u32 frag[3] = { HDR(2, 0x1a04), 0x00018529, 0x00000101 };
		u32 filt[2] = { HDR(1, 0x1a14), 0x01012000 };

		CHECK(push(frag, 3) == 0, "fragment unit 0: A8R8G8B8 refused (%s)", last_warn);
		CHECK(push(filt, 2) == 0, "fragment unit 0: filter word refused (%s)", last_warn);
	}

	/* a burst from the last vertex unit's border colour into 0x980 (not allowed) */
	{
		u32 over[3] = { HDR(2, 0x97c), 0, 0 };

		CHECK(push(over, 3) != 0, "burst past the vertex texture units accepted");
	}
}

/* --- recovery: what submit.c does with a rejected push -------------------------------- */

/* What reached the "GPU", in order: the accepted pushes and the kernel's own END */
static u32 gpu[1 << 16], ngpu;

static void gpu_put(const u32 *w, u32 n)
{
	if (ngpu + n > ARRAY_SIZE(gpu)) {
		printf("FAIL: test GPU stream full\n");
		exit(1);
	}
	memcpy(&gpu[ngpu], w, n * 4);
	ngpu += n;
}

/* A channel as submit.c keeps it: validator state, recovering client or not */
struct tchan {
	struct ps3rsx_vstate vs;
	bool recover, broken;
	u32 cancelled;
};

/*
 * One submit of npush pushes, as ps3rsx_ioctl_gem_pushbuf() and submit_rejected() run it: 0, or
 * -ECANCELED (rejected, the client recovers), -EINVAL (rejected, it doesn't: refused from now
 * on), -EIO (refused). Every push is checked against a copy of the channel's state, kept only
 * if all pass; a rejected submit sends nothing, except the END of a primitive the channel's
 * last accepted submit left open.
 */
static int submit(struct tchan *ch, u32 npush, const u32 *const *p, const u32 *n)
{
	struct ps3rsx_vstate vs;
	u32 i, sc;

	if (ch->broken)
		return -EIO;
	vs = ch->vs;
	for (i = 0; i < npush; i++) {
		if (!ps3rsx_validate(&vs, p[i], n[i]))
			continue;
		if (!ch->recover) {
			ch->broken = true;
			return -EINVAL;
		}
		if (ch->vs.prim_open) {
			u32 end[2] = { 0, 0 };

			for (sc = 0; sc < 8 && ch->vs.bound[sc] != 0x31337000; sc++)
				;
			end[0] = HDRS(1, sc & 7, 0x1808);
			gpu_put(end, 2);
			ch->vs.prim_open = false;
		}
		ch->cancelled++;
		return -ECANCELED;
	}
	for (i = 0; i < npush; i++)
		gpu_put(p[i], n[i]);
	ch->vs = vs;
	return 0;
}

#define SUB1(ch, a) submit(ch, 1, (const u32 *const[]){ a }, (const u32[]){ ARRAY_SIZE(a) })
#define SUB2(ch, a, b) submit(ch, 2, (const u32 *const[]){ a, b }, \
			      (const u32[]){ ARRAY_SIZE(a), ARRAY_SIZE(b) })

/* The GPU stream as one push through a fresh validator state: 0 if every word passes */
static int gpu_whole(void)
{
	struct ps3rsx_vstate vs;

	memset(&vs, 0, sizeof(vs));
	last_warn[0] = 0;
	return ps3rsx_validate(&vs, gpu, ngpu);
}

/* The 3D state the GPU holds after the stream (shadow.c's replay of it) */
static struct ps3rsx_shadow *gpu_state(void)
{
	struct ps3rsx_shadow *sh = ps3rsx_shadow_new();

	ps3rsx_shadow_push(sh, gpu, ngpu);
	return sh;
}

static u32 reg3d(const struct ps3rsx_shadow *sh, u32 m)
{
	return test_bit(m / 4, sh->reg3d_set) ? sh->reg3d[m / 4] : 0xdeadbeef;
}

static void tchan_init(struct tchan *ch, bool recover)
{
	static const u32 bind[] = { HDR(1, 0), 0x31337000 };

	memset(ch, 0, sizeof(*ch));
	ch->recover = recover;
	ngpu = 0;
	CHECK(SUB1(ch, bind) == 0, "3D bind refused");
}

/* Mesa-like words, on the 3D object at subchannel 7 */
#define L8_A	0x00018129	/* TEX_FORMAT unit 0: L8, DMA0, 2D, 1 level */
#define L8_C	0x00028129	/* the same, 2 levels */
#define X16	0x00019429	/* X16: refused (0x105) */
static const u32 state_a[] = { HDR(2, 0x1a00), 0x01943000, L8_A, HDR(1, 0x1740), 0x22 };
static const u32 state_c[] = { HDR(2, 0x1a00), 0x01a00000, L8_C, HDR(1, 0x1740), 0x32 };
static const u32 state_b_ok[] = { HDR(1, 0x1740), 0x32, HDR(1, 0x1a00), 0x02000000 };
static const u32 state_b_bad[] = { HDR(1, 0x1a04), X16 };
static const u32 draw[] = { HDR(1, 0x1808), 5, NONINC | HDR(1, 0x1814), 0x02000000,
			    HDR(1, 0x1808), 0 };
static const u32 begin_inline[] = { HDR(1, 0x1808), 5, NONINC | HDR(4, 0x1818), 1, 2, 3, 4 };
static const u32 inline_end[] = { NONINC | HDR(2, 0x1818), 5, 6, HDR(1, 0x1808), 0 };
static const u32 inline_only[] = { NONINC | HDR(2, 0x1818), 7, 8 };
static const u32 end_only[] = { HDR(1, 0x1808), 0 };

static void recover_cases(void)
{
	struct ps3rsx_shadow *sh;
	struct tchan ch;
	u32 before, i;
	int r;

	/* the primitive order, one push each (vertex data only inside BEGIN/END) */
	{
		static const u32 nested[] = { HDR(1, 0x1808), 5, HDR(1, 0x1808), 5 };
		static const u32 elems_out[] = { NONINC | HDR(2, 0x180c), 0x00010000, 0x00020003 };
		static const u32 elem32_out[] = { HDR(1, 0x1810), 7 };
		static const u32 batch_out[] = { NONINC | HDR(1, 0x1814), 0x02000000 };
		static const u32 index_out[] = { NONINC | HDR(1, 0x1824), 0x02000000 };
		static const u32 idxbuf[] = { HDR(2, 0x181c), 0x01900000, 0x10 };
		static const u32 elems_in[] = { HDR(1, 0x1808), 5, HDR(2, 0x181c), 0x01900000,
						0x10, NONINC | HDR(1, 0x1824), 0x02000000,
						HDR(1, 0x180c), 0x00010000, HDR(1, 0x1808), 0 };

		CHECK(push(draw, ARRAY_SIZE(draw)) == 0, "a draw refused (%s)", last_warn);
		CHECK(push(begin_inline, ARRAY_SIZE(begin_inline)) == 0, "BEGIN + inline data refused");
		CHECK(push(nested, ARRAY_SIZE(nested)) != 0, "a BEGIN in an open primitive accepted");
		CHECK(push(end_only, 2) != 0, "an END with no primitive open accepted");
		CHECK(push(inline_only, 3) != 0, "inline data outside a primitive accepted");
		CHECK(push(elems_out, 3) != 0, "U16 elements outside a primitive accepted");
		CHECK(push(elem32_out, 2) != 0, "a U32 element outside a primitive accepted");
		CHECK(push(batch_out, 2) != 0, "a vertex batch outside a primitive accepted");
		CHECK(push(index_out, 2) != 0, "an index batch outside a primitive accepted");
		CHECK(push(idxbuf, 3) == 0, "the index buffer's offset and format refused");
		CHECK(push(elems_in, ARRAY_SIZE(elems_in)) == 0, "an indexed draw refused (%s)",
		      last_warn);
	}

	/* 1. reject then recover: the rejected submit leaves no trace, the next one draws */
	tchan_init(&ch, true);
	CHECK(SUB2(&ch, state_a, draw) == 0, "1: state A and a draw refused");
	before = ngpu;
	CHECK(SUB2(&ch, state_b_bad, draw) == -ECANCELED, "1: the X16 submit not cancelled");
	CHECK(ngpu == before, "1: the cancelled submit sent %u words", ngpu - before);
	r = SUB2(&ch, state_c, draw);
	CHECK(r == 0, "1: the state sent again and a draw after the cancel: %d (%s)", r, last_warn);
	CHECK(gpu_whole() == 0, "1: the GPU's stream fails as one stream (%s)", last_warn);
	sh = gpu_state();
	CHECK(reg3d(sh, 0x1a04) == L8_C && reg3d(sh, 0x1a00) == 0x01a00000 &&
	      reg3d(sh, 0x1740) == 0x32, "1: the GPU state isn't state C");
	ps3rsx_shadow_free(sh);

	/*
	 * 2. a rejected state push, then a draw that counts on it: the submit's good push went
	 * nowhere either, and the draw runs on state A, which passed (nothing of B)
	 */
	tchan_init(&ch, true);
	CHECK(SUB2(&ch, state_a, draw) == 0, "2: state A and a draw refused");
	before = ngpu;
	CHECK(SUB2(&ch, state_b_ok, state_b_bad) == -ECANCELED, "2: state B not cancelled");
	CHECK(ngpu == before, "2: the cancelled submit sent %u words", ngpu - before);
	CHECK(SUB1(&ch, draw) == 0, "2: the draw after the cancel refused (%s)", last_warn);
	CHECK(gpu_whole() == 0, "2: the GPU's stream fails as one stream (%s)", last_warn);
	sh = gpu_state();
	CHECK(reg3d(sh, 0x1740) == 0x22 && reg3d(sh, 0x1a00) == 0x01943000 &&
	      reg3d(sh, 0x1a04) == L8_A, "2: the draw ran on state from the cancelled submit");
	ps3rsx_shadow_free(sh);
	/* a bind in a cancelled submit doesn't count: methods on that subchannel are refused */
	{
		static const u32 bind_bad[] = { HDRS(1, 3, 0), 0x31337303, HDR(1, 0x1a04), X16 };
		static const u32 m2mf3[] = { HDRS(8, 3, 0x30c), 0x2fc000, 0x1945000, 0x1f0, 0x1f0,
					     0x1f0, 1, 0x101, 0 };

		before = ngpu;
		CHECK(SUB1(&ch, bind_bad) == -ECANCELED, "2: a bind with X16 not cancelled");
		CHECK(SUB1(&ch, m2mf3) == -ECANCELED && strstr(last_warn, "no object bound"),
		      "2: a copy through the cancelled bind not cancelled (%s)", last_warn);
		CHECK(ngpu == before, "2: the cancelled bind's copy sent %u words", ngpu - before);
	}

	/* 3. a draw split across submits, its rest in a rejected one: the kernel ends it */
	tchan_init(&ch, true);
	CHECK(SUB2(&ch, state_a, begin_inline) == 0, "3: a draw left open refused");
	CHECK(ch.vs.prim_open, "3: the primitive isn't open");
	before = ngpu;
	CHECK(SUB2(&ch, inline_end, state_b_bad) == -ECANCELED, "3: its rest with X16 not cancelled");
	CHECK(ngpu == before + 2 && gpu[before] == HDR(1, 0x1808) && gpu[before + 1] == 0,
	      "3: no END from the kernel (%u words)", ngpu - before);
	CHECK(!ch.vs.prim_open, "3: the primitive stayed open");
	before = ngpu;
	CHECK(SUB1(&ch, inline_end) == -ECANCELED, "3: inline data after the END not cancelled");
	CHECK(SUB1(&ch, inline_only) == -ECANCELED, "3: inline data not cancelled");
	CHECK(SUB1(&ch, end_only) == -ECANCELED, "3: a second END not cancelled");
	CHECK(ngpu == before, "3: the continuation sent %u words", ngpu - before);
	CHECK(SUB2(&ch, state_c, begin_inline) == 0 && SUB1(&ch, inline_end) == 0,
	      "3: a new split draw refused (%s)", last_warn);
	CHECK(gpu_whole() == 0, "3: the GPU's stream fails as one stream (%s)", last_warn);
	/* the BEGIN in a rejected submit: its continuation has no primitive to go into */
	tchan_init(&ch, true);
	CHECK(SUB1(&ch, state_a) == 0, "3: state A refused");
	before = ngpu;
	CHECK(SUB2(&ch, state_b_bad, begin_inline) == -ECANCELED, "3: X16 + BEGIN not cancelled");
	CHECK(SUB1(&ch, inline_end) == -ECANCELED, "3: the rest of a cancelled BEGIN not cancelled");
	CHECK(ngpu == before, "3: the cancelled BEGIN's rest sent %u words", ngpu - before);
	CHECK(gpu_whole() == 0, "3: the GPU's stream fails as one stream (%s)", last_warn);

	/* 4. repeated rejections: each one cancelled, the good frames between them drawn */
	tchan_init(&ch, true);
	CHECK(SUB2(&ch, state_a, draw) == 0, "4: state A and a draw refused");
	for (i = 0; i < 1000; i++) {
		before = ngpu;
		if (i % 10 == 9) {
			r = SUB2(&ch, state_c, draw);
			CHECK(r == 0 && ngpu > before, "4: good frame %u: %d", i, r);
		} else {
			r = SUB2(&ch, state_b_bad, draw);
			CHECK(r == -ECANCELED && ngpu == before, "4: bad frame %u: %d, %u words", i, r,
			      ngpu - before);
		}
	}
	CHECK(ch.cancelled == 900, "4: %u cancelled, expected 900", ch.cancelled);
	CHECK(gpu_whole() == 0, "4: the GPU's stream fails as one stream (%s)", last_warn);

	/* 5. a client that doesn't recover (no PS3_RECOVER, or refuse_rejected=1): refused */
	tchan_init(&ch, false);
	CHECK(SUB2(&ch, state_a, draw) == 0, "5: state A and a draw refused");
	before = ngpu;
	CHECK(SUB2(&ch, state_b_bad, draw) == -EINVAL, "5: the X16 submit not rejected");
	CHECK(SUB2(&ch, state_c, draw) == -EIO && SUB1(&ch, draw) == -EIO,
	      "5: the channel not refused after a rejection");
	CHECK(ngpu == before, "5: a refused channel sent %u words", ngpu - before);

	/* 6. mpv's first frame as captured (X16 dither texture) cancelled, then from Mesa 0052 */
	tchan_init(&ch, true);
	ngpu = 0;
	memset(&ch.vs, 0, sizeof(ch.vs));
	CHECK(SUB1(&ch, mpv_init) == 0, "6: mpv's binds refused (%s)", last_warn);
	before = ngpu;
	CHECK(SUB1(&ch, mpv_frame1) == -ECANCELED, "6: mpv's X16 frame not cancelled");
	CHECK(ngpu == before, "6: the cancelled frame sent %u words", ngpu - before);
	for (i = 0; i < 3; i++) {
		r = SUB1(&ch, mpv_frame1_0052);
		CHECK(r == 0, "6: mpv's frame from Mesa 0052 after the cancel: %d (%s)", r, last_warn);
		r = SUB1(&ch, mpv_frame1);
		CHECK(r == -ECANCELED, "6: mpv's X16 frame %u not cancelled", i);
	}
	CHECK(gpu_whole() == 0, "6: the GPU's stream fails as one stream (%s)", last_warn);
	sh = gpu_state();
	CHECK(reg3d(sh, 0x1a04) != X16, "6: the GPU holds the X16 format");
	ps3rsx_shadow_free(sh);
}

int main(int argc, char **argv)
{
	verbose = argc > 1;
	ps3rsx_validate_init();
	ps3rsx_shadow_init();

	for (u32 slot = 0; slot < 16; slot++) {
		const u32 m = 0x1740 + 4 * slot;

		for (u32 type = 0; type < 16; type++)
			for (u32 size = 0; size < 16; size++)
				for (u32 hi = 0; hi < 3; hi++) {
					const u32 d = type | size << 4 |
						      (hi == 0 ? 0 : hi == 1 ? 0x2000 : 0x12345600);
					const bool ok = expect_ok(d);
					u32 one[2] = { HDR(1, m), d };
					u32 ni[4] = { NONINC | HDR(3, m), 0x2022, d, 0x2022 };
					u32 inc[4] = { HDR(3, 0x1740), 0x2022, 0x2022, 0x2022 };
					int r;

					r = push(one, 2);
					CHECK((r == 0) == ok, "slot %u data %08x: %d, expected %s", slot, d, r,
					      ok ? "accept" : "refuse");
					r = push(ni, 4);
					CHECK((r == 0) == ok, "non-incrementing slot %u data %08x: %d", slot, d, r);
					if (slot < 14) {
						inc[0] = HDR(3, m);
						inc[2] = d;	/* the middle word: slot + 1 */
						r = push(inc, 4);
						CHECK((r == 0) == ok, "burst at slot %u data %08x: %d",
						      slot + 1, d, r);
					}
				}
	}

	/* what Mesa sends: Sauerbraten's world vertex (stride 32) with 0043 fixed, and as it was */
	const u32 mesa_ok[] = { 0x2032, 0x2022, 0x2025, 0x2047, 0x2047, 0x0002, 0x2044, 0x2034, 0x1047 };
	const u32 mesa_bad[] = { 0x2037, 0x2027, 0x2017, 0x2000 };
	u32 w[1 + 16];

	w[0] = HDR(9, 0x1740);
	memcpy(&w[1], mesa_ok, sizeof(mesa_ok));
	CHECK(push(w, 10) == 0, "Mesa's formats with 0043 fixed refused");
	for (unsigned i = 0; i < ARRAY_SIZE(mesa_bad); i++) {
		memcpy(&w[1], mesa_ok, sizeof(mesa_ok));
		w[1 + 3] = mesa_bad[i];
		CHECK(push(w, 10) != 0, "%08x in a burst accepted", mesa_bad[i]);
	}

	/* texture formats: every format code in every fragment unit's TEX_FORMAT */
	for (u32 unit = 0; unit < 16; unit++) {
		const u32 base = 0x1a00 + 0x20 * unit;

		for (u32 fmt = 0; fmt < 0x100; fmt++) {
			/* Mesa's word: DMA0, no border, 2D, the format byte, 1 level */
			const u32 d = 0x00010029 | fmt << 8;
			const bool ok = (fmt & 0x1f) != 0x14;
			u32 one[4] = { HDR(1, base + 4), d };
			u32 burst[10] = { HDR(8, base), 0x01943000, d, 0x00010101, 0x80000000, 0x0000a9aa,
					  0x01012000, 0x00400040, 0 };
			u32 ni[4] = { NONINC | HDR(3, base + 4), 0x0001a129, d, 0x0001a129 };
			int r;

			r = push(one, 2);
			CHECK((r == 0) == ok, "unit %u format %02x: %d, expected %s", unit, fmt, r,
			      ok ? "accept" : "refuse");
			r = push(burst, 9);
			CHECK((r == 0) == ok, "unit %u format %02x in a burst: %d", unit, fmt, r);
			r = push(ni, 4);
			CHECK((r == 0) == ok, "unit %u format %02x non-incrementing: %d", unit, fmt, r);
		}
	}

	/* mpv's first frame as captured: refused for its X16 dither texture, nothing else */
	{
		static u32 f[ARRAY_SIZE(mpv_frame1)];
		u32 nfmt = 0;
		int r;

		r = run_mpv(mpv_frame1, ARRAY_SIZE(mpv_frame1));
		CHECK(r != 0 && strstr(last_warn, "X16"), "mpv's first frame: %d (%s)", r, last_warn);
		/* the dither texture as L8 (Mesa 0052): every word passes */
		memcpy(f, mpv_frame1, sizeof(f));
		for (u32 i = 0; i < ARRAY_SIZE(f); i++)
			if (f[i] == 0x00019429) {
				f[i] = 0x00018129;
				nfmt++;
			}
		CHECK(nfmt == 1, "mpv's frame: %u X16 format words, expected 1", nfmt);
		r = run_mpv(f, ARRAY_SIZE(f));
		CHECK(r == 0, "mpv's frame with an L8 dither texture: %d (%s)", r, last_warn);
		/* and as Mesa 0052 sends it */
		r = run_mpv(mpv_frame1_0052, ARRAY_SIZE(mpv_frame1_0052));
		CHECK(r == 0, "mpv's frame from Mesa 0052: %d (%s)", r, last_warn);
	}

	/* WebKit's composited frame (GPU compositing, Badwolf on the GameDB home): every word passes */
	{
		struct ps3rsx_vstate vs;
		int r;

		memset(&vs, 0, sizeof(vs));
		r = ps3rsx_validate(&vs, webkit_init, ARRAY_SIZE(webkit_init));
		CHECK(r == 0, "WebKit's channel binds: %d (%s)", r, last_warn);
		r = ps3rsx_validate(&vs, webkit_frame, ARRAY_SIZE(webkit_frame));
		CHECK(r == 0, "WebKit's composited frame: %d (%s)", r, last_warn);
	}

	/* 0x105, Neverball: Mesa's blit sampling a 2x2 level at +0xaa0 (fragment unit 0); and the
	 * same offset in the other fragment units (the vertex units: vtxtex_cases()) */
	for (u32 unit = 0; unit < 16; unit++) {
		const u32 base = 0x1a00 + 0x20 * unit;
		u32 bad[10] = { HDR(8, base), 0x01943aa0, 0x00018b29, 0x00030303, 0x80000000,
				0x0000aae4, 0x01012000, 0x00020002, 0 };
		u32 good[10];

		memcpy(good, bad, sizeof(bad));
		good[1] = 0x01943a80;
		CHECK(push(bad, 9) != 0, "unit %u: texture offset +0xaa0 accepted", unit);
		CHECK(push(good, 9) == 0, "unit %u: texture offset +0xa80 refused", unit);
	}

	/* 0x106: a swizzled render target 1 pixel wide (log2 width 0); 2 wide and linear pass */
	{
		u32 rt1[2] = { HDR(1, 0x208), 0x06000245 };	/* swizzled, 1x64 */
		u32 rt2[2] = { HDR(1, 0x208), 0x00000145 };	/* linear, 1 wide */
		u32 rt3[2] = { HDR(1, 0x208), 0x06010245 };	/* swizzled, 2x64 */

		CHECK(push(rt1, 2) != 0, "swizzled render target 1 pixel wide accepted");
		CHECK(push(rt2, 2) == 0, "linear render target refused");
		CHECK(push(rt3, 2) == 0, "swizzled render target 2 pixels wide refused");
	}

	/* 0x109: the M2MF launch burst sent to the surface-2D object */
	{
		const u32 m2mf[] = { HDRS(1, 2, 0), 0x31337303, HDRS(8, 2, 0x30c), 0x2fc000, 0x1945000,
				     0x1f0, 0x1f0, 0x1f0, 1, 0x101, 0 };
		u32 sf2d[ARRAY_SIZE(m2mf)];

		memcpy(sf2d, m2mf, sizeof(m2mf));
		sf2d[1] = 0x313371c3;
		CHECK(run(m2mf, ARRAY_SIZE(m2mf)) == 0, "M2MF copy refused");
		CHECK(run(sf2d, ARRAY_SIZE(sf2d)) != 0, "M2MF methods on the surface-2D object accepted");
	}

	vtxtex_cases();
	recover_cases();

	printf("validate-test: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
