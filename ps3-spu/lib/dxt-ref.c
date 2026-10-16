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
 * (all its palette entries are the same colour, so every pixel takes index 0). */
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
