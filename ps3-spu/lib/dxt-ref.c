/* Reference DXT1 encoder: J.M.P. van Waveren's real-time DXT1 (Real-Time DXT Compression,
 * id Software, 2006), scalar and portable (host tests and the PPE). The SPU encoder must give
 * the same bytes, so every step is one it can do with byte SIMD:
 * - endpoints: the per-channel bounding box of the block, inset by range >> 4 at both ends,
 *   then truncated to 565;
 * - palette: the endpoints expanded back to 8 bits and mixed exactly as the decoder does;
 * - indices: the nearest palette entry by Manhattan distance over R, G, B (spu_absd/spu_sumb),
 *   the lowest index on a tie;
 * - output byte by byte, little-endian, as Mesa writes it.
 * RGBA DXT1: pixels with alpha <= 127 are transparent and left out of the box; a block with any
 * of them uses the 3-colour mode (color0 <= color1, index 3 transparent). An opaque block whose
 * two endpoints come out equal is also in 3-colour mode for the decoder: it never gets index 3
 * (all its palette entries are the same colour, so every pixel takes index 0).
 *
 * DXT5 (RGBA_DXT5, what Mesa makes of generic compressed RGBA): the alpha block, then a colour
 * block that decoders always read in 4-colour mode (Mesa, and NVIDIA hardware), so it must never
 * rely on 3-colour mode: color0 >= color1, and when they are equal every index is 0 (both modes
 * then give the same colour). Measured on alpha textures from OpenArena, SuperTuxKart, Neverball
 * and Extreme Tux Racer against Mesa's encoder (tests/mesa-compare.sh):
 * - colour: van Waveren's box and inset over all 16 pixels (transparent ones too: bilinear
 *   filtering blends their colour into the edges), then the box's diagonal: red and blue swap
 *   their ends when they fall against green (the sign of sum((c - centre) * (g - centre)),
 *   centre (lo + hi + 1) >> 1 per channel, after the inset), as icons and foliage often have
 *   them; then color0 = the larger 565 end; indices as in DXT1, by Manhattan distance. The same
 *   without the inset is the second candidate, taken when its sum of distances is smaller (a
 *   block of two colours, white text on black, comes out exact);
 * - alpha: four endpoint pairs, each with the decoder's palette (alpha0 > alpha1: 8 values, the
 *   six between them (a0 (8 - c) + a1 (c - 1)) / 7; else 6 values (a0 (6 - c) + a1 (c - 1)) / 5
 *   and 0, 255): A (max, min); B (lo, hi) of the values other than 0 and 255, 6-value mode, so
 *   0 and 255 stay exact (none: 0, 0); C as A inset by (max - min) >> 5; D as B inset by
 *   (hi - lo) >> 4. Each pixel takes its nearest palette entry (the lowest index on a tie); the
 *   pair with the smallest sum of squared errors wins, the earlier on a tie. A flat block (A's
 *   pair equal) is decoded in 6-value mode, but every pixel is then entry 0 either way (and the
 *   encoders take that block's answer at once). */
#include "dxt-ref.h"

static unsigned to565(const uint8_t c[3])
{
	return (c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3;
}

/* 565 back to 8 bits per channel by bit replication (Mesa's EXP5TO8R and friends) */
static void from565(uint8_t c[3], unsigned v)
{
	unsigned r = v >> 11, g = v >> 5 & 63, b = v & 31;

	c[0] = r << 3 | r >> 2;
	c[1] = g << 2 | g >> 4;
	c[2] = b << 3 | b >> 2;
}

/* The decoder's palette for color0/color1; returns the number of colours (4, or 3 with
 * index 3 transparent/black) */
static int palette(uint8_t pal[4][3], unsigned c0, unsigned c1)
{
	int k;

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
	return c0 > c1 ? 4 : 3;
}

static unsigned absd(unsigned a, unsigned b)
{
	return a > b ? a - b : b - a;
}

void dxt1_ref_block(uint8_t out[8], const uint8_t px[16][4], int punch)
{
	uint8_t lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 }, pal[4][3];
	unsigned c0, c1, bits = 0, transp = 0, i, k, j, n, d, best, bestd;

	for (i = 0; i < 16; i++) {
		if (punch && px[i][3] <= DXT1_ALPHACUT) {
			transp |= 1u << i;
			continue;
		}
		for (k = 0; k < 3; k++) {
			if (px[i][k] < lo[k])
				lo[k] = px[i][k];
			if (px[i][k] > hi[k])
				hi[k] = px[i][k];
		}
	}
	if (transp == 0xffff) {	/* nothing to colour: black endpoints, all index 3 */
		c0 = c1 = 0;
		bits = 0xffffffff;
		goto emit;
	}
	for (k = 0; k < 3; k++) {	/* inset: lo + inset <= hi - inset always */
		unsigned inset = (hi[k] - lo[k]) >> 4;

		lo[k] += inset;
		hi[k] -= inset;
	}
	/* hi >= lo in every channel, so to565(hi) >= to565(lo): 4-colour mode unless equal */
	if (transp) {
		c0 = to565(lo);
		c1 = to565(hi);
	} else {
		c0 = to565(hi);
		c1 = to565(lo);
	}
	n = palette(pal, c0, c1);	/* 3-colour mode: opaque pixels search indices 0-2 only */
	for (i = 0; i < 16; i++) {
		if (transp >> i & 1) {
			bits |= 3u << 2 * i;
			continue;
		}
		best = 0;
		bestd = ~0u;
		for (j = 0; j < n; j++) {
			d = absd(px[i][0], pal[j][0]) + absd(px[i][1], pal[j][1]) +
			    absd(px[i][2], pal[j][2]);
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

void dxt1_ref_image(const uint8_t *src, unsigned srccomps, unsigned w, unsigned h,
		    uint8_t *dst, unsigned dst_stride, int punch)
{
	uint8_t px[16][4];
	unsigned bx, by, x, y;

	for (by = 0; by < h; by += 4) {
		uint8_t *out = dst + (by / 4) * (unsigned long)dst_stride;

		for (bx = 0; bx < w; bx += 4) {
			for (y = 0; y < 4; y++) {
				const uint8_t *p = src + ((unsigned long)(by + y) * w + bx) * srccomps;

				for (x = 0; x < 4; x++, p += srccomps) {
					px[y * 4 + x][0] = p[0];
					px[y * 4 + x][1] = p[1];
					px[y * 4 + x][2] = p[2];
					px[y * 4 + x][3] = srccomps == 4 ? p[3] : 255;
				}
			}
			dxt1_ref_block(out, (const uint8_t (*)[4])px, punch);
			out += 8;
		}
	}
}

void dxt1_decode_block(uint8_t px[16][4], const uint8_t in[8], int punch)
{
	uint8_t pal[4][3];
	unsigned c0 = in[0] | in[1] << 8, c1 = in[2] | in[3] << 8;
	unsigned long bits = in[4] | in[5] << 8 | in[6] << 16 | (unsigned long)in[7] << 24;
	int n = palette(pal, c0, c1), i, k;

	for (i = 0; i < 16; i++) {
		unsigned idx = bits >> 2 * i & 3;

		for (k = 0; k < 3; k++)
			px[i][k] = pal[idx][k];
		px[i][3] = n == 3 && idx == 3 && punch ? 0 : 255;
	}
}

/* --- DXT5 --- */

/* One colour candidate: the box inset (inset) or not, its diagonal, color0 the larger end;
 * returns the sum over the pixels of the distance to their entry (RGB, Manhattan) */
static unsigned color_try(uint8_t out[8], const uint8_t px[16][4], int inset)
{
	uint8_t lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 }, pal[4][3], t;
	unsigned c0, c1, bits = 0, i, k, j, n, d, best, bestd, sad = 0;
	int cen[3], crg = 0, cbg = 0;

	for (i = 0; i < 16; i++)
		for (k = 0; k < 3; k++) {
			if (px[i][k] < lo[k])
				lo[k] = px[i][k];
			if (px[i][k] > hi[k])
				hi[k] = px[i][k];
		}
	for (k = 0; k < 3; k++) {
		unsigned ins = inset ? (hi[k] - lo[k]) >> 4 : 0;

		lo[k] += ins;
		hi[k] -= ins;
		cen[k] = (lo[k] + hi[k] + 1) >> 1;
	}
	for (i = 0; i < 16; i++) {
		int dg = px[i][1] - cen[1];

		crg += (px[i][0] - cen[0]) * dg;
		cbg += (px[i][2] - cen[2]) * dg;
	}
	if (crg < 0) {
		t = lo[0];
		lo[0] = hi[0];
		hi[0] = t;
	}
	if (cbg < 0) {
		t = lo[2];
		lo[2] = hi[2];
		hi[2] = t;
	}
	c0 = to565(hi);
	c1 = to565(lo);
	if (c0 < c1) {
		k = c0;
		c0 = c1;
		c1 = k;
	}
	n = palette(pal, c0, c1);	/* equal ends: 3 entries, all that colour: index 0 */
	for (i = 0; i < 16; i++) {
		best = 0;
		bestd = ~0u;
		for (j = 0; j < n; j++) {
			d = absd(px[i][0], pal[j][0]) + absd(px[i][1], pal[j][1]) +
			    absd(px[i][2], pal[j][2]);
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

void dxt5_color_block(uint8_t out[8], const uint8_t px[16][4])
{
	uint8_t alt[8];
	unsigned i;

	if (color_try(alt, px, 0) < color_try(out, px, 1))
		for (i = 0; i < 8; i++)
			out[i] = alt[i];
}

/* The decoder's alpha palette, in the mode given (eight: the 8-value formula) */
static void alpha_palette(uint8_t pal[8], unsigned a0, unsigned a1, int eight)
{
	unsigned c;

	pal[0] = a0;
	pal[1] = a1;
	for (c = 2; c < 8; c++)
		pal[c] = eight ? (a0 * (8 - c) + a1 * (c - 1)) / 7 :
			 c < 6 ? (a0 * (6 - c) + a1 * (c - 1)) / 5 : c == 6 ? 0 : 255;
}

/* Each value's nearest entry (the lowest index on a tie); returns the sum of squared errors */
static uint32_t alpha_fit(const uint8_t a[16], const uint8_t pal[8], uint8_t idx[16])
{
	uint32_t sse = 0;
	unsigned i, c, d, bestd;

	for (i = 0; i < 16; i++) {
		idx[i] = 0;
		bestd = absd(a[i], pal[0]);
		for (c = 1; c < 8; c++) {
			d = absd(a[i], pal[c]);
			if (d < bestd) {
				bestd = d;
				idx[i] = c;
			}
		}
		sse += bestd * bestd;
	}
	return sse;
}

void dxt5_alpha_block(uint8_t out[8], const uint8_t a[16])
{
	unsigned mn = 255, mx = 0, lo = 255, hi = 0, i, k, ins, e[4][2], best = 0;
	uint8_t pal[8], idx[16], bidx[16];
	uint32_t sse, bsse = 0;
	uint64_t bits = 0;

	for (i = 0; i < 16; i++) {
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
	if (mn == mx) {	/* flat (opaque, clear): A, every index 0, as the search below gives */
		out[0] = out[1] = mn;
		for (i = 2; i < 8; i++)
			out[i] = 0;
		return;
	}
	if (lo > hi)	/* only 0 and 255 */
		lo = hi = 0;
	ins = (mx - mn) >> 5;
	e[0][0] = mx;			/* A: 8 values (when mx > mn) */
	e[0][1] = mn;
	e[1][0] = lo;			/* B: 6 values, 0 and 255 exact */
	e[1][1] = hi;
	e[2][0] = mx - ins;		/* C: A inset */
	e[2][1] = mn + ins;
	ins = (hi - lo) >> 4;
	e[3][0] = lo + ins;		/* D: B inset */
	e[3][1] = hi - ins;
	for (k = 0; k < 4; k++) {
		alpha_palette(pal, e[k][0], e[k][1], !(k & 1));
		sse = alpha_fit(a, pal, idx);
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

void dxt5_ref_block(uint8_t out[16], const uint8_t px[16][4])
{
	uint8_t a[16];
	unsigned i;

	for (i = 0; i < 16; i++)
		a[i] = px[i][3];
	dxt5_alpha_block(out, a);
	dxt5_color_block(out + 8, px);
}

void dxt5_ref_image(const uint8_t *src, unsigned srccomps, unsigned w, unsigned h,
		    uint8_t *dst, unsigned dst_stride)
{
	uint8_t px[16][4];
	unsigned bx, by, x, y;

	for (by = 0; by < h; by += 4) {
		uint8_t *out = dst + (by / 4) * (unsigned long)dst_stride;

		for (bx = 0; bx < w; bx += 4) {
			for (y = 0; y < 4; y++) {
				const uint8_t *p = src + ((unsigned long)(by + y) * w + bx) * srccomps;

				for (x = 0; x < 4; x++, p += srccomps) {
					px[y * 4 + x][0] = p[0];
					px[y * 4 + x][1] = p[1];
					px[y * 4 + x][2] = p[2];
					px[y * 4 + x][3] = srccomps == 4 ? p[3] : 255;
				}
			}
			dxt5_ref_block(out, (const uint8_t (*)[4])px);
			out += 16;
		}
	}
}

void dxt5_decode_block(uint8_t px[16][4], const uint8_t in[16])
{
	uint8_t pal[8], cp[4][3];
	unsigned c0 = in[8] | in[9] << 8, c1 = in[10] | in[11] << 8, i, k;
	unsigned long cbits = in[12] | in[13] << 8 | in[14] << 16 | (unsigned long)in[15] << 24;
	uint64_t abits = 0;

	for (i = 0; i < 6; i++)
		abits |= (uint64_t)in[2 + i] << 8 * i;
	alpha_palette(pal, in[0], in[1], in[0] > in[1]);
	from565(cp[0], c0);
	from565(cp[1], c1);
	for (k = 0; k < 3; k++) {	/* 4-colour mode whatever the order */
		cp[2][k] = (2 * cp[0][k] + cp[1][k]) / 3;
		cp[3][k] = (cp[0][k] + 2 * cp[1][k]) / 3;
	}
	for (i = 0; i < 16; i++) {
		for (k = 0; k < 3; k++)
			px[i][k] = cp[cbits >> 2 * i & 3][k];
		px[i][3] = pal[abits >> 3 * i & 7];
	}
}
