/* pngdec [-i] FILE...: decode PNGs as Irrlicht's loader does (classic libpng API: expand palette and
 * gray, strip 16-bit, packing, gray to RGB), or with -i only the zlib part libpng does for them:
 * every chunk's crc32 and the IDAT stream inflated (adler32 included). Prints a checksum. */
#include <png.h>
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void dump_maps(void)	/* MAPS=FILE: the guest's mappings, for the PC attribution */
{
	const char *o = getenv("MAPS"); if (!o) return;
	FILE *i = fopen("/proc/self/maps", "r"), *w = fopen(o, "w"); char l[512];
	while (i && w && fgets(l, sizeof l, i)) fputs(l, w);
	if (i) fclose(i); if (w) fclose(w);
}
static unsigned char *slurp(const char *p, size_t *n)
{
	FILE *f = fopen(p, "rb"); if (!f) return NULL;
	fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
	unsigned char *b = malloc(*n); if (fread(b, 1, *n, f) != *n) { fclose(f); free(b); return NULL; }
	fclose(f); return b;
}
static unsigned be32(const unsigned char *p) { return (unsigned)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
struct mem { const unsigned char *p; size_t n, o; };
static void rd(png_structp png, png_bytep d, png_size_t n)
{
	struct mem *m = png_get_io_ptr(png);
	if (m->o + n > m->n) png_error(png, "short");
	memcpy(d, m->p + m->o, n); m->o += n;
}
int main(int argc, char **argv)
{
	int inf = argc > 1 && !strcmp(argv[1], "-i");
	unsigned long sum = 0, outb = 0;
	for (int a = 1 + inf; a < argc; a++) {
		size_t n; unsigned char *b = slurp(argv[a], &n);
		if (!b || n < 8 || png_sig_cmp(b, 0, 8)) { free(b); continue; }
		if (inf) {
			size_t o = 8, cap = 1 << 20, idl = 0; unsigned w = 0, h = 0, bd = 0, ct = 0;
			unsigned char *id = malloc(cap);
			while (o + 12 <= n) {
				unsigned len = be32(b + o);
				if (o + 12 + len > n) break;
				unsigned c = crc32(crc32(0, NULL, 0), b + o + 4, len + 4);
				sum += c == be32(b + o + 8 + len);
				if (!memcmp(b + o + 4, "IHDR", 4)) { w = be32(b + o + 8); h = be32(b + o + 12); bd = b[o + 16]; ct = b[o + 17]; }
				if (!memcmp(b + o + 4, "IDAT", 4)) {
					while (idl + len > cap) id = realloc(id, cap *= 2);
					memcpy(id + idl, b + o + 8, len); idl += len;
				}
				o += 12 + len;
			}
			int ch = ct == 2 ? 3 : ct == 6 ? 4 : ct == 4 ? 2 : 1;
			uLongf ol = (uLongf)h * (((unsigned long)w * ch * bd + 7) / 8 + 1);
			unsigned char *out = malloc(ol ? ol : 1);
			if (uncompress(out, &ol, id, idl) == Z_OK) { outb += ol; for (uLongf k = 0; k < ol; k += 997) sum += out[k]; }
			free(out); free(id);
		} else {
			png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
			png_infop info = png_create_info_struct(png);
			unsigned char ** volatile rows = NULL, * volatile img = NULL;
			if (setjmp(png_jmpbuf(png))) { fprintf(stderr, "png error: %s\n", argv[a]); png_destroy_read_struct(&png, &info, NULL); free(rows); free(img); free(b); continue; }
			struct mem m = { b, n, 0 };
			png_set_read_fn(png, &m, rd);
			png_read_info(png, info);
			png_uint_32 w, h; int bd, ct;
			png_get_IHDR(png, info, &w, &h, &bd, &ct, NULL, NULL, NULL);
			if (ct == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
			if (bd < 8) { if (ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_expand_gray_1_2_4_to_8(png); else png_set_packing(png); }
			if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
			if (bd == 16) png_set_strip_16(png);
			if (ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
			int passes = png_set_interlace_handling(png);
			(void)passes;
			png_read_update_info(png, info);
			size_t rb = png_get_rowbytes(png, info);
			img = malloc(rb * h); rows = malloc(sizeof *rows * h);
			for (png_uint_32 y = 0; y < h; y++) rows[y] = img + y * rb;
			png_read_image(png, rows);
			png_read_end(png, NULL);
			outb += rb * h; for (size_t k = 0; k < rb * h; k += 997) sum += img[k];
			png_destroy_read_struct(&png, &info, NULL); free(rows); free(img);
		}
		free(b);
	}
	printf("%s bytes=%lu checksum=%lu zlib=%s\n", inf ? "inflate" : "png", outb, sum, zlibVersion());
	dump_maps();
	return 0;
}
