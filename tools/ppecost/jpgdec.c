/* jpgdec FILE...: decode JPEGs to RGB with libjpeg's defaults (islow IDCT, fancy upsampling), as
 * Irrlicht's loader does; JSIMD_FORCENONE=1 / JSIMD_FORCEALTIVEC=1 pick libjpeg-turbo's path */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>
static void dump_maps(void)
{
	const char *o = getenv("MAPS"); if (!o) return;
	FILE *i = fopen("/proc/self/maps", "r"), *w = fopen(o, "w"); char l[512];
	while (i && w && fgets(l, sizeof l, i)) fputs(l, w);
	if (i) fclose(i); if (w) fclose(w);
}
int main(int argc, char **argv)
{
	unsigned long sum = 0, outb = 0;
	for (int a = 1; a < argc; a++) {
		FILE *f = fopen(argv[a], "rb"); if (!f) continue;
		struct jpeg_decompress_struct c; struct jpeg_error_mgr e;
		c.err = jpeg_std_error(&e); jpeg_create_decompress(&c);
		jpeg_stdio_src(&c, f); jpeg_read_header(&c, TRUE);
		c.out_color_space = JCS_RGB;
		jpeg_start_decompress(&c);
		size_t rb = (size_t)c.output_width * c.output_components;
		unsigned char *row = malloc(rb);
		while (c.output_scanline < c.output_height) {
			JSAMPROW r = row; jpeg_read_scanlines(&c, &r, 1);
			outb += rb; sum += row[rb / 2];
		}
		jpeg_finish_decompress(&c); jpeg_destroy_decompress(&c); free(row); fclose(f);
	}
	printf("jpeg bytes=%lu checksum=%lu\n", outb, sum);
	dump_maps();
	return 0;
}
