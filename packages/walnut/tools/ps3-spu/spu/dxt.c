/* SPU DXT1 and DXT5 encoder: van Waveren's real-time DXT1 with byte SIMD, giving exactly the
 * bytes of libspeq's scalar reference (lib/dxt-ref.c; its header comment has the algorithms),
 * and DXT5 on top of it (below).
 *
 * One block is four quadwords, one per pixel row, each holding 4 RGBA pixels (a word each):
 * - bounding box: byte min/max over the four rows (cmpgt + sel), then across the words with
 *   the min and the complemented max in one vector; RGBA DXT1 first turns transparent pixels
 *   into 0xff (min) and 0 (max);
 * - inset (range >> 4) with halfword shifts and adds that can't carry between bytes;
 * - 565 and the expanded endpoints with word shifts and masks, colour 0 in word 0;
 * - palette entries 2 and 3 in words per channel: (2a + b) / 3 as (x * 683) >> 11 (exact for
 *   x <= 765), (a + b) / 2 in 3-colour mode, where entry 3 becomes a copy of entry 0 so it
 *   never wins the search (equal distance, higher index);
 * - distances: absd per byte, then sumb adds up a pixel's bytes; the palette's alpha is 0, so
 *   the pixel's alpha adds the same to all four distances and changes no comparison;
 *   d0/d2 and d1/d3 share a halfword pair, so one compare picks within the pairs (0,1) and
 *   (2,3) and a second between the pair winners, the lower index on every tie as the
 *   reference's scan does;
 * - indices: two rows at a time, a pixel per halfword: the pair compare, the 2-bit index
 *   shifted to 2 * pixel, ORed across the halfwords, and the 8 output bytes assembled with
 *   one shuffle (little-endian).
 * Source rows are fetched as two aligned quadwords and one shuffle (any byte offset). The
 * main loop encodes two blocks per iteration (independent work for the dual-issue pipes)
 * and stores them as one quadword.
 * DXT5 (dxt5_encode_rows_stride): the colour block is encode_block's opaque path plus the box's
 * diagonal (per-pixel channel differences from the box's centre as halfwords, red-green and
 * blue-green products summed with mpy/mpyhh, their signs swap red's and blue's ends) and a swap
 * that puts the larger 565 end first; the alpha block gathers the 16 alphas in one quadword,
 * takes min, max and the min and max of the values other than 0 and 255 with byte min/max in
 * halving steps (two reductions per vector), builds the four candidate pairs in words, each
 * candidate's 8-entry palette with halfword multiplies ((x * 9363) >> 16 is x / 7 for x <= 1785,
 * (x * 13108) >> 16 is x / 5 for x <= 1275), the nearest entry for all 16 pixels at once
 * (absd, 7 compare-selects, lower index on ties), the squared error with mpyu/mpyhhu and a
 * sum across words; the best candidate's indices are packed 3 bits each with byte shuffles
 * and word shifts. A block is one quadword store.
 * dxt_rgb_expand turns a 3-byte RGB source (Mesa's GL_RGB) into these RGBA rows in local store
 * first (dxt-op.c), with shuffles, about a cycle per pixel.
 * -DDXT_SCALAR builds a plain C version instead (debugging). On the host (no __SPU__) the
 * intrinsics come from dxt-emu.h, so the SIMD path can be tested against the reference there. */
#include "dxt.h"

/* dxt-emu.h is little-endian only: a big-endian host (the ppc64 package build's tests) gets the
 * plain C version */
#if !defined(__SPU__) && !defined(DXT_SCALAR) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define DXT_SCALAR
#endif

#ifdef DXT_SCALAR

static unsigned to565(const uint8_t c[3])
{
	return (c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3;
}

static void from565(uint8_t c[3], unsigned v)
{
	unsigned r = v >> 11, g = v >> 5 & 63, b = v & 31;

	c[0] = r << 3 | r >> 2;
	c[1] = g << 2 | g >> 4;
	c[2] = b << 3 | b >> 2;
}

/* one block: p is its top-left pixel, rows stride bytes apart */
static void block_scalar(uint8_t out[8], const uint8_t *p, unsigned stride, int punch)
{
	uint8_t lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 }, pal[4][3];
	unsigned c0, c1, bits = 0, transp = 0, i, k, j, n, d, best, bestd;

	for (i = 0; i < 16; i++) {
		const uint8_t *px = p + i / 4 * stride + i % 4 * 4;

		if (punch && px[3] <= 127) {
			transp |= 1u << i;
			continue;
		}
		for (k = 0; k < 3; k++) {
			if (px[k] < lo[k])
				lo[k] = px[k];
			if (px[k] > hi[k])
				hi[k] = px[k];
		}
	}
	if (transp == 0xffff) {
		c0 = c1 = 0;
		bits = 0xffffffff;
		goto emit;
	}
	for (k = 0; k < 3; k++) {
		unsigned inset = (hi[k] - lo[k]) >> 4;

		lo[k] += inset;
		hi[k] -= inset;
	}
	c0 = transp ? to565(lo) : to565(hi);
	c1 = transp ? to565(hi) : to565(lo);
	from565(pal[0], c0);
	from565(pal[1], c1);
	for (k = 0; k < 3; k++) {
		if (c0 > c1) {
			pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
			pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
		} else {
			pal[2][k] = (pal[0][k] + pal[1][k]) / 2;
			pal[3][k] = 0;
		}
	}
	n = c0 > c1 ? 4 : 3;
	for (i = 0; i < 16; i++) {
		const uint8_t *px = p + i / 4 * stride + i % 4 * 4;

		if (transp >> i & 1) {
			bits |= 3u << 2 * i;
			continue;
		}
		best = 0;
		bestd = ~0u;
		for (j = 0; j < n; j++) {
			d = 0;
			for (k = 0; k < 3; k++)
				d += px[k] > pal[j][k] ? px[k] - pal[j][k] : pal[j][k] - px[k];
			if (d < bestd) {
				bestd = d;
				best = j;
			}
		}
		bits |= best << 2 * i;
	}
emit:
	out[0] = c0 & 0xff;
	out[1] = c0 >> 8;
	out[2] = c1 & 0xff;
	out[3] = c1 >> 8;
	out[4] = bits & 0xff;
	out[5] = bits >> 8 & 0xff;
	out[6] = bits >> 16 & 0xff;
	out[7] = bits >> 24;
}

void dxt1_encode_rows_stride(const uint8_t *src, unsigned src_offset, unsigned width,
			     unsigned stride, unsigned block_rows, uint8_t *dst, int alpha_mode)
{
	const uint8_t *s = src + src_offset;
	unsigned bx, by;

	for (by = 0; by < block_rows; by++)
		for (bx = 0; bx < width / 4; bx++, dst += 8)
			block_scalar(dst, s + by * 4 * stride + bx * 16, stride, alpha_mode);
}

/* DXT5, as lib/dxt-ref.c's dxt5_ref_block */
static unsigned absdu(unsigned a, unsigned b)
{
	return a > b ? a - b : b - a;
}

static unsigned color5_try(uint8_t out[8], const uint8_t *p, unsigned stride, int inset_on)
{
	uint8_t lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 }, pal[4][3], t;
	unsigned c0, c1, bits = 0, i, k, j, d, best, bestd, sad = 0;
	int cen[3], crg = 0, cbg = 0;

	for (i = 0; i < 16; i++) {
		const uint8_t *px = p + i / 4 * stride + i % 4 * 4;

		for (k = 0; k < 3; k++) {
			if (px[k] < lo[k])
				lo[k] = px[k];
			if (px[k] > hi[k])
				hi[k] = px[k];
		}
	}
	for (k = 0; k < 3; k++) {
		unsigned inset = inset_on ? (hi[k] - lo[k]) >> 4 : 0;

		lo[k] += inset;
		hi[k] -= inset;
		cen[k] = (lo[k] + hi[k] + 1) >> 1;
	}
	for (i = 0; i < 16; i++) {
		const uint8_t *px = p + i / 4 * stride + i % 4 * 4;
		int dg = px[1] - cen[1];

		crg += (px[0] - cen[0]) * dg;
		cbg += (px[2] - cen[2]) * dg;
	}
	if (crg < 0)
		t = lo[0], lo[0] = hi[0], hi[0] = t;
	if (cbg < 0)
		t = lo[2], lo[2] = hi[2], hi[2] = t;
	c0 = to565(hi);
	c1 = to565(lo);
	if (c0 < c1)
		k = c0, c0 = c1, c1 = k;
	from565(pal[0], c0);
	from565(pal[1], c1);
	for (k = 0; k < 3; k++) {
		pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
		pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
	}
	for (i = 0; i < 16; i++) {
		const uint8_t *px = p + i / 4 * stride + i % 4 * 4;

		best = 0;
		bestd = ~0u;
		for (j = 0; j < (c0 > c1 ? 4u : 1u); j++) {	/* equal ends: index 0 */
			d = absdu(px[0], pal[j][0]) + absdu(px[1], pal[j][1]) + absdu(px[2], pal[j][2]);
			if (d < bestd) {
				bestd = d;
				best = j;
			}
		}
		bits |= best << 2 * i;
		sad += bestd;
	}
	out[0] = c0 & 0xff;
	out[1] = c0 >> 8;
	out[2] = c1 & 0xff;
	out[3] = c1 >> 8;
	out[4] = bits & 0xff;
	out[5] = bits >> 8 & 0xff;
	out[6] = bits >> 16 & 0xff;
	out[7] = bits >> 24;
	return sad;
}

static void color5_scalar(uint8_t out[8], const uint8_t *p, unsigned stride)
{
	uint8_t alt[8];
	unsigned i;

	if (color5_try(alt, p, stride, 0) < color5_try(out, p, stride, 1))
		for (i = 0; i < 8; i++)
			out[i] = alt[i];
}

static void alpha5_scalar(uint8_t out[8], const uint8_t *p, unsigned stride)
{
	unsigned mn = 255, mx = 0, lo = 255, hi = 0, i, k, c, ins, e[4][2], best = 0, d, bd;
	uint8_t a[16], pal[8], idx[16], bidx[16];
	uint32_t sse, bsse = 0;
	uint64_t bits = 0;

	for (i = 0; i < 16; i++) {
		a[i] = p[i / 4 * stride + i % 4 * 4 + 3];
		if (a[i] < mn)
			mn = a[i];
		if (a[i] > mx)
			mx = a[i];
		if (a[i] && a[i] != 255) {
			if (a[i] < lo)
				lo = a[i];
			if (a[i] > hi)
				hi = a[i];
		}
	}
	if (mn == mx) {
		out[0] = out[1] = mn;
		for (i = 2; i < 8; i++)
			out[i] = 0;
		return;
	}
	if (lo > hi)
		lo = hi = 0;
	ins = (mx - mn) >> 5;
	e[0][0] = mx, e[0][1] = mn;
	e[1][0] = lo, e[1][1] = hi;
	e[2][0] = mx - ins, e[2][1] = mn + ins;
	ins = (hi - lo) >> 4;
	e[3][0] = lo + ins, e[3][1] = hi - ins;
	for (k = 0; k < 4; k++) {
		pal[0] = e[k][0];
		pal[1] = e[k][1];
		for (c = 2; c < 8; c++)
			pal[c] = !(k & 1) ? (e[k][0] * (8 - c) + e[k][1] * (c - 1)) / 7 :
				 c < 6 ? (e[k][0] * (6 - c) + e[k][1] * (c - 1)) / 5 : c == 6 ? 0 : 255;
		for (sse = 0, i = 0; i < 16; i++) {
			idx[i] = 0;
			bd = absdu(a[i], pal[0]);
			for (c = 1; c < 8; c++)
				if ((d = absdu(a[i], pal[c])) < bd) {
					bd = d;
					idx[i] = c;
				}
			sse += bd * bd;
		}
		if (!k || sse < bsse) {
			bsse = sse;
			best = k;
			for (i = 0; i < 16; i++)
				bidx[i] = idx[i];
		}
	}
	for (i = 0; i < 16; i++)
		bits |= (uint64_t)bidx[i] << 3 * i;
	out[0] = e[best][0];
	out[1] = e[best][1];
	for (i = 0; i < 6; i++)
		out[2 + i] = bits >> 8 * i;
}

void dxt5_encode_rows_stride(const uint8_t *src, unsigned src_offset, unsigned width,
			     unsigned stride, unsigned block_rows, uint8_t *dst)
{
	const uint8_t *s = src + src_offset;
	unsigned bx, by;

	for (by = 0; by < block_rows; by++)
		for (bx = 0; bx < width / 4; bx++, dst += 16) {
			const uint8_t *p = s + by * 4 * stride + bx * 16;

			alpha5_scalar(dst, p, stride);
			color5_scalar(dst + 8, p, stride);
		}
}

void dxt_rgb_expand(uint8_t *dst, const uint8_t *src, unsigned src_offset, unsigned npix)
{
	const uint8_t *s = src + src_offset;
	unsigned i;

	for (i = 0; i < npix; i++, s += 3, dst += 4) {
		dst[0] = s[0];
		dst[1] = s[1];
		dst[2] = s[2];
		dst[3] = 255;
	}
}

#else /* SIMD */

#ifdef __SPU__
#include <spu_intrinsics.h>
#define VU8(...)	((vec_uchar16){ __VA_ARGS__ })
#define VU32(...)	((vec_uint4){ __VA_ARGS__ })
#define LDQ(p)		(*(const vec_uchar16 *)(p))
#define STQ(p, v)	(*(vec_uchar16 *)(p) = (v))
#else
#include "dxt-emu.h"
#define VU8(...)	emu_lit8((const uint8_t[16]){ __VA_ARGS__ })
#define VU32(...)	emu_lit32((const uint32_t[4]){ __VA_ARGS__ })
#define LDQ(p)		emu_load(p)
#define STQ(p, v)	emu_store(p, v)
#endif

typedef vec_uchar16 vu8;
typedef vec_ushort8 vu16;
typedef vec_uint4 vu32;

#define INLINE static inline __attribute__((always_inline))

#define SPLAT_W0	VU8(0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3)
#define SPLAT_W1	VU8(4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7)
#define SPLAT_ALPHA	VU8(3, 3, 3, 3, 7, 7, 7, 7, 11, 11, 11, 11, 15, 15, 15, 15)
/* the even and the odd halfwords of a, then of b */
#define EVENS		VU8(0, 1, 4, 5, 8, 9, 12, 13, 16, 17, 20, 21, 24, 25, 28, 29)
#define ODDS		VU8(2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27, 30, 31)

INLINE vu8 vmin(vu8 a, vu8 b)
{
	return spu_sel(a, b, spu_cmpgt(a, b));
}

INLINE vu8 vmax(vu8 a, vu8 b)
{
	return spu_sel(b, a, spu_cmpgt(a, b));
}

/* One block from its four pixel rows (RGBA); returns the 8 output bytes in bytes 0-7 (and
 * again in 8-15). diag (opaque only): DXT5's colour block, the box's diagonal and color0 first,
 * the box inset or not (noinset), and in every word of *sad the sum of the pixels' distances to
 * their entries (plus their alphas: the same for both of DXT5's candidates) */
INLINE vu8 encode_block(vu8 r0, vu8 r1, vu8 r2, vu8 r3, const int punch, const int diag,
			const int noinset, vu32 *sad)
{
	vu8 c[4] = { r0, r1, r2, r3 }, lo[4], hi[4];
	vu32 o[4], allt = VU32(0, 0, 0, 0), bits;
	vu16 g[4], M[4], half[2];
	vu8 mn, mx, d, inset, lo_e, hi_e, order, P0, P1, P2, P3;
	vu32 E, c565, X, four, W0, W1, S2, S3, q2, q3, pal2, pal3;
	const vu32 m683 = VU32(683, 683, 683, 683);
	int k;

	for (k = 0; k < 4; k++) {
		if (punch) {	/* o: opaque pixels (alpha > 127), the others out of the box */
			o[k] = (vu32)spu_shuffle(spu_cmpgt(c[k], 127), c[k], SPLAT_ALPHA);
			lo[k] = spu_orc(c[k], (vu8)o[k]);
			hi[k] = spu_and(c[k], (vu8)o[k]);
		} else
			lo[k] = hi[k] = c[k];
	}
	/* bounding box over the rows, then across the words: the min and the complemented max
	 * share one vector (words 0-1 and 2-3) for that, word 0 of mn/mx ends up with the box */
	mn = vmin(vmin(lo[0], lo[1]), vmin(lo[2], lo[3]));
	mx = vmax(vmax(hi[0], hi[1]), vmax(hi[2], hi[3]));
	mx = spu_nor(mx, mx);
	mn = vmin(spu_shuffle(mn, mx, VU8(0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23)),
		  spu_shuffle(mn, mx, VU8(8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 30, 31)));
	mn = vmin(mn, spu_rlqwbyte(mn, 4));	/* word 0: min, word 2: ~max */
	mx = spu_rlqwbyte(mn, 8);
	mx = spu_nor(mx, mx);

	/* inset: mn + inset <= mx and mx - inset >= mn in every byte (alpha too), so halfword
	 * arithmetic doesn't carry or borrow across bytes (except in the all-transparent block,
	 * whose endpoints are masked below) */
	d = spu_absd(mx, mn);
	inset = (vu8)spu_and(spu_rlmask((vu16)d, -4), (unsigned short)0x0f0f);
	if (noinset)
		inset = spu_and(inset, 0);
	lo_e = (vu8)spu_add((vu16)mn, (vu16)inset);
	hi_e = (vu8)spu_sub((vu16)mx, (vu16)inset);

	if (diag) {
		/* the signs of sum((r - cen) (g - cen)) and sum((b - cen) (g - cen)): pixels'
		 * channels as halfwords (8 a vector), the centre splatted, mpy/mpyhh into words */
		vu8 cen = spu_avg(lo_e, hi_e), m;
		vec_short8 cr = (vec_short8)spu_shuffle(cen, cen, VU8(0x80, 0, 0x80, 0, 0x80, 0, 0x80, 0,
								  0x80, 0, 0x80, 0, 0x80, 0, 0x80, 0));
		vec_short8 cg = (vec_short8)spu_shuffle(cen, cen, VU8(0x80, 1, 0x80, 1, 0x80, 1, 0x80, 1,
								  0x80, 1, 0x80, 1, 0x80, 1, 0x80, 1));
		vec_short8 cb = (vec_short8)spu_shuffle(cen, cen, VU8(0x80, 2, 0x80, 2, 0x80, 2, 0x80, 2,
								  0x80, 2, 0x80, 2, 0x80, 2, 0x80, 2));
		const vu8 CH = VU8(0x80, 0, 0x80, 4, 0x80, 8, 0x80, 12, 0x80, 16, 0x80, 20, 0x80, 24,
				   0x80, 28);
		const vu8 CHG = VU8(0x80, 1, 0x80, 5, 0x80, 9, 0x80, 13, 0x80, 17, 0x80, 21, 0x80, 25,
				    0x80, 29);
		const vu8 CHB = VU8(0x80, 2, 0x80, 6, 0x80, 10, 0x80, 14, 0x80, 18, 0x80, 22, 0x80, 26,
				    0x80, 30);
		vec_int4 srg = (vec_int4)VU32(0, 0, 0, 0), sbg = srg, zero = srg;
		vu32 neg_rg, neg_bg;

		for (k = 0; k < 2; k++) {
			vec_short8 dr = (vec_short8)spu_shuffle(c[2 * k], c[2 * k + 1], CH);
			vec_short8 dg = (vec_short8)spu_shuffle(c[2 * k], c[2 * k + 1], CHG);
			vec_short8 db = (vec_short8)spu_shuffle(c[2 * k], c[2 * k + 1], CHB);

			dr = spu_sub(dr, cr);
			dg = spu_sub(dg, cg);
			db = spu_sub(db, cb);
			srg = spu_add(srg, spu_add(spu_mule(dr, dg), spu_mulo(dr, dg)));
			sbg = spu_add(sbg, spu_add(spu_mule(db, dg), spu_mulo(db, dg)));
		}
		srg = spu_add(srg, spu_rlqwbyte(srg, 8));
		srg = spu_add(srg, spu_rlqwbyte(srg, 4));
		sbg = spu_add(sbg, spu_rlqwbyte(sbg, 8));
		sbg = spu_add(sbg, spu_rlqwbyte(sbg, 4));
		neg_rg = (vu32)spu_cmpgt(zero, srg);
		neg_bg = (vu32)spu_cmpgt(zero, sbg);
		/* swap the ends' red (byte 0 of a word) and blue (byte 2) where negative */
		m = (vu8)spu_or(spu_and(neg_rg, 0xff000000u), spu_and(neg_bg, 0x0000ff00u));
		d = spu_sel(hi_e, lo_e, m);
		lo_e = spu_sel(lo_e, hi_e, m);
		hi_e = d;
	}

	/* E: colour 0 in word 0, colour 1 in word 1; opaque blocks put the max first (4-colour
	 * mode unless equal), blocks with transparency the min (3-colour mode) */
	order = VU8(0, 1, 2, 3, 16, 17, 18, 19, 0, 1, 2, 3, 16, 17, 18, 19);
	if (punch) {
		vu32 oand = spu_and(spu_and(o[0], o[1]), spu_and(o[2], o[3]));
		vu32 oor = spu_or(spu_or(o[0], o[1]), spu_or(o[2], o[3]));
		vu32 anyt = spu_orx(spu_nor(oand, oand));

		anyt = spu_shuffle(anyt, anyt, SPLAT_W0);
		allt = spu_cmpeq(spu_orx(oor), 0u);
		allt = spu_shuffle(allt, allt, SPLAT_W0);
		order = spu_sel(order, VU8(16, 17, 18, 19, 0, 1, 2, 3, 16, 17, 18, 19, 0, 1, 2, 3),
				(vu8)anyt);
	}
	E = (vu32)spu_shuffle(hi_e, lo_e, order);
	c565 = spu_or(spu_or(spu_and(spu_rlmask(E, -16), 0xf800u), spu_and(spu_rlmask(E, -13), 0x07e0u)),
		      spu_and(spu_rlmask(E, -11), 0x001fu));
	if (diag) {	/* color0 the larger: E and c565 hold (c0, c1, c0, c1); rotated, (c1, c0, ...) */
		vu32 sw = spu_cmpgt(spu_rlqwbyte(c565, 4), c565);

		sw = spu_shuffle(sw, sw, SPLAT_W0);
		E = spu_sel(E, spu_rlqwbyte(E, 4), sw);
		c565 = spu_sel(c565, spu_rlqwbyte(c565, 4), sw);
	}
	/* the endpoints back to 8 bits as the decoder expands them (bit replication), alpha 0 */
	X = spu_or(spu_or(spu_and(E, 0xf8fcf800u), spu_and(spu_rlmask(E, -5), 0x07000700u)),
		   spu_and(spu_rlmask(E, -6), 0x00030000u));
	four = spu_cmpgt(c565, spu_rlqwbyte(c565, 4));	/* word 0: color0 > color1 */
	four = spu_shuffle(four, four, SPLAT_W0);

	/* entries 2 and 3, one channel per word */
	W0 = (vu32)spu_shuffle((vu8)X, (vu8)X, VU8(0x80, 0x80, 0x80, 0, 0x80, 0x80, 0x80, 1,
						    0x80, 0x80, 0x80, 2, 0x80, 0x80, 0x80, 3));
	W1 = (vu32)spu_shuffle((vu8)X, (vu8)X, VU8(0x80, 0x80, 0x80, 4, 0x80, 0x80, 0x80, 5,
						    0x80, 0x80, 0x80, 6, 0x80, 0x80, 0x80, 7));
	S2 = spu_add(spu_add(W0, W0), W1);
	S3 = spu_add(spu_add(W1, W1), W0);
	q2 = spu_rlmask(spu_mulo((vu16)S2, (vu16)m683), -11);
	q3 = spu_rlmask(spu_mulo((vu16)S3, (vu16)m683), -11);
	pal2 = spu_sel(spu_rlmask(spu_add(W0, W1), -1), q2, four);
	pal3 = spu_sel(W0, q3, four);

	/* the four entries, splatted to RGB0 in every word */
	P0 = spu_shuffle((vu8)X, (vu8)X, SPLAT_W0);
	P1 = spu_shuffle((vu8)X, (vu8)X, SPLAT_W1);
	P2 = spu_shuffle((vu8)pal2, (vu8)pal2, VU8(3, 7, 11, 0x80, 3, 7, 11, 0x80,
						    3, 7, 11, 0x80, 3, 7, 11, 0x80));
	P3 = spu_shuffle((vu8)pal3, (vu8)pal3, VU8(3, 7, 11, 0x80, 3, 7, 11, 0x80,
						    3, 7, 11, 0x80, 3, 7, 11, 0x80));

	/* per pixel (word) of each row: A = d0 | d2, B = d1 | d3 (even | odd halfword); g picks
	 * within the pairs (0,1) and (2,3), M is the pair minimum */
	for (k = 0; k < 4; k++) {
		vu16 A = spu_sumb(spu_absd(c[k], P2), spu_absd(c[k], P0));
		vu16 B = spu_sumb(spu_absd(c[k], P3), spu_absd(c[k], P1));

		g[k] = spu_cmpgt(A, B);		/* even: 1 beats 0, odd: 3 beats 2 */
		M[k] = spu_sel(A, B, g[k]);	/* even: min(d0, d1), odd: min(d2, d3) */
	}
	if (diag) {	/* the pixels' smallest distances, summed */
		vu32 t = VU32(0, 0, 0, 0);

		for (k = 0; k < 4; k++) {
			vu16 o = (vu16)spu_rlqwbyte((vu8)M[k], 2);	/* odd onto even */

			t = spu_add(t, spu_and((vu32)spu_sel(M[k], o, spu_cmpgt(M[k], o)), 0xffff0000u));
		}
		t = spu_rlmask(t, -16);
		t = spu_add(t, spu_rlqwbyte(t, 8));
		*sad = spu_add(t, spu_rlqwbyte(t, 4));
	}
	/* two rows at a time, a pixel per halfword: pair (2,3) wins only when strictly closer,
	 * then the index's 2 bits shifted to 2 * pixel (16 bits for the 8 pixels) */
	for (k = 0; k < 2; k++) {
		vu16 h = spu_cmpgt(spu_shuffle(M[2 * k], M[2 * k + 1], EVENS),
				   spu_shuffle(M[2 * k], M[2 * k + 1], ODDS));
		vu16 l = spu_sel(spu_shuffle(g[2 * k], g[2 * k + 1], EVENS),
				 spu_shuffle(g[2 * k], g[2 * k + 1], ODDS), h);
		vu16 idx = spu_sub(spu_and(h, (unsigned short)2), l);	/* l is 0 or -1 */

		if (punch)	/* transparent: index 3 */
			idx = spu_sel((vu16)VU32(0x30003, 0x30003, 0x30003, 0x30003), idx,
				      (vu16)spu_shuffle(o[2 * k], o[2 * k + 1], EVENS));
		half[k] = spu_sl(idx, (vu16)VU32(0x00000002, 0x00040006, 0x0008000a, 0x000c000e));
	}
	/* OR the halfwords: rows 2-3 in the high halfword of each word, rows 0-1 in the low */
	bits = (vu32)spu_or(spu_shuffle(half[0], half[1], VU8(16, 17, 0, 1, 18, 19, 2, 3,
							      20, 21, 4, 5, 22, 23, 6, 7)),
			    spu_shuffle(half[0], half[1], VU8(24, 25, 8, 9, 26, 27, 10, 11,
							      28, 29, 12, 13, 30, 31, 14, 15)));
	bits = spu_or(bits, spu_rlqwbyte(bits, 8));
	bits = spu_or(bits, spu_rlqwbyte(bits, 4));
	if (punch)	/* nothing to colour: black endpoints, every index 3 (bits already) */
		c565 = spu_andc(c565, allt);
	return spu_shuffle((vu8)c565, (vu8)bits, VU8(3, 2, 7, 6, 19, 18, 17, 16,
						     3, 2, 7, 6, 19, 18, 17, 16));
}

/* One pixel row of a block: the 16 bytes at p + the shift in pat */
INLINE vu8 load_row(const uint8_t *p, vu8 pat)
{
	return spu_shuffle(LDQ(p), LDQ(p + 16), pat);
}

INLINE vu8 block_at(const uint8_t *p, unsigned stride, vu8 pat, const int punch)
{
	return encode_block(load_row(p, pat), load_row(p + stride, pat),
			    load_row(p + 2 * stride, pat), load_row(p + 3 * stride, pat), punch, 0, 0,
			    0);
}

/* min and max of each 8-byte half's bytes, in every byte of the half */
INLINE vu8 hmin8(vu8 v)
{
	v = vmin(v, spu_shuffle(v, v, VU8(4, 5, 6, 7, 0, 1, 2, 3, 12, 13, 14, 15, 8, 9, 10, 11)));
	v = vmin(v, spu_shuffle(v, v, VU8(2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13)));
	return vmin(v, spu_shuffle(v, v, VU8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14)));
}

INLINE vu8 hmax8(vu8 v)
{
	v = vmax(v, spu_shuffle(v, v, VU8(4, 5, 6, 7, 0, 1, 2, 3, 12, 13, 14, 15, 8, 9, 10, 11)));
	v = vmax(v, spu_shuffle(v, v, VU8(2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13)));
	return vmax(v, spu_shuffle(v, v, VU8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14)));
}

/* DXT5's alpha block of the four pixel rows: the 8 bytes in bytes 0-7 */
INLINE vu8 alpha_block(vu8 r0, vu8 r1, vu8 r2, vu8 r3)
{
	const vu8 AP = VU8(3, 7, 11, 15, 19, 23, 27, 31, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80);
	const vu32 W8a0 = VU32(7, 0, 6, 5), W8a1 = VU32(4, 3, 2, 1), W8b0 = VU32(0, 7, 1, 2),
		   W8b1 = VU32(3, 4, 5, 6), W6a0 = VU32(5, 0, 4, 3), W6a1 = VU32(2, 1, 0, 0),
		   W6b0 = VU32(0, 5, 1, 2), W6b1 = VU32(3, 4, 0, 0);
	const vu32 m7 = VU32(9363, 9363, 9363, 9363), m5 = VU32(13108, 13108, 13108, 13108);
	const vu8 PACK = VU8(3, 7, 11, 15, 19, 23, 27, 31, 3, 7, 11, 15, 19, 23, 27, 31);
	vu8 A = spu_shuffle(spu_shuffle(r0, r1, AP), spu_shuffle(r2, r3, AP),
			    VU8(0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23));
	vu8 z = spu_or(spu_cmpeq(A, 0), spu_cmpeq(A, 255)), lo, hi, mins, maxs, bidx = A;
	vu32 S0, S1, E0, E1, I1, I2, bsse = VU32(0, 0, 0, 0), ba0 = bsse, ba1 = bsse, T, W;
	int k, e;

	/* mins: min of all (bytes 0-7) and of the values but 0 and 255 (bytes 8-15); maxs too */
	lo = spu_or(A, z);
	hi = spu_andc(A, z);
	mins = vmin(spu_shuffle(A, lo, VU8(0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23)),
		    spu_shuffle(A, lo, VU8(8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 30, 31)));
	maxs = vmax(spu_shuffle(A, hi, VU8(0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23)),
		    spu_shuffle(A, hi, VU8(8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 30, 31)));
	mins = hmin8(mins);
	maxs = hmax8(maxs);
	/* flat (opaque or clear, most blocks of most textures): (min, min), every index 0, what the
	 * search below would give */
	if (__builtin_expect(spu_extract((vu32)spu_cmpeq(mins, maxs), 0) >> 24, 0))
		return spu_shuffle(mins, mins, VU8(0, 0, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
						   0, 0, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80));
	/* words: S0 = (max, lo, max, lo), S1 = (min, hi, min, hi); no value but 0 and 255: lo, hi 0 */
	S0 = (vu32)spu_shuffle(maxs, mins, VU8(0x80, 0x80, 0x80, 0, 0x80, 0x80, 0x80, 24,
					       0x80, 0x80, 0x80, 0, 0x80, 0x80, 0x80, 24));
	S1 = (vu32)spu_shuffle(mins, maxs, VU8(0x80, 0x80, 0x80, 0, 0x80, 0x80, 0x80, 24,
					       0x80, 0x80, 0x80, 0, 0x80, 0x80, 0x80, 24));
	{
		vu32 none = spu_cmpgt(spu_shuffle(S0, S0, SPLAT_W1), spu_shuffle(S1, S1, SPLAT_W1));

		none = spu_and(none, VU32(0, ~0u, 0, ~0u));
		S0 = spu_andc(S0, none);
		S1 = spu_andc(S1, none);
	}
	/* inset: C = A's pair moved in by (max - min) >> 5, D = B's by (hi - lo) >> 4 */
	I1 = spu_rlmask(spu_sub(spu_shuffle(S0, S0, SPLAT_W0), spu_shuffle(S1, S1, SPLAT_W0)), -5);
	I2 = spu_rlmask(spu_sub(spu_shuffle(S1, S1, SPLAT_W1), spu_shuffle(S0, S0, SPLAT_W1)), -4);
	I1 = spu_and(I1, VU32(0, 0, ~0u, 0));
	I2 = spu_and(I2, VU32(0, 0, 0, ~0u));
	E0 = spu_sub(spu_add(S0, I2), I1);	/* alpha0 of A, B, C, D */
	E1 = spu_sub(spu_add(S1, I1), I2);	/* alpha1 */

#pragma GCC unroll 4
	for (k = 0; k < 4; k++) {
		const vu8 sw = k == 0 ? SPLAT_W0 : k == 1 ? SPLAT_W1 :
			       k == 2 ? VU8(8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11) :
					VU8(12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15);
		vu32 a0 = spu_shuffle(E0, E0, sw), a1 = spu_shuffle(E1, E1, sw), q0, q1, sq, gt;
		vu8 pal, d, idx = spu_and(A, 0), bd;
		vu16 h0, h1;

		/* the decoder's palette: 8 values for A and C, 6 and 0, 255 for B and D */
		if (!(k & 1)) {
			q0 = spu_add(spu_mulo((vu16)a0, (vu16)W8a0), spu_mulo((vu16)a1, (vu16)W8b0));
			q1 = spu_add(spu_mulo((vu16)a0, (vu16)W8a1), spu_mulo((vu16)a1, (vu16)W8b1));
			q0 = spu_rlmask(spu_mulo((vu16)q0, (vu16)m7), -16);
			q1 = spu_rlmask(spu_mulo((vu16)q1, (vu16)m7), -16);
		} else {
			q0 = spu_add(spu_mulo((vu16)a0, (vu16)W6a0), spu_mulo((vu16)a1, (vu16)W6b0));
			q1 = spu_add(spu_mulo((vu16)a0, (vu16)W6a1), spu_mulo((vu16)a1, (vu16)W6b1));
			q0 = spu_rlmask(spu_mulo((vu16)q0, (vu16)m5), -16);
			q1 = spu_rlmask(spu_mulo((vu16)q1, (vu16)m5), -16);
			q1 = spu_sel(q1, VU32(0, 0, 0, 255), VU32(0, 0, ~0u, ~0u));
		}
		pal = spu_shuffle((vu8)q0, (vu8)q1, PACK);
		/* every pixel's nearest entry, the lower index on a tie */
		bd = spu_absd(A, spu_shuffle(pal, pal, (vu8)VU32(0, 0, 0, 0)));
#pragma GCC unroll 8
		for (e = 1; e < 8; e++) {
			vu8 m;

			d = spu_absd(A, spu_shuffle(pal, pal, (vu8)VU32(e * 0x01010101u, e * 0x01010101u,
								      e * 0x01010101u, e * 0x01010101u)));
			m = spu_cmpgt(bd, d);
			bd = spu_sel(bd, d, m);
			idx = spu_sel(idx, (vu8)VU32(e * 0x01010101u, e * 0x01010101u, e * 0x01010101u,
						     e * 0x01010101u), m);
		}
		/* the squared error: the distances as halfwords, mpyu/mpyhhu, summed */
		h0 = (vu16)spu_shuffle(bd, bd, VU8(0x80, 0, 0x80, 1, 0x80, 2, 0x80, 3, 0x80, 4, 0x80, 5,
						   0x80, 6, 0x80, 7));
		h1 = (vu16)spu_shuffle(bd, bd, VU8(0x80, 8, 0x80, 9, 0x80, 10, 0x80, 11, 0x80, 12,
						   0x80, 13, 0x80, 14, 0x80, 15));
		sq = spu_add(spu_add(spu_mule(h0, h0), spu_mulo(h0, h0)),
			     spu_add(spu_mule(h1, h1), spu_mulo(h1, h1)));
		sq = spu_add(sq, spu_rlqwbyte(sq, 8));
		sq = spu_add(sq, spu_rlqwbyte(sq, 4));
		if (!k) {
			bsse = sq;
			bidx = idx;
			ba0 = a0;
			ba1 = a1;
			continue;
		}
		gt = spu_cmpgt(bsse, sq);	/* strictly better: the earlier on a tie */
		bsse = spu_sel(bsse, sq, gt);
		bidx = spu_sel(bidx, idx, (vu8)gt);
		ba0 = spu_sel(ba0, a0, gt);
		ba1 = spu_sel(ba1, a1, gt);
	}
	/* 3 bits a pixel, little-endian: word m gets pixels 4m..4m+3 (12 bits), then pairs of words
	 * make 24 bits (pixels 0-7 in word 0, 8-15 in word 2), out low byte first */
	W = (vu32)spu_shuffle(bidx, bidx, VU8(0x80, 0x80, 0x80, 0, 0x80, 0x80, 0x80, 4,
					      0x80, 0x80, 0x80, 8, 0x80, 0x80, 0x80, 12));
	W = spu_or(W, spu_sl((vu32)spu_shuffle(bidx, bidx, VU8(0x80, 0x80, 0x80, 1, 0x80, 0x80, 0x80, 5,
								0x80, 0x80, 0x80, 9, 0x80, 0x80, 0x80, 13)), 3));
	W = spu_or(W, spu_sl((vu32)spu_shuffle(bidx, bidx, VU8(0x80, 0x80, 0x80, 2, 0x80, 0x80, 0x80, 6,
								0x80, 0x80, 0x80, 10, 0x80, 0x80, 0x80, 14)), 6));
	W = spu_or(W, spu_sl((vu32)spu_shuffle(bidx, bidx, VU8(0x80, 0x80, 0x80, 3, 0x80, 0x80, 0x80, 7,
								0x80, 0x80, 0x80, 11, 0x80, 0x80, 0x80, 15)), 9));
	W = spu_sl(W, VU32(0, 12, 0, 12));
	T = spu_or(W, spu_rlqwbyte(W, 4));
	return spu_shuffle(spu_shuffle((vu8)ba0, (vu8)ba1, VU8(3, 19, 3, 19, 3, 19, 3, 19, 3, 19, 3, 19,
								3, 19, 3, 19)),
			   (vu8)T, VU8(0, 1, 19, 18, 17, 27, 26, 25, 0, 1, 19, 18, 17, 27, 26, 25));
}

/* One DXT5 block: alpha, then colour, as one quadword */
INLINE vu8 block5_at(const uint8_t *p, unsigned stride, vu8 pat)
{
	vu8 r0 = load_row(p, pat), r1 = load_row(p + stride, pat), r2 = load_row(p + 2 * stride, pat),
	    r3 = load_row(p + 3 * stride, pat), c, alt;
	vu32 s0, s1;

	/* colour: inset, or not when that is closer (strictly) */
	c = encode_block(r0, r1, r2, r3, 0, 1, 0, &s0);
	alt = encode_block(r0, r1, r2, r3, 0, 1, 1, &s1);
	c = spu_sel(c, alt, (vu8)spu_cmpgt(s0, s1));
	return spu_shuffle(alpha_block(r0, r1, r2, r3), c,
			   VU8(0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23));
}

/* 8 bytes into the half of d's quadword d points at, keeping the other half */
INLINE void store8(uint8_t *d, vu8 b)
{
	uint8_t *q = (uint8_t *)((uintptr_t)d & ~(uintptr_t)15);
	vu8 old = LDQ(q);

	if ((uintptr_t)d & 8)
		STQ(q, spu_shuffle(old, b, VU8(0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23)));
	else
		STQ(q, spu_shuffle(b, old, VU8(0, 1, 2, 3, 4, 5, 6, 7, 24, 25, 26, 27, 28, 29, 30, 31)));
}

/* n blocks from p (aligned; pat shifts), bw to a block row, pixel rows stride bytes apart,
 * the output in one run from d */
INLINE void encode_all(const uint8_t *p, vu8 pat, unsigned bw, unsigned stride, unsigned n,
		       uint8_t *d, const int punch)
{
	unsigned skip = 4 * stride - 16 * bw, x = 0;

#define NEXT() do {					\
		unsigned wrap_ = ++x == bw;		\
		p += 16 + (wrap_ ? skip : 0);		\
		x = wrap_ ? 0 : x;			\
	} while (0)

	if (n && ((uintptr_t)d & 8)) {
		store8(d, block_at(p, stride, pat, punch));
		NEXT();
		d += 8;
		n--;
	}
	for (; n >= 2; n -= 2, d += 16) {
		const uint8_t *p0 = p;
		vu8 b0, b1;

		NEXT();
		b0 = block_at(p0, stride, pat, punch);
		b1 = block_at(p, stride, pat, punch);
		NEXT();
		STQ(d, spu_shuffle(b0, b1, VU8(0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23)));
	}
	if (n)
		store8(d, block_at(p, stride, pat, punch));
#undef NEXT
}

void dxt1_encode_rows_stride(const uint8_t *src, unsigned src_offset, unsigned width,
			     unsigned stride, unsigned block_rows, uint8_t *dst, int alpha_mode)
{
	uintptr_t a = (uintptr_t)src + src_offset;
	const uint8_t *base = (const uint8_t *)(a & ~(uintptr_t)15);
	unsigned off = a & 15, bw = width / 4;
	/* shuffle pattern: bytes off..off+15 of two consecutive quadwords (no byte carries) */
	vu8 pat = (vu8)spu_add((vu16)VU8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
			       (vu16)VU32(off * 0x01010101u, off * 0x01010101u, off * 0x01010101u,
					  off * 0x01010101u));

	if (alpha_mode == DXT1_MODE_RGBA)
		encode_all(base, pat, bw, stride, bw * block_rows, dst, 1);
	else
		encode_all(base, pat, bw, stride, bw * block_rows, dst, 0);
}

void dxt5_encode_rows_stride(const uint8_t *src, unsigned src_offset, unsigned width,
			     unsigned stride, unsigned block_rows, uint8_t *dst)
{
	uintptr_t a = (uintptr_t)src + src_offset;
	const uint8_t *p = (const uint8_t *)(a & ~(uintptr_t)15);
	unsigned off = a & 15, bw = width / 4, skip = 4 * stride - 16 * bw, x, y;
	vu8 pat = (vu8)spu_add((vu16)VU8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
			       (vu16)VU32(off * 0x01010101u, off * 0x01010101u, off * 0x01010101u,
					  off * 0x01010101u));

	for (y = 0; y < block_rows; y++, p += skip)
#pragma GCC unroll 2
		for (x = 0; x < bw; x++, p += 16, dst += 16)
			STQ(dst, block5_at(p, stride, pat));
}

/* RGB to RGBA: 16 pixels (48 source bytes, three quadwords) a step, four output quadwords.
 * Output quadword k takes the 12 bytes at off + 12k from the aligned base: they lie in the
 * quadword pair (Q[k - 1], Q[k]) or (Q[k], Q[k + 1]) (k >= 1; k = 0: Q0, Q1), the later pair when
 * off >= 4k, one select per operand; the shuffle takes R, G, B and makes alpha 0xff (pattern
 * bytes 0xc0 + n). The pairs and patterns are fixed for the call, and Q3, Q4 of a step are the
 * next step's Q0, Q1: 3 loads, 4 shuffles, 4 stores (odd pipe) and 6 selects (even) a step. */
void dxt_rgb_expand(uint8_t *dst, const uint8_t *src, unsigned src_offset, unsigned npix)
{
	uintptr_t a = (uintptr_t)src + src_offset;
	const uint8_t *p = (const uint8_t *)(a & ~(uintptr_t)15);
	unsigned off = a & 15, r1 = (off + 12) & 15, r2 = (off + 8) & 15, r3 = (off + 4) & 15, n;
	unsigned m1 = off >= 4 ? ~0u : 0, m2 = off >= 8 ? ~0u : 0, m3 = off >= 12 ? ~0u : 0;
	/* pixel j's R, G, B from bytes 3j..3j+2 of the window, alpha 0xc0 (0xff); + the window's
	 * start: no byte carries (11 + 15, 0xc0 + 15) */
	const vu8 base = VU8(0, 1, 2, 0xc0, 3, 4, 5, 0xc0, 6, 7, 8, 0xc0, 9, 10, 11, 0xc0);
	vu8 pat0 = (vu8)spu_add((vu16)base, (vu16)VU32(off * 0x01010101u, off * 0x01010101u,
						       off * 0x01010101u, off * 0x01010101u));
	vu8 pat1 = (vu8)spu_add((vu16)base, (vu16)VU32(r1 * 0x01010101u, r1 * 0x01010101u,
						       r1 * 0x01010101u, r1 * 0x01010101u));
	vu8 pat2 = (vu8)spu_add((vu16)base, (vu16)VU32(r2 * 0x01010101u, r2 * 0x01010101u,
						       r2 * 0x01010101u, r2 * 0x01010101u));
	vu8 pat3 = (vu8)spu_add((vu16)base, (vu16)VU32(r3 * 0x01010101u, r3 * 0x01010101u,
						       r3 * 0x01010101u, r3 * 0x01010101u));
	vu8 s1 = (vu8)VU32(m1, m1, m1, m1), s2 = (vu8)VU32(m2, m2, m2, m2),
	    s3 = (vu8)VU32(m3, m3, m3, m3);
	vu8 q0 = LDQ(p), q1 = LDQ(p + 16), q2, q3, q4;

	for (n = 0; n < npix; n += 16, p += 48, dst += 64) {
		q2 = LDQ(p + 32);
		q3 = LDQ(p + 48);
		q4 = LDQ(p + 64);
		STQ(dst, spu_shuffle(q0, q1, pat0));
		STQ(dst + 16, spu_shuffle(spu_sel(q0, q1, s1), spu_sel(q1, q2, s1), pat1));
		STQ(dst + 32, spu_shuffle(spu_sel(q1, q2, s2), spu_sel(q2, q3, s2), pat2));
		STQ(dst + 48, spu_shuffle(spu_sel(q2, q3, s3), spu_sel(q3, q4, s3), pat3));
		q0 = q3;
		q1 = q4;
	}
}

#endif /* SIMD */

void dxt1_encode_rows(const uint8_t *src, unsigned src_offset, unsigned width, unsigned block_rows,
		      uint8_t *dst, int alpha_mode)
{
	dxt1_encode_rows_stride(src, src_offset, width, 4 * width, block_rows, dst, alpha_mode);
}
