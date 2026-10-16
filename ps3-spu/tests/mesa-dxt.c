/* mesa-dxt dxt1|dxt1a <image.pam> <out.blocks> [N]: Mesa's own DXT1 encoder (tx_compress_dxt1
 * from src/util/format/texcompress_s3tc_tmp.h, compiled as is against tests/shim) on a PAM
 * written by tests/make-corpus.py, for speq-dxt --compare; N: also time N encodes (Mpixel/s).
 * Called as Mesa's texstore calls it: RGB DXT1 from the image's own components, RGBA DXT1 from 4. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define UNUSED __attribute__((unused))
#define UNREACHABLE(msg) abort()
#include "util/format/texcompress_s3tc_tmp.h"

int main(int argc, char **argv)
{
	unsigned w = 0, h = 0, comps = 0, maxval = 0, n = argc > 4 ? atoi(argv[4]) : 0, i;
	char tuple[32], line[128];
	int dxt1a;
	size_t len, blen;
	GLubyte *px, *blocks;
	FILE *f;

	if (argc < 4 || (strcmp(argv[1], "dxt1") && strcmp(argv[1], "dxt1a"))) {
		fprintf(stderr, "usage: mesa-dxt dxt1|dxt1a <image.pam> <out.blocks> [N]\n");
		return 2;
	}
	dxt1a = !strcmp(argv[1], "dxt1a");
	if (!(f = fopen(argv[2], "rb")) || !fgets(line, sizeof(line), f) || strcmp(line, "P7\n")) {
		fprintf(stderr, "%s: not a PAM\n", argv[2]);
		return 1;
	}
	while (fgets(line, sizeof(line), f) && strcmp(line, "ENDHDR\n")) {
		sscanf(line, "WIDTH %u", &w);
		sscanf(line, "HEIGHT %u", &h);
		sscanf(line, "DEPTH %u", &comps);
		sscanf(line, "MAXVAL %u", &maxval);
		sscanf(line, "TUPLTYPE %31s", tuple);
	}
	if (maxval != 255 || (comps != 3 && comps != 4) || w % 4 || h % 4 || !w || !h) {
		fprintf(stderr, "%s: want 8-bit RGB(A), sides multiples of 4\n", argv[2]);
		return 1;
	}
	len = (size_t)w * h * comps;
	blen = (size_t)w * h / 2;
	px = malloc(len);
	blocks = malloc(blen);
	if (!px || !blocks || fread(px, 1, len, f) != len) {
		fprintf(stderr, "%s: truncated\n", argv[2]);
		return 1;
	}
	fclose(f);
	if (dxt1a && comps == 3) {	/* _mesa_texstore_rgba_dxt1 always passes RGBA */
		GLubyte *rgba = malloc((size_t)w * h * 4);

		for (i = 0; i < w * h; i++) {
			memcpy(rgba + i * 4, px + i * 3, 3);
			rgba[i * 4 + 3] = 255;
		}
		free(px);
		px = rgba;
		comps = 4;
	}
	tx_compress_dxt1(comps, w, h, px, blocks, w * 2, dxt1a ? 4 : 3);
	if (!(f = fopen(argv[3], "wb")) || fwrite(blocks, 1, blen, f) != blen || fclose(f)) {
		fprintf(stderr, "%s: write failed\n", argv[3]);
		return 1;
	}
	if (n) {
		struct timespec t0, t1;
		double t;

		clock_gettime(CLOCK_MONOTONIC, &t0);
		for (i = 0; i < n; i++)
			tx_compress_dxt1(comps, w, h, px, blocks, w * 2, dxt1a ? 4 : 3);
		clock_gettime(CLOCK_MONOTONIC, &t1);
		t = t1.tv_sec - t0.tv_sec + (t1.tv_nsec - t0.tv_nsec) / 1e9;
		printf("mesa   bench %u x %ux%u in %.3f s: %.2f Mpixel/s\n", n, w, h, t,
		       (double)n * w * h / t / 1e6);
	}
	return 0;
}
