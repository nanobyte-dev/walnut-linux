/* SPU DXT1 encoder: van Waveren's real-time DXT1 with byte SIMD, giving exactly the bytes of
 * libspeq's scalar reference (lib/dxt-ref.c; its header comment has the algorithm).
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
 * again in 8-15) */
INLINE vu8 encode_block(vu8 r0, vu8 r1, vu8 r2, vu8 r3, const int punch)
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
	lo_e = (vu8)spu_add((vu16)mn, (vu16)inset);
	hi_e = (vu8)spu_sub((vu16)mx, (vu16)inset);

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
			    load_row(p + 2 * stride, pat), load_row(p + 3 * stride, pat), punch);
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

#endif /* SIMD */

void dxt1_encode_rows(const uint8_t *src, unsigned src_offset, unsigned width, unsigned block_rows,
		      uint8_t *dst, int alpha_mode)
{
	dxt1_encode_rows_stride(src, src_offset, width, 4 * width, block_rows, dst, alpha_mode);
}
