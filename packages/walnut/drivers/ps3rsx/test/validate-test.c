// SPDX-License-Identifier: GPL-2.0
/*
 * The validator (validate.c's fast path and check_method()) against the streams that raised
 * graph exceptions on the RSX (docs/RSX.md, "Errors and what wedges the GPU"):
 * - vertex formats: every type and size in every VTXFMT slot, as one write, an incrementing burst
 *   and a non-incrementing one, against the rule spelled out here independently: type 0 is
 *   refused (the RSX lacks B8G8R8A8_UNORM), type 7 (UB256) only with 4 components or disabled
 *   (size 0; 3 components raised 0x106 with Mesa 0043), everything else passes; plus the words
 *   Mesa sends: its float, short and byte formats, 0043's UB256 x4;
 * - texture formats: X16 (0x14, mpv's dither texture: 0x105) refused in every fragment and vertex
 *   unit, every other format passes; mpv's first frame as captured (fixtures/) refused, and
 *   passed with its dither texture as L8 and as Mesa 0052 sends it;
 * - texture offsets not 128-byte aligned (Neverball's +0xaa0 blit source: 0x105), swizzled render
 *   targets 1 pixel wide (0x106), M2MF methods on the surface-2D object (0x109).
 * Built in userspace with inc/ (kernel stand-ins) by the Makefile here.
 */
#include "shim.h"
#define PS3RSX_H	/* the driver header: not in userspace */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static int verbose;
static char last_warn[256];
#define pr_warn(...) do { snprintf(last_warn, sizeof(last_warn), __VA_ARGS__); \
			  if (verbose) printf("%s", last_warn); } while (0)

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

#include "fixtures/mpv-vo-gpu-0x105.h"

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

int main(int argc, char **argv)
{
	verbose = argc > 1;
	ps3rsx_validate_init();

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

	/* texture formats: every format code in every unit's TEX_FORMAT, fragment and vertex */
	for (u32 unit = 0; unit < 20; unit++) {
		const u32 base = unit < 16 ? 0x1a00 + 0x20 * unit : 0x900 + 0x20 * (unit - 16);

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

	/* 0x105, Neverball: Mesa's blit sampling a 2x2 level at +0xaa0 (fragment unit 0); and the
	 * same offset in the other units, vertex ones included */
	for (u32 unit = 0; unit < 20; unit++) {
		const u32 base = unit < 16 ? 0x1a00 + 0x20 * unit : 0x900 + 0x20 * (unit - 16);
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

	printf("validate-test: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
