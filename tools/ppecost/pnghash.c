/* pnghash FILE...: decodes each PNG with libpng's raw rows (no transformations, Adam7 passes
 * expanded, 16-bit as stored) and prints "FNV-1a-64 of the rows, name"; run it with two libpng builds
 * (LD_LIBRARY_PATH) and diff the outputs: the unfilters must give the same bytes. A file libpng
 * rejects prints "error". */
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
int main(int argc, char **argv)
{
	for (int a = 1; a < argc; a++) {
		FILE *f = fopen(argv[a], "rb"); if (!f) continue;
		png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		png_infop info = png_create_info_struct(png);
		unsigned char ** volatile rows = NULL; volatile png_uint_32 hh = 0;
		if (setjmp(png_jmpbuf(png))) {
			printf("error %s\n", argv[a]);
			if (rows) { for (png_uint_32 y = 0; y < hh; y++) free(rows[y]); free(rows); }
			png_destroy_read_struct(&png, &info, NULL); fclose(f); continue;
		}
		png_set_user_limits(png, 20000, 20000);
		png_init_io(png, f);
		png_read_info(png, info);
		png_set_interlace_handling(png);
		png_read_update_info(png, info);
		png_uint_32 h = png_get_image_height(png, info); size_t rb = png_get_rowbytes(png, info);
		rows = calloc(h, sizeof *rows); hh = h;
		for (png_uint_32 y = 0; y < h; y++) rows[y] = calloc(1, rb);
		png_read_image(png, rows);
		uint64_t hash = 14695981039346656037ull;
		for (png_uint_32 y = 0; y < h; y++) for (size_t k = 0; k < rb; k++) hash = (hash ^ rows[y][k]) * 1099511628211ull;
		printf("%016llx %s\n", (unsigned long long)hash, argv[a]);
		for (png_uint_32 y = 0; y < h; y++) free(rows[y]);
		free(rows); png_destroy_read_struct(&png, &info, NULL); fclose(f);
	}
	return 0;
}
