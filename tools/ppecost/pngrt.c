/* pngrt: round trip through libpng for the unfilters: for every colour type and bit depth, plain and
 * Adam7, widths 1..WMAX, each filter type forced on every row and the per-row heuristic, content random,
 * smooth or flat: write with libpng's writer (its forward filters), read back, compare with the
 * original pixels. The reader uses whatever unfilters the library under test has (run it with the stock
 * library and with ours). Prints the number of images and mismatches; exit status 1 on any. */
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct buf { unsigned char *p; size_t n, cap, o; };
static void wr(png_structp png, png_bytep d, size_t n)
{
	struct buf *b = png_get_io_ptr(png);
	if (b->n + n > b->cap) { b->cap = (b->n + n) * 2; b->p = realloc(b->p, b->cap); }
	memcpy(b->p + b->n, d, n); b->n += n;
}
static void fl(png_structp png) { (void)png; }
static void rd(png_structp png, png_bytep d, size_t n)
{
	struct buf *b = png_get_io_ptr(png);
	if (b->o + n > b->n) png_error(png, "short");
	memcpy(d, b->p + b->o, n); b->o += n;
}
static unsigned rs = 12345;
static unsigned rnd(void) { rs = rs * 1103515245u + 12345u; return rs >> 8; }
int main(int argc, char **argv)
{
	int wmax = argc > 1 ? atoi(argv[1]) : 70;
	struct { int ct, bd, ch; } fm[] = {
		{ 0, 1, 1 }, { 0, 2, 1 }, { 0, 4, 1 }, { 0, 8, 1 }, { 0, 16, 1 }, { 2, 8, 3 }, { 2, 16, 3 },
		{ 3, 1, 1 }, { 3, 2, 1 }, { 3, 4, 1 }, { 3, 8, 1 }, { 4, 8, 2 }, { 4, 16, 2 }, { 6, 8, 4 }, { 6, 16, 4 } };
	int filters[] = { PNG_FILTER_NONE, PNG_FILTER_SUB, PNG_FILTER_UP, PNG_FILTER_AVG, PNG_FILTER_PAETH, PNG_ALL_FILTERS };
	long images = 0, bad = 0;
	for (size_t t = 0; t < sizeof fm / sizeof *fm; t++)
	for (int il = 0; il < 2; il++)
	for (int w = 1; w <= wmax; w += (w < 40 ? 1 : 7))
	for (size_t fi = 0; fi < sizeof filters / sizeof *filters; fi++)
	for (int kind = 0; kind < 3; kind++) {
		int h = 1 + (w * 7 + (int)fi) % 19;
		size_t rb = ((size_t)w * fm[t].ch * fm[t].bd + 7) / 8;
		unsigned char *img = malloc(rb * h), *out = malloc(rb * h), **rows = malloc(h * sizeof *rows);
		unsigned char v = rnd();
		for (size_t k = 0; k < rb * h; k++) img[k] = kind == 0 ? rnd() : kind == 1 ? (v += rnd() % 5 - 2) : (k / 7 & 1) * 255;
		if (((size_t)w * fm[t].ch * fm[t].bd) % 8)	/* the padding bits of a row are not data */
			for (int y = 0; y < h; y++) img[(y + 1) * rb - 1] &= (unsigned char)~((1u << (8 - (w * fm[t].ch * fm[t].bd) % 8)) - 1);
		struct buf b = { 0 };
		png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		png_infop info = png_create_info_struct(png);
		if (setjmp(png_jmpbuf(png))) { fprintf(stderr, "write error\n"); return 2; }
		png_set_write_fn(png, &b, wr, fl);
		png_set_IHDR(png, info, w, h, fm[t].bd, fm[t].ct, il ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
		if (fm[t].ct == 3) { png_color pal[256]; for (int i = 0; i < 256; i++) pal[i].red = i, pal[i].green = i * 3, pal[i].blue = i * 7; png_set_PLTE(png, info, pal, 1 << (fm[t].bd > 8 ? 8 : fm[t].bd)); }
		png_set_filter(png, 0, filters[fi]);
		png_set_compression_level(png, 1);
		png_write_info(png, info);
		if (il) png_set_interlace_handling(png);
		for (int y = 0; y < h; y++) rows[y] = img + y * rb;
		if (fm[t].bd == 16) png_set_swap(png);
		png_write_image(png, rows);
		png_write_end(png, NULL);
		png_destroy_write_struct(&png, &info);
		/* read back */
		png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		info = png_create_info_struct(png);
		if (setjmp(png_jmpbuf(png))) { fprintf(stderr, "read error ct=%d bd=%d w=%d\n", fm[t].ct, fm[t].bd, w); bad++; images++; continue; }
		png_set_read_fn(png, &b, rd);
		png_read_info(png, info);
		if (il) png_set_interlace_handling(png);
		if (fm[t].bd == 16) png_set_swap(png);
		png_read_update_info(png, info);
		memset(out, 0xa5, rb * h);
		for (int y = 0; y < h; y++) rows[y] = out + y * rb;
		png_read_image(png, rows);
		png_destroy_read_struct(&png, &info, NULL);
		images++;
		if (((size_t)w * fm[t].ch * fm[t].bd) % 8)	/* what the reader leaves in a row's padding bits is not data either */
			for (int y = 0; y < h; y++) out[(y + 1) * rb - 1] &= (unsigned char)~((1u << (8 - (w * fm[t].ch * fm[t].bd) % 8)) - 1);
		if (memcmp(img, out, rb * h)) { bad++; fprintf(stderr, "MISMATCH ct=%d bd=%d il=%d w=%d h=%d filter=%d kind=%d\n", fm[t].ct, fm[t].bd, il, w, h, filters[fi], kind); }
		free(b.p); free(img); free(out); free(rows);
	}
	printf("%ld images, %ld mismatches (libpng %s)\n", images, bad, png_get_libpng_ver(NULL));
	return bad != 0;
}
