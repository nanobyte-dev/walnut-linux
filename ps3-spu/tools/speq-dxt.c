/* speq-dxt: tests and benchmarks libspeq's DXT1 encoder.
 *   speq-dxt [--path spu|ppe|auto] [--fmt dxt1|dxt1a] [--out f.dds|f.blocks] [--decode f.pam]
 *            [--compare other] [--bench N] [--raw WxH] image
 *   speq-dxt [--path P] --check [image]
 *   speq-dxt [--path P] [--fmt F] --bench
 * The image is binary PAM (P7, RGB or RGB_ALPHA) or PPM (P6), 8 bits, or with --raw WxH bare
 * RGBA bytes (gl-dxt's <fmt>.src); w and h multiples of 4.
 * It is encoded through speq_s3tc_compress_v1 (the path Mesa takes) and the RMSE of the decoded
 * result against the original printed. --compare decodes another encoding of the same image
 * (raw blocks or DDS, e.g. Mesa's) and prints its RMSE next to ours.
 * --path: the encoder speq_s3tc_compress_v1 uses (as SPEQ_S3TC, which it overrides).
 * --check: the reference encoder on special blocks (flat, transparent, two-colour, gradient)
 * and its determinism; then the API on the chosen path against the reference, byte for byte:
 * sizes up to 4096 wide, both modes, source offsets (8 mod 16 and others), destinations with
 * row padding and 8 mod 16, a job forced to fault (the image must be redone on the PPE), a fork
 * child, four threads at once, and last three faulting images in a row (then auto uses the
 * PPE, spu declines).
 * On the PS3, --path spu checks the SPEs.
 * --bench (no image): Mpixel/s of speq_s3tc_compress_v1 for 256x256, 512x512 and 1024x1024
 * RGBA (to RGBA DXT1 unless --fmt dxt1) on the PPE and the SPEs (or the --path one), back to back (hot), after the workers parked (30 ms idle),
 * and into a fresh destination each time (its page faults). The SPE load stays a few seconds.
 * The reference encoder and decoder are compiled in (lib/dxt-ref.c), and libspeq's code with
 * them (its internals: the path, the statistics, the fault). */
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "dxt-ref.h"
#include "speq-int.h"

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
		memset(c, 0x33, blen + 64);
		ret = speq_s3tc_compress_v1(fmt, im->comps, im->px, im->w, im->h, c, im->w * 2);
		check(!ret && !memcmp(a, c, blen) && c[blen] == 0x33, what, ret ? strerror(-ret) : NULL);
	}
	free(a);
	free(b);
	free(c);
}

/* --- the paths against the reference --- */

static const char *const path_name[] = { "off", "ppe", "spu", "auto" };

static void *xalign(size_t align, size_t n)
{
	void *p;

	if (posix_memalign(&p, align, n ? n : 1))
		die("out of memory%s", "");
	return p;
}

/* RGBA test content, block by block: noise, flat, gradient, punch-through patterns, random
 * alpha */
static void test_image(uint8_t *px, unsigned w, unsigned h, unsigned seed)
{
	unsigned x, y;

	rng = seed;
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			uint8_t *p = px + ((size_t)y * w + x) * 4;

			switch ((x / 4 * 7 + y / 4 * 13 + seed) % 5) {
			case 0:
				p[0] = rnd(), p[1] = rnd(), p[2] = rnd(), p[3] = rnd();
				break;
			case 1:
				p[0] = seed * 37, p[1] = x / 4 * 11, p[2] = y / 4 * 5, p[3] = 255;
				break;
			case 2:
				p[0] = x * 3, p[1] = y * 5, p[2] = (x + y) * 2, p[3] = 255;
				break;
			case 3:
				p[0] = rnd(), p[1] = x * 4, p[2] = 200, p[3] = (x + y) & 1 ? 0 : 255;
				break;
			default:
				p[0] = 100 + rnd() % 8, p[1] = 50 + rnd() % 4, p[2] = 30, p[3] = 128 + rnd() % 2 * 127;
			}
		}
}

#define GUARD	4096

struct pcase {
	unsigned w, h, comps;
	unsigned off;		/* source pointer mod 128 */
	unsigned doff;		/* destination: page-aligned + doff */
	unsigned stride;	/* passed as dst_stride (< 2w: packed) */
	int punch;
};

/* One image through speq_s3tc_compress_v1 and the reference into identical buffers (guard
 * pages, row padding): all bytes must match. *spu: whether it went to the SPEs. Returns 0 or
 * a description of the mismatch in msg. */
static int run_case(const struct pcase *c, int *spu, char *msg, size_t msglen)
{
	unsigned fmt = c->punch ? SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT :
				  SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
	unsigned stride = c->stride < 2 * c->w ? 2 * c->w : c->stride, rows = c->h / 4;
	size_t slen = (size_t)c->w * c->h * c->comps, dlen = (size_t)(rows - 1) * stride + 2 * c->w;
	size_t blen = GUARD + c->doff + dlen + GUARD, i;
	uint8_t *src = xalign(128, slen + 256), *a = xalign(4096, blen), *b = xalign(4096, blen);
	struct spq_s3tc_stats s0, s1;
	int ret, bad = 0;

	if (c->comps == 4) {
		test_image(src + c->off, c->w, c->h, c->w * 3 + c->h);
	} else {
		uint8_t *t = xmalloc((size_t)c->w * c->h * 4);

		test_image(t, c->w, c->h, c->w + c->h);
		for (i = 0; i < (size_t)c->w * c->h; i++)
			memcpy(src + c->off + 3 * i, t + 4 * i, 3);
		free(t);
	}
	memset(a, 0xa5, blen);
	memset(b, 0xa5, blen);
	dxt1_ref_image(src + c->off, c->comps, c->w, c->h, a + GUARD + c->doff, stride, c->punch);
	spq_s3tc_stats(&s0);
	ret = speq_s3tc_compress_v1(fmt, c->comps, src + c->off, c->w, c->h, b + GUARD + c->doff,
				    c->stride);
	spq_s3tc_stats(&s1);
	*spu = s1.spu > s0.spu;
	if (ret) {
		snprintf(msg, msglen, "%s", strerror(-ret));
		bad = 1;
	} else if (memcmp(a, b, blen)) {
		for (i = 0; i < blen && a[i] == b[i]; i++)
			;
		if (i < GUARD + c->doff || i >= GUARD + c->doff + dlen)
			snprintf(msg, msglen, "wrote outside the image at %+ld",
				 (long)i - GUARD - (long)c->doff);
		else if ((i - GUARD - c->doff) % stride >= 2 * c->w)
			snprintf(msg, msglen, "wrote into the row padding (row %zu)",
				 (i - GUARD - c->doff) / stride);
		else
			snprintf(msg, msglen, "block row %zu block %zu differs",
				 (i - GUARD - c->doff) / stride,
				 (i - GUARD - c->doff) % stride / 8);
		bad = 1;
	}
	free(src);
	free(a);
	free(b);
	return bad;
}

/* Whether the path may (spu: must) take the SPEs for this case */
static int spu_eligible(const struct pcase *c)
{
	return c->comps == 4 && !(c->doff % 8) && !(c->stride % 8);
}

static void case_check(const struct pcase *c, int path, unsigned *nspu, unsigned *nppe)
{
	char msg[128] = "", what[160];
	int spu = 0, bad = run_case(c, &spu, msg, sizeof(msg));

	if (!bad && path == SPQ_S3TC_SPU && spu != spu_eligible(c)) {
		snprintf(msg, sizeof(msg), "went to the %s", spu ? "SPEs" : "PPE");
		bad = 1;
	}
	if (!bad && spu && (path == SPQ_S3TC_PPE || !spu_eligible(c))) {
		snprintf(msg, sizeof(msg), "went to the SPEs");
		bad = 1;
	}
	*(spu ? nspu : nppe) += 1;
	if (!bad)
		return;
	snprintf(what, sizeof(what), "%s %s %ux%u comps %u src+%u dst+%u stride %u (%s)",
		 path_name[path], c->punch ? "dxt1a" : "dxt1 ", c->w, c->h, c->comps, c->off,
		 c->doff, c->stride, spu ? "spu" : "ppe");
	check(0, what, msg);
}

struct thr {
	pthread_t th;
	unsigned idx, bad;
};

static void *thread_main(void *arg)
{
	struct thr *t = arg;
	char msg[128];
	unsigned k;
	int spu;

	for (k = 0; k < 8; k++) {
		struct pcase c = { 64 << (k + t->idx) % 4, 32 << (k * 3 + t->idx) % 3, 4,
				   8 * ((k + t->idx) % 3), 0, 0, (k + t->idx) & 1 };

		if (run_case(&c, &spu, msg, sizeof(msg)))
			t->bad++;
	}
	return NULL;
}

/* The API on the chosen path against the reference */
static void check_paths(int path)
{
	static const unsigned sizes[][2] = {
		{ 4, 4 }, { 8, 4 }, { 4, 8 }, { 12, 8 }, { 20, 12 }, { 60, 4 }, { 64, 64 },
		{ 68, 20 }, { 100, 36 }, { 128, 128 }, { 132, 8 }, { 256, 256 }, { 260, 12 },
		{ 512, 64 }, { 1020, 8 }, { 1024, 16 }, { 1028, 8 }, { 1536, 12 }, { 2048, 8 },
		{ 2052, 4 }, { 3000, 8 }, { 4092, 4 }, { 4096, 16 }, { 16, 1024 }, { 1024, 1024 },
	};
	static const unsigned offs[] = { 0, 1, 4, 13, 12, 3, 9, 15 };
	unsigned i, v, nspu = 0, nppe = 0, f0 = failures;
	struct spq_s3tc_stats s0, s1;
	struct thr th[4];
	struct pcase c;
	char msg[160];
	int punch, spu;

	printf("path %s: the API against the reference%s\n", path_name[path],
	       path == SPQ_S3TC_SPU ? " (every RGBA image with an 8-aligned dst on the SPEs)" : "");
	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
		for (punch = 0; punch < 2; punch++)
			for (v = 0; v < 2; v++) {
				unsigned w = sizes[i][0], pads[5] = { 0, 2 * w, 2 * w + 8,
								      (2 * w + 63) & ~63u, 2 * w + 128 };

				c.w = w;
				c.h = sizes[i][1];
				c.comps = 4;
				c.off = v ? offs[(2 * i + punch) % 8] : 8;	/* 8: ioq3's hunk */
				c.doff = (i + v) % 3 == 2 ? 8 : 0;
				c.stride = pads[(i + punch + v) % 5];
				c.punch = punch;
				case_check(&c, path, &nspu, &nppe);
			}
	/* the PPE's cases: RGB source, a dst or stride not 8-aligned */
	c = (struct pcase){ 128, 64, 3, 8, 0, 0, 0 };
	case_check(&c, path, &nspu, &nppe);
	c = (struct pcase){ 256, 64, 4, 8, 4, 0, 1 };
	case_check(&c, path, &nspu, &nppe);
	c = (struct pcase){ 256, 64, 4, 8, 0, 516, 1 };
	case_check(&c, path, &nspu, &nppe);
	snprintf(msg, sizeof(msg), "%u images on the SPEs, %u on the PPE", nspu, nppe);
	check(failures == f0, "path matrix: speq_s3tc_compress_v1 = reference", msg);

	/* a job that faults: the image is redone on the PPE, and the next one is on the SPEs */
	if (path != SPQ_S3TC_PPE && spq_s3tc_queue()) {
		c = (struct pcase){ 512, 256, 4, 8, 0, 0, 1 };
		spq_s3tc_stats(&s0);
		spq_s3tc_fault = 2;
		msg[0] = 0;
		v = run_case(&c, &spu, msg, sizeof(msg));
		spq_s3tc_stats(&s1);
		if (msg[0])
			strcat(msg, "; ");
		snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), "redone %lu, spu %lu, jobs %lu",
			 s1.redone - s0.redone, s1.spu - s0.spu, s1.jobs - s0.jobs);
		check(!v && s1.redone == s0.redone + 1 && s1.spu == s0.spu && s1.jobs > s0.jobs + 1,
		      "a faulting job: the image redone on the PPE, = reference", msg);
		spq_s3tc_stats(&s0);
		v = run_case(&c, &spu, msg, sizeof(msg));
		spq_s3tc_stats(&s1);
		check(!v && spu && s1.redone == s0.redone, "the next image on the SPEs again",
		      v ? msg : NULL);
	} else {
		check(1, "a faulting job", path == SPQ_S3TC_PPE ? "skipped (ppe)" : "skipped (no queue)");
	}

	/* a fork child has no workers: it encodes on the PPE, still right */
	if (path != SPQ_S3TC_PPE && spq_s3tc_queue()) {
		pid_t pid;
		int st = 0;

		c = (struct pcase){ 256, 128, 4, 8, 0, 0, 1 };
		fflush(stdout);
		pid = fork();
		if (!pid) {
			spq_s3tc_stats(&s0);
			v = run_case(&c, &spu, msg, sizeof(msg));
			spq_s3tc_stats(&s1);
			_exit(v ? 1 : spu || s1.ppe != s0.ppe + 1 ? 2 : 0);
		}
		if (pid < 0 || waitpid(pid, &st, 0) != pid)
			st = -1;
		check(WIFEXITED(st) && !WEXITSTATUS(st), "a fork child: on the PPE, = reference",
		      WIFEXITED(st) && WEXITSTATUS(st) == 2 ? "not on the PPE" : NULL);
	}

	/* four threads at once, each its own images */
	spq_s3tc_stats(&s0);
	for (i = 0; i < 4; i++) {
		th[i].idx = i;
		th[i].bad = 0;
		if (pthread_create(&th[i].th, NULL, thread_main, &th[i]))
			die("pthread_create%s", "");
	}
	for (v = 0, i = 0; i < 4; i++) {
		pthread_join(th[i].th, NULL);
		v += th[i].bad;
	}
	spq_s3tc_stats(&s1);
	snprintf(msg, sizeof(msg), "%u bad, %lu of 32 on the SPEs", v, s1.spu - s0.spu);
	check(!v && s1.redone == s0.redone, "4 threads x 8 images = reference", msg);

	/* last (it ends the SPU path for this process): 3 failed images in a row, then the PPE */
	if (path != SPQ_S3TC_PPE && spq_s3tc_queue()) {
		c = (struct pcase){ 256, 128, 4, 8, 0, 0, 0 };
		spq_s3tc_stats(&s0);
		for (v = 0, i = 0; i < 3; i++) {
			spq_s3tc_fault = 1;
			v += run_case(&c, &spu, msg, sizeof(msg));
		}
		spq_s3tc_fault = 0;
		/* then auto takes the PPE; spu declines (as without a queue) and Mesa encodes */
		i = run_case(&c, &spu, msg, sizeof(msg));
		spq_s3tc_stats(&s1);
		if (path == SPQ_S3TC_SPU) {
			snprintf(msg, sizeof(msg), "redone %lu, then %s", s1.redone - s0.redone,
				 i ? "declined" : "encoded");
			check(!v && s1.redone == s0.redone + 3 && i && !spu,
			      "3 failed images in a row: declined from then on", msg);
		} else {
			snprintf(msg, sizeof(msg), "redone %lu, then %s", s1.redone - s0.redone,
				 spu ? "spu" : "ppe");
			check(!v && !i && s1.redone == s0.redone + 3 && !spu && s1.ppe == s0.ppe + 4,
			      "3 failed images in a row: the PPE from then on, = reference", msg);
		}
	}
}

/* --- --bench without an image --- */

static double median(double *v, unsigned n)
{
	unsigned i, j;
	double t;

	for (i = 1; i < n; i++)
		for (j = i; j > 0 && v[j - 1] > v[j]; j--)
			t = v[j], v[j] = v[j - 1], v[j - 1] = t;
	return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

static void bench_line(const char *path, const char *how, unsigned w, unsigned h, int punch,
		       double us, unsigned n, double jobs)
{
	printf("bench %s %4ux%-4u %-3s %-6s %8.1f Mpixel/s %8.0f us/image", punch ? "dxt1a" : "dxt1 ",
	       w, h, path, how, w * h / us, us);
	if (jobs > 0)
		printf("  %.1f jobs", jobs);
	printf("  (%u image%s)\n", n, n == 1 ? "" : "s");
}

static int encode(int punch, const uint8_t *src, unsigned w, unsigned h, uint8_t *dst)
{
	return speq_s3tc_compress_v1(punch ? SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT :
				     SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 4, src, w, h, dst, 2 * w);
}

/* hot: back to back for ~0.25 s; parked (SPEs): 30 ms idle before each; fresh: a new mmap
 * destination each time (its first touch is in the timing) */
static void bench_size(int path, unsigned w, unsigned h, int punch)
{
	size_t blen = (size_t)w * h / 2;
	uint8_t *src = xalign(128, (size_t)w * h * 4 + 8) + 8, *dst = xalign(4096, blen);
	const char *pn = path_name[path];
	struct spq_s3tc_stats s0, s1;
	double t0, t, v[7];
	unsigned n, k;
	int ret;

	spq_s3tc_mode(path);
	test_image(src, w, h, 7);
	if ((ret = encode(punch, src, w, h, dst)))	/* warm: pages, the queue */
		die("speq_s3tc_compress_v1: %s", strerror(-ret));
	spq_s3tc_stats(&s0);
	t0 = now();
	n = 0;
	do {
		if ((ret = encode(punch, src, w, h, dst)))
			die("speq_s3tc_compress_v1: %s", strerror(-ret));
		n++;
		t = now() - t0;
	} while ((t < 0.25 || n < 2) && n < 2000);
	spq_s3tc_stats(&s1);
	bench_line(pn, "hot", w, h, punch, t / n * 1e6, n,
		   path == SPQ_S3TC_SPU ? (double)(s1.jobs - s0.jobs) / n : 0);
	if (path == SPQ_S3TC_SPU) {
		for (k = 0; k < 5; k++) {
			usleep(30000);	/* > 10 ms: every worker parks */
			t0 = now();
			encode(punch, src, w, h, dst);
			v[k] = (now() - t0) * 1e6;
		}
		bench_line(pn, "parked", w, h, punch, median(v, 5), 5, 0);
	}
	n = path == SPQ_S3TC_PPE && w * h > 300000 ? 3 : 7;
	for (k = 0; k < n; k++) {
		uint8_t *d = mmap(NULL, blen, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		if (d == MAP_FAILED)
			die("mmap%s", "");
		t0 = now();
		encode(punch, src, w, h, d);
		v[k] = (now() - t0) * 1e6;
		munmap(d, blen);
	}
	bench_line(pn, "fresh", w, h, punch, median(v, n), n, 0);
	free(src - 8);
	free(dst);
}

static int run_bench(int path, int punch)
{
	static const unsigned sizes[] = { 256, 512, 1024 };
	int paths[2], np = 0, p, ret;
	unsigned i;
	uint8_t *src, *dst;
	double t0;

	if (path < 0 || path == SPQ_S3TC_AUTO) {
		paths[np++] = SPQ_S3TC_PPE;
		paths[np++] = SPQ_S3TC_SPU;
	} else {
		paths[np++] = path;
	}
	for (p = 0; p < np; p++) {
		if (paths[p] == SPQ_S3TC_SPU && !spq_s3tc_queue()) {
			/* the first SPU call opens the queue: worker per SPE */
			spq_s3tc_mode(SPQ_S3TC_SPU);
			src = xalign(128, 256 * 256 * 4);
			dst = xalign(4096, 256 * 256 / 2);
			test_image(src, 256, 256, 3);
			t0 = now();
			ret = encode(punch, src, 256, 256, dst);
			t0 = now() - t0;
			free(src);
			free(dst);
			if (ret) {
				printf("bench spu: no queue (%s): is dxt.spu installed, spufs mounted?\n",
				       strerror(-ret));
				continue;
			}
			printf("bench spu: first call %.0f us (opens the queue: %u workers)\n", t0 * 1e6,
			       spq_s3tc_queue()->nworkers);
		}
		for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
			bench_size(paths[p], sizes[i], sizes[i], punch);
	}
	return 0;
}

static int run_check(const struct image *input, int path)
{
	uint8_t px[16][4], out[8], buf[64 * 4 * 4];
	struct image im;
	int punch, i, x, y, ret;

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
	{	/* 16x8 RGBA into rows 64 bytes apart: the padding stays */
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
	check_paths(path);
	printf("%s: %d failure%s\n", failures ? "FAIL" : "ok", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: speq-dxt [--path spu|ppe|auto] [--fmt dxt1|dxt1a] [--out f.dds|f.blocks]\n"
		"                [--decode f.pam] [--compare other.blocks|.dds] [--bench N] [--raw WxH] image\n"
		"       speq-dxt [--path spu|ppe|auto] --check [image]\n"
		"       speq-dxt [--path spu|ppe|auto] [--fmt dxt1|dxt1a] --bench\n");
	exit(2);
}

int main(int argc, char **argv)
{
	const char *in = NULL, *out = NULL, *decode = NULL, *compare = NULL, *raw = NULL;
	int punch = -1, check_only = 0, bench_all = 0, path = -1, i, ret;
	unsigned bench = 0, fmt;
	struct image im;
	uint8_t *blocks;
	size_t blen;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (!strcmp(a, "--check"))
			check_only = 1;
		else if (!strcmp(a, "--bench") &&
			 (i + 1 >= argc || !argv[i + 1][0] || strspn(argv[i + 1], "0123456789") !=
			  strlen(argv[i + 1])))
			bench_all = 1;	/* no count: the size sweep */
		else if (i + 1 >= argc && a[0] == '-' && a[1] == '-')
			usage();
		else if (!strcmp(a, "--path")) {
			a = argv[++i];
			if (!strcmp(a, "spu"))
				path = SPQ_S3TC_SPU;
			else if (!strcmp(a, "ppe"))
				path = SPQ_S3TC_PPE;
			else if (!strcmp(a, "auto"))
				path = SPQ_S3TC_AUTO;
			else
				usage();
		}
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
	if (path >= 0)
		spq_s3tc_mode(path);
	if (check_only)
		return run_check(in ? &im : NULL, spq_s3tc_mode(-1));
	if (bench_all && !in)
		return run_bench(path, punch != 0);	/* default RGBA DXT1, as OpenArena */
	punch = punch > 0;
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
