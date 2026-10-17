/* dxt-gen: writes dxt-test's data header (host tool, run at build time by dxt-test.mk):
 * the test images, RGBA8, and the reference encoder's blocks for both DXT1 modes and DXT5
 * (lib/dxt-ref.c).
 *   dxt-gen > dxt-test-data.h
 * Images: speq-dxt --check's special blocks and more corner cases, noise, gradients and a few
 * generated texture-like 64x64 crops (stone, bricks, alpha-tested foliage, a low-contrast sky). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dxt-ref.h"

struct img {
	const char *name;
	unsigned w, h;
	uint8_t *px;
};

static uint32_t rng = 12345;

static unsigned rnd(void)	/* speq-dxt's generator */
{
	rng = rng * 1103515245 + 12345;
	return rng >> 16 & 0xff;
}

static struct img new_img(const char *name, unsigned w, unsigned h)
{
	struct img im = { name, w, h, calloc(w * h, 4) };

	if (!im.px)
		exit(1);
	return im;
}

static uint8_t *px(struct img *im, unsigned x, unsigned y)
{
	return im->px + (y * im->w + x) * 4;
}

static void set(uint8_t *p, unsigned r, unsigned g, unsigned b, unsigned a)
{
	p[0] = r;
	p[1] = g;
	p[2] = b;
	p[3] = a;
}

/* lattice value noise in [0,1), smooth, wrapping every 64 */
static double hashv(int x, int y, unsigned seed)
{
	uint32_t h = (x & 63) * 374761393u + (y & 63) * 668265263u + seed * 2246822519u;

	h = (h ^ h >> 13) * 1274126177u;
	return ((h ^ h >> 16) & 0xffff) / 65536.0;
}

static double vnoise(double x, double y, unsigned seed)
{
	int ix = (int)floor(x), iy = (int)floor(y);
	double fx = x - ix, fy = y - iy, a, b, c, d;

	fx = fx * fx * (3 - 2 * fx);
	fy = fy * fy * (3 - 2 * fy);
	a = hashv(ix, iy, seed);
	b = hashv(ix + 1, iy, seed);
	c = hashv(ix, iy + 1, seed);
	d = hashv(ix + 1, iy + 1, seed);
	return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

static double fbm(double x, double y, unsigned seed)
{
	return 0.5 * vnoise(x / 16, y / 16, seed) + 0.25 * vnoise(x / 8, y / 8, seed + 1) +
	       0.125 * vnoise(x / 4, y / 4, seed + 2) + 0.125 * vnoise(x / 2, y / 2, seed + 3);
}

static unsigned clamp(double v)
{
	return v < 0 ? 0 : v > 255 ? 255 : (unsigned)(v + 0.5);
}

/* 21 blocks, 7 by 3 (odd blocks per row: pairs straddle block rows) */
static struct img special(void)
{
	struct img im = new_img("special", 28, 12);
	uint8_t b[16][4];
	int n, i;

	for (n = 0; n < 21; n++) {
		for (i = 0; i < 16; i++)
			set(b[i], 0, 0, 0, 255);
		switch (n) {
		case 0:	/* flat red */
			for (i = 0; i < 16; i++)
				set(b[i], 255, 0, 0, 255);
			break;
		case 1:	/* left black, right white */
			for (i = 0; i < 16; i++)
				b[i][0] = b[i][1] = b[i][2] = i % 4 < 2 ? 0 : 255;
			break;
		case 2:
			for (i = 0; i < 16; i++)
				set(b[i], 200, 100, 50, 255);
			break;
		case 3:
			for (i = 0; i < 16; i++)
				set(b[i], 37, 201, 129, 255);
			break;
		case 4:	/* flat with transparency (speq-dxt's) */
			for (i = 0; i < 16; i++)
				set(b[i], 200, 100, 50, 255);
			for (i = 0; i < 16; i += 3)
				b[i][3] = i & 1 ? 127 : 0;
			b[1][3] = 128;
			break;
		case 5:	/* all transparent */
			for (i = 0; i < 16; i++)
				set(b[i], 90, 30, 250, 0);
			break;
		case 6:	/* top black, bottom white */
			for (i = 0; i < 16; i++)
				b[i][0] = b[i][1] = b[i][2] = i < 8 ? 0 : 255;
			break;
		case 7:	/* gradient along the box's diagonal */
			for (i = 0; i < 16; i++)
				set(b[i], i % 4 * 60 + i / 4 * 20, i % 4 * 40 + i / 4 * 30 + 10,
				    i % 4 * 20 + i / 4 * 10 + 100, 255);
			break;
		case 8:	/* grey ramp */
			for (i = 0; i < 16; i++)
				b[i][0] = b[i][1] = b[i][2] = i % 4 * 85;
			break;
		case 9:	/* alpha 127/128 alternating, random colours */
			for (i = 0; i < 16; i++)
				set(b[i], rnd(), rnd(), rnd(), i & 1 ? 128 : 127);
			break;
		case 10:	/* one opaque pixel */
			for (i = 0; i < 16; i++)
				set(b[i], rnd(), rnd(), rnd(), i == 9 ? 200 : rnd() & 127);
			break;
		case 11:	/* one transparent pixel */
			for (i = 0; i < 16; i++)
				set(b[i], rnd(), rnd(), rnd(), i == 14 ? 3 : 128 + (rnd() & 127));
			break;
		case 12:	/* inside one 565 cell: equal endpoints */
			for (i = 0; i < 16; i++)
				set(b[i], 200 + (rnd() & 7), 100 + (rnd() & 3), 48 + (rnd() & 7), 255);
			break;
		case 13:	/* range 15: no inset */
			for (i = 0; i < 16; i++)
				set(b[i], 100 + i, 50 + 15 - i, 7 + (i & 1) * 15, 255);
			break;
		case 14:	/* range 16: inset 1 */
			for (i = 0; i < 16; i++)
				set(b[i], 100 + i + (i == 15), 50 + 16 - i - (i == 15), 7 + (i & 1) * 16, 255);
			break;
		case 15:	/* full range, each channel its own axis */
			for (i = 0; i < 16; i++)
				set(b[i], i % 4 * 85, i / 4 * 85, (i % 4 + i / 4) * 255 / 6, 255);
			break;
		case 16:	/* primaries and white */
			for (i = 0; i < 16; i++)
				set(b[i], i % 4 == 0 || i % 4 == 3 ? 255 : 0, i % 4 == 1 || i % 4 == 3 ? 255 : 0,
				    i % 4 == 2 || i % 4 == 3 ? 255 : 0, 255);
			break;
		case 17:	/* two greys and the points between them: distance ties */
			for (i = 0; i < 16; i++)
				b[i][0] = b[i][1] = b[i][2] = (unsigned[]){ 0, 255, 127, 128, 85, 170, 42, 213,
									    64, 191, 0, 255, 96, 160, 31, 224 }[i];
			break;
		case 18:	/* random opaque */
			for (i = 0; i < 16; i++)
				set(b[i], rnd(), rnd(), rnd(), 255);
			break;
		case 19:	/* random with random alpha */
			for (i = 0; i < 16; i++)
				set(b[i], rnd(), rnd(), rnd(), rnd());
			break;
		case 20:	/* black (all zero, alpha 255) */
			break;
		}
		for (i = 0; i < 16; i++)
			memcpy(px(&im, n % 7 * 4 + i % 4, n / 7 * 4 + i / 4), b[i], 4);
	}
	return im;
}

static struct img noise(const char *name, unsigned w, unsigned h)
{
	struct img im = new_img(name, w, h);
	unsigned i;

	for (i = 0; i < w * h * 4; i++)
		im.px[i] = rnd();
	return im;
}

static struct img gradient(void)	/* speq-dxt's */
{
	struct img im = new_img("gradient", 64, 32);
	unsigned x, y;

	for (y = 0; y < 32; y++)
		for (x = 0; x < 64; x++)
			set(px(&im, x, y), x * 4, y * 8, 255 - x * 2, x < 8 ? 0 : 255);
	return im;
}

static struct img stone(void)
{
	struct img im = new_img("stone", 64, 64);
	unsigned x, y;

	for (y = 0; y < 64; y++)
		for (x = 0; x < 64; x++) {
			double v = fbm(x, y, 1), grain = (rnd() - 128) / 12.0;

			set(px(&im, x, y), clamp(70 + 110 * v + grain), clamp(62 + 100 * v + grain),
			    clamp(55 + 80 * v + grain * 0.8), 255);
		}
	return im;
}

static struct img bricks(void)
{
	struct img im = new_img("bricks", 64, 64);
	unsigned x, y;

	for (y = 0; y < 64; y++)
		for (x = 0; x < 64; x++) {
			unsigned row = y / 16, bx = (x + (row & 1) * 16) % 32, by = y % 16;
			int mortar = bx < 2 || by < 2;
			double v = fbm(x, y, 7), tint = hashv((x + (row & 1) * 16) / 32, row, 9);

			if (mortar)
				set(px(&im, x, y), clamp(150 + 40 * v), clamp(145 + 40 * v), clamp(135 + 40 * v), 255);
			else
				set(px(&im, x, y), clamp(120 + 60 * tint + 50 * v), clamp(45 + 20 * tint + 30 * v),
				    clamp(30 + 15 * tint + 20 * v), 255);
		}
	return im;
}

static struct img foliage(void)
{
	struct img im = new_img("foliage", 64, 64);
	unsigned x, y;

	for (y = 0; y < 64; y++)
		for (x = 0; x < 64; x++) {
			double v = fbm(x * 1.7, y * 1.7, 21), a = fbm(x * 2.3, y * 2.3, 33);

			/* alpha ramps through 127/128 at the leaf edges */
			set(px(&im, x, y), clamp(30 + 60 * v), clamp(70 + 140 * v), clamp(20 + 40 * v),
			    clamp((a - 0.45) * 2000 + 127.5));
		}
	return im;
}

static struct img sky(void)	/* smooth, low contrast: many equal-endpoint blocks */
{
	struct img im = new_img("sky", 64, 32);
	unsigned x, y;

	for (y = 0; y < 32; y++)
		for (x = 0; x < 64; x++) {
			double v = fbm(x, y, 55);

			set(px(&im, x, y), clamp(110 + 20 * v + y * 0.3), clamp(150 + 15 * v + y * 0.2),
			    clamp(220 + 10 * v), 255);
		}
	return im;
}

static void emit(const char *name, const uint8_t *p, size_t n)
{
	size_t i;

	printf("static const unsigned char %s[%zu] __attribute__((aligned(16))) = {", name, n);
	for (i = 0; i < n; i++)
		printf("%s%u,", i % 24 ? "" : "\n", p[i]);
	printf("\n};\n");
}

int main(void)
{
	struct img ims[9];
	unsigned n = 0, i, mode;
	char name[64];

	ims[n++] = special();
	ims[n++] = noise("noise", 64, 32);
	ims[n++] = noise("noise20", 20, 8);
	ims[n++] = gradient();
	ims[n++] = stone();
	ims[n++] = bricks();
	ims[n++] = foliage();
	ims[n++] = sky();

	printf("/* generated by dxt-gen: test images and lib/dxt-ref.c's blocks */\n");
	for (i = 0; i < n; i++) {
		size_t blen = ims[i].w / 4 * (ims[i].h / 4) * 8;
		uint8_t *ref = malloc(2 * blen);

		snprintf(name, sizeof(name), "img%u_px", i);
		emit(name, ims[i].px, ims[i].w * ims[i].h * 4);
		for (mode = 0; mode < 3; mode++) {	/* 2: DXT5 */
			if (mode == 2)
				dxt5_ref_image(ims[i].px, 4, ims[i].w, ims[i].h, ref, ims[i].w * 4);
			else
				dxt1_ref_image(ims[i].px, 4, ims[i].w, ims[i].h, ref, ims[i].w * 2, mode);
			snprintf(name, sizeof(name), "img%u_ref%u", i, mode);
			emit(name, ref, mode == 2 ? 2 * blen : blen);
		}
		free(ref);
	}
	printf("static const struct test_img test_imgs[] = {\n");
	for (i = 0; i < n; i++)
		printf("\t{ \"%s\", %u, %u, img%u_px, { img%u_ref0, img%u_ref1, img%u_ref2 } },\n",
		       ims[i].name, ims[i].w, ims[i].h, i, i, i, i);
	printf("};\n#define TEST_TIMING_IMG %u\t/* stone */\n", 4);
	return 0;
}
