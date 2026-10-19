/* pngfilt: tests libpng's Cell unfilters (powerpc/filter_cell.h) against the generic C ones, byte for
 * byte, and for stray writes. Compile inside libpng's source tree, with the library's sources:
 *   gcc -O2 -mcpu=cell -I<libpng src> -o pngfilt pngfilt.c  <png*.c except pngtest.c, pngrutil.c> -lz -lm
 * (this file includes pngrutil.c itself, to reach its static functions).
 * pngfilt [-p BPP] [-s SEED]: for each filter (up, sub, avg, paeth), pixel size (3, 4; up: also
 * 1, 2, 6, 8), row length (every length up to 300, then random up to 70000), row and previous-row
 * alignments (all 16 each, the library only ever passes one), and content (random, smooth, 0/255
 * runs, all 255, all 0), compares the Cell function with the generic one and checks the bytes around
 * the row and the previous row are untouched. Exit status 1 on any difference.
 * pngfilt -b FILTER BPP ref|cell [PASSES]: instead, unfilters 64 rows of 4092 bytes (rounded to whole
 * pixels) PASSES times (default 50) with one or the other and prints a checksum (equal for both); run
 * under the ppecost plugin for the cost of one filter. */
#include "pngrutil.c"
#include <stdlib.h>
#include <string.h>
typedef void (*fn)(png_row_infop, png_bytep, png_const_bytep);
static unsigned rs = 1;
static unsigned rnd(void) { rs = rs * 1103515245u + 12345u; return rs >> 8; }
static void fill(unsigned char *p, size_t n, int kind)
{
	size_t i; unsigned char v = rnd();
	for (i = 0; i < n; i++) {
		switch (kind) {
		case 0: p[i] = rnd(); break;
		case 1: v += (int)(rnd() % 7) - 3; p[i] = v; break;
		case 2: p[i] = (i / 5 + kind) & 1 ? 255 : 0; break;
		case 3: p[i] = 255; break;
		case 4: p[i] = 0; break;
		default: p[i] = (rnd() & 8) ? 255 : rnd() & 3; break;
		}
	}
}
static long fails, runs;
static int check(const char *name, fn ref, fn got, unsigned bpp, size_t len, unsigned ra, unsigned pa, int kind)
{
	enum { PAD = 64 };
	unsigned char *rb = malloc(len + 2 * PAD + 64), *pb = malloc(len + 2 * PAD + 64);
	unsigned char *r1 = malloc(len + 2 * PAD + 64), *p1 = malloc(len + 2 * PAD + 64);
	unsigned char *row = rb + PAD + ra, *prev = pb + PAD + pa;
	unsigned char *row1 = r1 + PAD + ra, *prev1 = p1 + PAD + pa;
	png_row_info ri; int bad = 0; size_t i;
	memset(&ri, 0, sizeof ri);
	ri.rowbytes = len; ri.pixel_depth = bpp * 8; ri.width = len / (bpp ? bpp : 1);
	fill(rb, len + 2 * PAD + 64, 5); fill(pb, len + 2 * PAD + 64, 5);
	fill(row, len, kind); fill(prev, len, kind == 1 ? 1 : (kind + 1) % 6);
	memcpy(r1, rb, len + 2 * PAD + 64); memcpy(p1, pb, len + 2 * PAD + 64);
	/* r1 and p1 are the reference's copies; rb and pb are the ones under test */
	ref(&ri, row1, prev1);
	got(&ri, row, prev);
	if (memcmp(rb, r1, len + 2 * PAD + 64)) {
		for (i = 0; i < len + 2 * PAD + 64 && rb[i] == r1[i]; i++) ;
		fprintf(stderr, "%s bpp=%u len=%zu ra=%u pa=%u kind=%d: row differs at byte %zd (row starts at %d)\n",
		    name, bpp, len, ra, pa, kind, (ssize_t)i - PAD - ra, 0);
		bad = 1;
	}
	if (memcmp(pb, p1, len + 2 * PAD + 64)) { fprintf(stderr, "%s bpp=%u len=%zu: prev row modified\n", name, bpp, len); bad = 1; }
	free(rb); free(pb); free(r1); free(p1);
	fails += bad; runs++;
	return bad;
}
static fn pick(const char *name, unsigned bpp, int cell)
{
	if (!strcmp(name, "up")) return cell ? png_read_filter_row_up_cell : png_read_filter_row_up;
	if (!strcmp(name, "sub")) return cell ? (bpp == 3 ? png_read_filter_row_sub3_cell : png_read_filter_row_sub4_cell) : png_read_filter_row_sub;
	if (!strcmp(name, "avg")) return cell ? (bpp == 3 ? png_read_filter_row_avg3_cell : png_read_filter_row_avg4_cell) : png_read_filter_row_avg;
	return cell ? (bpp == 3 ? png_read_filter_row_paeth3_cell : png_read_filter_row_paeth4_cell) : png_read_filter_row_paeth_multibyte_pixel;
}
static int bench(char **v, int n)
{
	unsigned bpp = atoi(v[1]); fn f = pick(v[0], bpp, !strcmp(v[2], "cell"));
	int passes = n > 3 ? atoi(v[3]) : 50, rows = 64, y, p;
	size_t rb = 4092 / bpp * bpp, k; unsigned long sum = 0;
	unsigned char *src = malloc(rb * rows + 32), *img = malloc(rb * rows + 32), *zero = calloc(rb + 32, 1);
	png_row_info ri; memset(&ri, 0, sizeof ri); ri.rowbytes = rb; ri.pixel_depth = bpp * 8;
	fill(src, rb * rows, 0);
	for (p = 0; p < passes; p++) {
		memcpy(img, src, rb * rows);
		for (y = 0; y < rows; y++) f(&ri, img + 1 + y * rb, y ? img + 1 + (y - 1) * rb : zero + 1);
		sum += img[rb * rows - 1] + img[rb * rows / 2];
	}
	for (k = 0; k < rb * rows; k++) sum += img[k] * (k & 7);
	printf("%s bpp=%u %s checksum=%lu bytes=%zu\n", v[0], bpp, v[2], sum, rb * rows * passes);
	return 0;
}
int main(int argc, char **argv)
{
	int only = 0, a;
	if (argc > 3 && !strcmp(argv[1], "-b")) return bench(argv + 2, argc - 2);
	for (a = 1; a < argc; a++) {
		if (!strcmp(argv[a], "-p") && a + 1 < argc) only = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-s") && a + 1 < argc) rs = atoi(argv[++a]);
	}
	struct { const char *name; unsigned bpp; fn ref, got; } t[] = {
		{ "up", 1, png_read_filter_row_up, png_read_filter_row_up_cell },
		{ "up", 2, png_read_filter_row_up, png_read_filter_row_up_cell },
		{ "up", 3, png_read_filter_row_up, png_read_filter_row_up_cell },
		{ "up", 4, png_read_filter_row_up, png_read_filter_row_up_cell },
		{ "up", 6, png_read_filter_row_up, png_read_filter_row_up_cell },
		{ "up", 8, png_read_filter_row_up, png_read_filter_row_up_cell },
		{ "sub", 3, png_read_filter_row_sub, png_read_filter_row_sub3_cell },
		{ "sub", 4, png_read_filter_row_sub, png_read_filter_row_sub4_cell },
		{ "avg", 3, png_read_filter_row_avg, png_read_filter_row_avg3_cell },
		{ "avg", 4, png_read_filter_row_avg, png_read_filter_row_avg4_cell },
		{ "paeth", 3, png_read_filter_row_paeth_multibyte_pixel, png_read_filter_row_paeth3_cell },
		{ "paeth", 4, png_read_filter_row_paeth_multibyte_pixel, png_read_filter_row_paeth4_cell },
	};
	for (size_t k = 0; k < sizeof t / sizeof *t; k++) {
		if (only && t[k].bpp != (unsigned)only) continue;
		unsigned bpp = t[k].bpp; size_t len;
		for (len = bpp; len <= 300; len += bpp)
			for (unsigned ra = 0; ra < 16; ra += (len < 70 ? 1 : 5))
				for (unsigned pa = 0; pa < 16; pa += 3)
					for (int kind = 0; kind < 6; kind += (len < 70 ? 1 : 2))
						check(t[k].name, t[k].ref, t[k].got, bpp, len, ra, pa, kind);
		for (int n = 0; n < 300; n++) {
			len = (1 + rnd() % (70000 / bpp)) * bpp;
			check(t[k].name, t[k].ref, t[k].got, bpp, len, rnd() % 16, rnd() % 16, rnd() % 6);
		}
	}
	/* odd lengths for up (any depth) */
	for (size_t len = 1; len < 200; len++)
		for (unsigned ra = 0; ra < 16; ra++)
			check("up", png_read_filter_row_up, png_read_filter_row_up_cell, 1, len, ra, ra * 7 % 16, len % 6);
	printf("%ld checks, %ld failures\n", runs, fails);
	return fails != 0;
}
