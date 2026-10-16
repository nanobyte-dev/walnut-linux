/* speq-dxt: tests and benchmarks libspeq's DXT1 encoder.
 *   speq-dxt [--fmt dxt1|dxt1a] [--out f.dds|f.blocks] [--decode f.pam] [--compare other]
 *            [--bench N] [--raw WxH] image
 *   speq-dxt --check [image]
 * The image is binary PAM (P7, RGB or RGB_ALPHA) or PPM (P6), 8 bits, or with --raw WxH bare
 * RGBA bytes (gl-dxt's <fmt>.src); w and h multiples of 4.
 * It is encoded through speq_s3tc_compress_v1 (the path Mesa takes, SPEQ_S3TC applies) and the
 * RMSE of the decoded result against the original printed. --compare decodes another encoding
 * of the same image (raw blocks or DDS, e.g. Mesa's) and prints its RMSE next to ours.
 * --check: the reference encoder on special blocks (flat, transparent, two-colour, gradient),
 * its determinism, and the API against it (later: the SPU encoder against it, bit for bit).
 * The reference encoder and decoder are compiled in (lib/dxt-ref.c). */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dxt-ref.h"
#include "speq.h"

static void die(const char *fmt, const char *arg)
{
	fprintf(stderr, "speq-dxt: ");
	fprintf(stderr, fmt, arg);
	fputc('\n', stderr);
	exit(1);
}

static void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);

	if (!p)
		die("out of memory%s", "");
	return p;
}

struct image {
	unsigned w, h, comps;
	uint8_t *px;
};

static uint8_t *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf = NULL;
	size_t n = 0, cap = 0, r;

	if (!f)
		die("%s: cannot open", path);
	do {
		if (n == cap)
			buf = realloc(buf, cap = cap ? 2 * cap : 1 << 16);
		if (!buf)
			die("out of memory%s", "");
		r = fread(buf + n, 1, cap - n, f);
		n += r;
	} while (r);
	fclose(f);
	*len = n;
	return buf;
}

/* next whitespace-separated token of a PNM header, skipping # comments */
static const char *token(const uint8_t *buf, size_t len, size_t *pos, char *tok, size_t toksz)
{
	size_t n = 0;

	while (*pos < len) {
		if (buf[*pos] == '#')
			while (*pos < len && buf[*pos] != '\n')
				(*pos)++;
		else if (buf[*pos] == ' ' || buf[*pos] == '\t' || buf[*pos] == '\n' || buf[*pos] == '\r')
			(*pos)++;
		else
			break;
	}
	while (*pos < len && n + 1 < toksz && buf[*pos] > ' ')
		tok[n++] = buf[(*pos)++];
	tok[n] = 0;
	return n ? tok : NULL;
}

static void read_raw(const char *path, const char *size, struct image *im)
{
	size_t len;

	if (sscanf(size, "%ux%u", &im->w, &im->h) != 2 || !im->w || !im->h)
		die("--raw %s: want WxH", size);
	im->comps = 4;
	im->px = read_file(path, &len);
	if (len != (size_t)im->w * im->h * 4)
		die("%s: not WxH RGBA bytes", path);
}

static void read_image(const char *path, struct image *im)
{
	size_t len, pos = 2, need;
	uint8_t *buf = read_file(path, &len);
	char tok[32];
	unsigned maxval = 0;

	im->w = im->h = im->comps = 0;
	if (len > 2 && !memcmp(buf, "P6", 2)) {
		im->comps = 3;
		im->w = atoi(token(buf, len, &pos, tok, sizeof(tok)) ?: "0");
		im->h = atoi(token(buf, len, &pos, tok, sizeof(tok)) ?: "0");
		maxval = atoi(token(buf, len, &pos, tok, sizeof(tok)) ?: "0");
	} else if (len > 2 && !memcmp(buf, "P7", 2)) {
		while (token(buf, len, &pos, tok, sizeof(tok)) && strcmp(tok, "ENDHDR")) {
			char val[32];

			if (!token(buf, len, &pos, val, sizeof(val)))
				break;
			if (!strcmp(tok, "WIDTH"))
				im->w = atoi(val);
			else if (!strcmp(tok, "HEIGHT"))
				im->h = atoi(val);
			else if (!strcmp(tok, "DEPTH"))
				im->comps = atoi(val);
			else if (!strcmp(tok, "MAXVAL"))
				maxval = atoi(val);
			else if (!strcmp(tok, "TUPLTYPE") && strcmp(val, "RGB") && strcmp(val, "RGB_ALPHA"))
				die("%s: only RGB and RGB_ALPHA PAM", path);
		}
	} else
		die("%s: not a binary PPM or PAM", path);
	pos++;	/* the single whitespace after the header */
	if (maxval != 255 || (im->comps != 3 && im->comps != 4) || !im->w || !im->h)
		die("%s: need 8-bit RGB or RGBA", path);
	need = (size_t)im->w * im->h * im->comps;
	if (pos + need > len)
		die("%s: truncated", path);
	im->px = xmalloc(need);
	memcpy(im->px, buf + pos, need);
	free(buf);
}

static void write_file(const char *path, const void *hdr, size_t hlen, const void *data, size_t len)
{
	FILE *f = fopen(path, "wb");

	if (!f || (hlen && fwrite(hdr, 1, hlen, f) != hlen) || fwrite(data, 1, len, f) != len ||
	    fclose(f))
		die("%s: write failed", path);
}

static int has_suffix(const char *s, const char *suf)
{
	size_t n = strlen(s), m = strlen(suf);

	return n >= m && !strcmp(s + n - m, suf);
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

/* DDS header for one DXT1 surface, little-endian whatever the host */
static void dds_header(uint8_t h[128], unsigned w, unsigned h_, int punch)
{
	memset(h, 0, 128);
	memcpy(h, "DDS ", 4);
	put32(h + 4, 124);
	put32(h + 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000);	/* CAPS HEIGHT WIDTH PIXELFORMAT LINEARSIZE */
	put32(h + 12, h_);
	put32(h + 16, w);
	put32(h + 20, w / 4 * (h_ / 4) * 8);
	put32(h + 76, 32);					/* pixel format */
	put32(h + 80, 0x4 | (punch ? 0x1 : 0));		/* FOURCC, ALPHAPIXELS */
	memcpy(h + 84, "DXT1", 4);
	put32(h + 108, 0x1000);					/* TEXTURE */
}

static void decode_image(const uint8_t *blocks, unsigned w, unsigned h, int punch, uint8_t *rgba)
{
	uint8_t px[16][4];
	unsigned bx, by, y;

	for (by = 0; by < h; by += 4)
		for (bx = 0; bx < w; bx += 4, blocks += 8) {
			dxt1_decode_block(px, blocks, punch);
			for (y = 0; y < 4; y++)
				memcpy(rgba + ((size_t)(by + y) * w + bx) * 4, px[y * 4], 16);
		}
}

/* RMSE of an encoding against the original: R, G, B over the pixels that are opaque in the
 * original (all of them for RGB DXT1), A (RGBA DXT1 only) over all; total over all samples */
static double rmse(const char *label, const struct image *im, const uint8_t *blocks, int punch)
{
	uint8_t *dec = xmalloc((size_t)im->w * im->h * 4);
	double sum[4] = { 0 }, n_rgb = 0, n_a = 0, all;
	size_t i, n = (size_t)im->w * im->h;
	int k;

	decode_image(blocks, im->w, im->h, punch, dec);
	for (i = 0; i < n; i++) {
		const uint8_t *o = im->px + i * im->comps, *d = dec + i * 4;
		int a = im->comps == 4 ? o[3] : 255;

		if (punch) {
			sum[3] += (double)(a - d[3]) * (a - d[3]);
			n_a++;
			if (a <= DXT1_ALPHACUT)
				continue;
		}
		for (k = 0; k < 3; k++)
			sum[k] += (double)(o[k] - d[k]) * (o[k] - d[k]);
		n_rgb++;
	}
	all = sqrt((sum[0] + sum[1] + sum[2] + sum[3]) / (3 * n_rgb + n_a ? 3 * n_rgb + n_a : 1));
	free(dec);
	if (!label)
		return all;
	printf("%-6s rmse R %.3f G %.3f B %.3f", label, sqrt(sum[0] / (n_rgb ? n_rgb : 1)),
	       sqrt(sum[1] / (n_rgb ? n_rgb : 1)), sqrt(sum[2] / (n_rgb ? n_rgb : 1)));
	if (punch)
		printf(" A %.3f", sqrt(sum[3] / (n_a ? n_a : 1)));
	printf(" total %.3f\n", all);
	return all;
}

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* --- --check --- */

static int failures;

static void check(int ok, const char *what, const char *detail)
{
	printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", what, detail ? ": " : "", detail ? detail : "");
	if (!ok)
		failures++;
}

/* Encodes one 4x4 RGBA block with the reference, decodes it and checks: transparency where
 * (and only where) expected, no index 3 for an opaque pixel in 3-colour mode, the largest
 * channel error on the opaque pixels within maxerr. Returns the block for golden checks. */
static void check_block(const char *name, const uint8_t px[16][4], int punch, unsigned maxerr,
			uint8_t out[8])
{
	uint8_t dec[16][4];
	unsigned i, k, c0, c1, bits, worst = 0, holes = 0, alpha_bad = 0;
	char what[96], detail[128];

	dxt1_ref_block(out, px, punch);
	dxt1_decode_block(dec, out, punch);
	c0 = out[0] | out[1] << 8;
	c1 = out[2] | out[3] << 8;
	bits = out[4] | out[5] << 8 | out[6] << 16 | (unsigned)out[7] << 24;
	for (i = 0; i < 16; i++) {
		int transp = punch && px[i][3] <= DXT1_ALPHACUT;

		if (dec[i][3] != (transp ? 0 : 255))
			alpha_bad++;
		if (transp)
			continue;
		if (c0 <= c1 && (bits >> 2 * i & 3) == 3)
			holes++;
		for (k = 0; k < 3; k++) {
			unsigned e = px[i][k] > dec[i][k] ? px[i][k] - dec[i][k] : dec[i][k] - px[i][k];

			if (e > worst)
				worst = e;
		}
	}
	snprintf(what, sizeof(what), "%s %s", punch ? "dxt1a" : "dxt1 ", name);
	snprintf(detail, sizeof(detail), "%02x %02x %02x %02x %02x %02x %02x %02x, max error %u%s%s",
		 out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7], worst,
		 holes ? ", index 3 on an opaque pixel" : "", alpha_bad ? ", wrong alpha" : "");
	check(worst <= maxerr && !holes && !alpha_bad, what, detail);
}

static void fill(uint8_t px[16][4], unsigned r, unsigned g, unsigned b, unsigned a)
{
	int i;

	for (i = 0; i < 16; i++) {
		px[i][0] = r;
		px[i][1] = g;
		px[i][2] = b;
		px[i][3] = a;
	}
}

static uint32_t rng = 12345;

static unsigned rnd(void)
{
	rng = rng * 1103515245 + 12345;
	return rng >> 16 & 0xff;
}

/* An image both ways (RGB and RGBA DXT1): the reference twice gives the same bytes, its RMSE
 * against the image is within bound, and the API gives the reference's bytes */
static void check_image(const char *name, const struct image *im, double bound)
{
	size_t blen = (size_t)im->w / 4 * (im->h / 4) * 8;
	uint8_t *a = xmalloc(blen), *b = xmalloc(blen), *c = xmalloc(blen + 64);
	int punch, ret;
	char what[96], detail[64];
	const char *mode = getenv("SPEQ_S3TC");
	double e;

	for (punch = 0; punch < 2; punch++) {
		unsigned fmt = punch ? SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT :
				       SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT;

		memset(a, 0x11, blen);
		memset(b, 0x22, blen);
		dxt1_ref_image(im->px, im->comps, im->w, im->h, a, im->w * 2, punch);
		dxt1_ref_image(im->px, im->comps, im->w, im->h, b, im->w * 2, punch);
		snprintf(what, sizeof(what), "%s %s: reference twice, same bytes",
			 punch ? "dxt1a" : "dxt1 ", name);
		check(!memcmp(a, b, blen), what, NULL);
		e = rmse(NULL, im, a, punch);
		snprintf(what, sizeof(what), "%s %s: RMSE within %.1f", punch ? "dxt1a" : "dxt1 ",
			 name, bound);
		snprintf(detail, sizeof(detail), "%.3f", e);
		check(e <= bound, what, detail);

		snprintf(what, sizeof(what), "%s %s: speq_s3tc_compress_v1 = reference",
			 punch ? "dxt1a" : "dxt1 ", name);
		if (mode && strcmp(mode, "ppe") && *mode) {
			check(1, what, "skipped (SPEQ_S3TC set)");
			continue;
		}
		memset(c, 0x33, blen + 64);
		ret = speq_s3tc_compress_v1(fmt, im->comps, im->px, im->w, im->h, c, im->w * 2);
		check(!ret && !memcmp(a, c, blen) && c[blen] == 0x33, what, ret ? strerror(-ret) : NULL);
	}
	snprintf(what, sizeof(what), "dxt1  %s: SPU encoder = reference", name);
	check(1, what, "skipped (no SPU encoder yet)");
	free(a);
	free(b);
	free(c);
}

static int run_check(const struct image *input)
{
	uint8_t px[16][4], out[8], buf[64 * 4 * 4];
	struct image im;
	int punch, i, x, y, ret;
	const char *mode = getenv("SPEQ_S3TC");

	/* golden blocks: endpoints and byte order pinned by hand */
	fill(px, 255, 0, 0, 255);
	dxt1_ref_block(out, (const uint8_t (*)[4])px, 0);
	check(!memcmp(out, "\x00\xf8\x00\xf8\x00\x00\x00\x00", 8), "golden flat red",
	      "00 f8 00 f8 00 00 00 00");
	for (i = 0; i < 16; i++) {	/* left half black, right half white */
		unsigned v = i % 4 < 2 ? 0 : 255;

		px[i][0] = px[i][1] = px[i][2] = v;
		px[i][3] = 255;
	}
	dxt1_ref_block(out, (const uint8_t (*)[4])px, 0);
	check(!memcmp(out, "\x9e\xf7\x61\x08\x05\x05\x05\x05", 8), "golden black|white",
	      "9e f7 61 08 05 05 05 05");

	for (punch = 0; punch < 2; punch++) {
		fill(px, 200, 100, 50, 255);
		check_block("flat opaque", (const uint8_t (*)[4])px, punch, 7, out);
		fill(px, 37, 201, 129, 255);
		check_block("flat opaque 2", (const uint8_t (*)[4])px, punch, 7, out);
		fill(px, 200, 100, 50, 255);
		for (i = 0; i < 16; i += 3)
			px[i][3] = i & 1 ? 127 : 0;
		px[1][3] = 128;
		check_block("flat with transparency", (const uint8_t (*)[4])px, punch, 7, out);
		fill(px, 90, 30, 250, 0);
		check_block("all transparent", (const uint8_t (*)[4])px, punch, 255, out);
		for (i = 0; i < 16; i++) {
			unsigned v = i % 4 < 2 ? 0 : 255;

			px[i][0] = px[i][1] = px[i][2] = v;
			px[i][3] = 255;
		}
		check_block("two-colour", (const uint8_t (*)[4])px, punch, 16, out);
		for (i = 0; i < 16; i++) {	/* channels rising together (the box's diagonal) */
			px[i][0] = i % 4 * 60 + i / 4 * 20;
			px[i][1] = i % 4 * 40 + i / 4 * 30 + 10;
			px[i][2] = i % 4 * 20 + i / 4 * 10 + 100;
			px[i][3] = 255;
		}
		check_block("gradient", (const uint8_t (*)[4])px, punch, 48, out);
		for (i = 0; i < 16; i++)	/* grey ramp: one channel axis, 4 levels */
			px[i][0] = px[i][1] = px[i][2] = i % 4 * 85;
		check_block("grey ramp", (const uint8_t (*)[4])px, punch, 24, out);
	}

	/* API corner cases (dst untouched on refusal) */
	fill(px, 1, 2, 3, 255);
	memset(buf, 0x5a, sizeof(buf));
	ret = speq_s3tc_compress_v1(0x83F2, 4, px, 4, 4, buf, 8);
	check(ret == -ENOTSUP && buf[0] == 0x5a, "DXT3 refused", strerror(-ret));
	ret = speq_s3tc_compress_v1(SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 2, px, 4, 4, buf, 8);
	check(ret == -ENOTSUP && buf[0] == 0x5a, "srccomps 2 refused", strerror(-ret));
	ret = speq_s3tc_compress_v1(SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 4, px, 6, 4, buf, 16);
	check(ret == -ENOTSUP && buf[0] == 0x5a, "6x4 refused", strerror(-ret));
	if (!mode || !*mode || !strcmp(mode, "ppe")) {
		/* 16x8 RGBA into rows 64 bytes apart: the padding stays */
		uint8_t src[16 * 8 * 4];

		for (i = 0; i < (int)sizeof(src); i++)
			src[i] = rnd();
		ret = speq_s3tc_compress_v1(SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 4, src, 16, 8, buf, 64);
		check(!ret && buf[32] == 0x5a && buf[63] == 0x5a &&
		      buf[96] == 0x5a, "dst_stride 64 keeps the row padding", ret ? strerror(-ret) : NULL);
	}

	/* noise and gradient images */
	im.w = 64;
	im.h = 32;
	im.comps = 4;
	im.px = xmalloc(64 * 32 * 4);
	for (i = 0; i < 64 * 32 * 4; i++)
		im.px[i] = rnd();
	check_image("noise 64x32 RGBA", &im, 80);
	for (y = 0; y < 32; y++)
		for (x = 0; x < 64; x++) {
			uint8_t *p = im.px + (y * 64 + x) * 4;

			p[0] = x * 4;
			p[1] = y * 8;
			p[2] = 255 - x * 2;
			p[3] = x < 8 ? 0 : 255;
		}
	check_image("gradient 64x32 RGBA", &im, 6);
	for (i = 0; i < 64 * 32; i++)	/* the same as RGB */
		memmove(im.px + i * 3, im.px + i * 4, 3);
	im.comps = 3;
	check_image("gradient 64x32 RGB", &im, 6);
	free(im.px);
	if (input)
		check_image("input", input, 255);
	printf("%s: %d failure%s\n", failures ? "FAIL" : "ok", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: speq-dxt [--fmt dxt1|dxt1a] [--out f.dds|f.blocks] [--decode f.pam]\n"
		"                [--compare other.blocks|.dds] [--bench N] [--raw WxH] image\n"
		"       speq-dxt --check [image]\n");
	exit(2);
}

int main(int argc, char **argv)
{
	const char *in = NULL, *out = NULL, *decode = NULL, *compare = NULL, *raw = NULL;
	int punch = 0, check_only = 0, i, ret;
	unsigned bench = 0, fmt;
	struct image im;
	uint8_t *blocks;
	size_t blen;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (!strcmp(a, "--check"))
			check_only = 1;
		else if (i + 1 >= argc && a[0] == '-' && a[1] == '-')
			usage();
		else if (!strcmp(a, "--fmt")) {
			a = argv[++i];
			if (!strcmp(a, "dxt1"))
				punch = 0;
			else if (!strcmp(a, "dxt1a"))
				punch = 1;
			else
				usage();
		} else if (!strcmp(a, "--out"))
			out = argv[++i];
		else if (!strcmp(a, "--decode"))
			decode = argv[++i];
		else if (!strcmp(a, "--compare"))
			compare = argv[++i];
		else if (!strcmp(a, "--bench"))
			bench = atoi(argv[++i]);
		else if (!strcmp(a, "--raw"))
			raw = argv[++i];
		else if (a[0] == '-' || in)
			usage();
		else
			in = a;
	}
	if (in && raw)
		read_raw(in, raw, &im);
	else if (in)
		read_image(in, &im);
	if (check_only)
		return run_check(in ? &im : NULL);
	if (!in)
		usage();
	if (im.w % 4 || im.h % 4)
		die("%s: width and height must be multiples of 4", in);

	fmt = punch ? SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
	blen = (size_t)im.w / 4 * (im.h / 4) * 8;
	blocks = xmalloc(blen);
	ret = speq_s3tc_compress_v1(fmt, im.comps, im.px, im.w, im.h, blocks, im.w * 2);
	if (ret)
		die("speq_s3tc_compress_v1: %s", strerror(-ret));
	rmse("ours", &im, blocks, punch);
	if (compare) {
		size_t len;
		uint8_t *other = read_file(compare, &len), *p = other;

		if (len >= 128 && !memcmp(other, "DDS ", 4)) {
			p += 128;
			len -= 128;
		}
		if (len < blen)
			die("%s: shorter than the image's blocks", compare);
		rmse("other", &im, p, punch);
		free(other);
	}
	if (out) {
		uint8_t hdr[128];
		int dds = has_suffix(out, ".dds");

		if (dds)
			dds_header(hdr, im.w, im.h, punch);
		write_file(out, hdr, dds ? 128 : 0, blocks, blen);
	}
	if (decode) {
		uint8_t *rgba = xmalloc((size_t)im.w * im.h * 4), *rgb;
		char hdr[128];
		size_t n = (size_t)im.w * im.h, k;
		int hl;

		decode_image(blocks, im.w, im.h, punch, rgba);
		if (punch) {
			hl = snprintf(hdr, sizeof(hdr), "P7\nWIDTH %u\nHEIGHT %u\nDEPTH 4\nMAXVAL 255\n"
				      "TUPLTYPE RGB_ALPHA\nENDHDR\n", im.w, im.h);
			write_file(decode, hdr, hl, rgba, n * 4);
		} else {
			rgb = xmalloc(n * 3);
			for (k = 0; k < n; k++)
				memcpy(rgb + k * 3, rgba + k * 4, 3);
			hl = snprintf(hdr, sizeof(hdr), "P7\nWIDTH %u\nHEIGHT %u\nDEPTH 3\nMAXVAL 255\n"
				      "TUPLTYPE RGB\nENDHDR\n", im.w, im.h);
			write_file(decode, hdr, hl, rgb, n * 3);
			free(rgb);
		}
		free(rgba);
	}
	if (bench) {
		double t0 = now(), t;
		unsigned k;

		for (k = 0; k < bench; k++)
			if ((ret = speq_s3tc_compress_v1(fmt, im.comps, im.px, im.w, im.h, blocks, im.w * 2)))
				die("speq_s3tc_compress_v1: %s", strerror(-ret));
		t = now() - t0;
		printf("bench  %u x %ux%u in %.3f s: %.2f Mpixel/s, %.0f us per image\n", bench, im.w,
		       im.h, t, (double)bench * im.w * im.h / t / 1e6, t / bench * 1e6);
	}
	free(blocks);
	free(im.px);
	return 0;
}
