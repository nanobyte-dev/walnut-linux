/* dxt-test: the SPU DXT1 encoder (dxt.c) against the reference's blocks, then its speed.
 * Standalone SPU program (no libspeq runtime), built by dxt-test.mk:
 *   spurun dxt-test.spu result
 * Every test image (dxt-test-data.h, made on the host by dxt-gen with lib/dxt-ref.c) is
 * encoded in both modes at every source offset 0..15, with the destination at 0 and 8 mod 16,
 * whole and one block row per call, the source pointer aligned and not; the blocks must match
 * the reference's byte for byte and the bytes around the output stay untouched. Then a 256-wide
 * image (the stone crop tiled), 64 block rows, is timed with the decrementer per mode.
 * The summary goes to the local-store string `result` (SPU printf isn't served by spurun); the
 * exit code is 0 when everything matched. Built for the host (dxt-emu.h or DXT_SCALAR) it also
 * prints the summary and fuzzes single blocks against dxt1_ref_block. */
#include <stdint.h>
#include <string.h>
#include "dxt.h"

struct test_img {
	const char *name;
	unsigned w, h;
	const unsigned char *px, *ref[2];
};
#include "dxt-test-data.h"

#ifdef __SPU__
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#else
#include <stdio.h>
#include <time.h>
#include "dxt-ref.h"
#endif

#define DEC_HZ		79800000u	/* PS3 timebase: the SPU decrementer's rate */
#define CYC_PER_TICK	40u		/* SPU clock 3.192 GHz / 79.8 MHz */
#define TIME_W		256
#define TIME_ROWS	64		/* block rows timed, in calls of 4 */

char result[320];

static uint8_t work[64 * 64 * 4 + 64] __attribute__((aligned(128)));
static uint8_t out[64 * 64 / 2 + 64] __attribute__((aligned(128)));

static unsigned nblocks, nbad, nguard;
static char first[160];

static char *put(char *p, const char *s)
{
	char *end = result + sizeof(result) - 1;

	while (*s && p < end)
		*p++ = *s++;
	*p = 0;
	return p;
}

static char *putu(char *p, unsigned long long v)
{
	char tmp[24];
	int n = 0;

	do
		tmp[n++] = '0' + v % 10;
	while (v /= 10);
	tmp[n] = 0;
	while (n--) {	/* reverse into place */
		char c[2] = { tmp[n], 0 };

		p = put(p, c);
	}
	return p;
}

/* v / 10 with one decimal */
static char *putd1(char *p, unsigned long long v10)
{
	char c[3] = { '.', 0, 0 };

	p = putu(p, v10 / 10);
	c[1] = '0' + v10 % 10;
	return put(p, c);
}

static char *puthex(char *p, const uint8_t *b, unsigned n)
{
	static const char hex[] = "0123456789abcdef";
	char c[3] = { 0, 0, 0 };
	unsigned i;

	for (i = 0; i < n; i++) {
		c[0] = hex[b[i] >> 4];
		c[1] = hex[b[i] & 15];
		p = put(p, c);
	}
	return p;
}

/* failure detail, built in `first` with the same helpers (they write into result) */
static void note_first(const struct test_img *im, unsigned mode, unsigned off, unsigned dstoff,
		       unsigned blk, const uint8_t *got, const uint8_t *want)
{
	char *p = result;

	if (first[0])
		return;
	p = put(p, im->name);
	p = put(p, " m");
	p = putu(p, mode);
	p = put(p, " off");
	p = putu(p, off);
	p = put(p, " dst+");
	p = putu(p, dstoff);
	p = put(p, " blk");
	p = putu(p, blk);
	if (got) {
		p = put(p, " got ");
		p = puthex(p, got, 8);
		p = put(p, " want ");
		p = puthex(p, want, 8);
	} else
		p = put(p, " wrote outside its output");
	memcpy(first, result, sizeof(first) - 1);
	result[0] = 0;
}

static void run(const struct test_img *im, unsigned mode, unsigned off)
{
	unsigned len = im->w * im->h * 4, blen = im->w / 4 * (im->h / 4) * 8, i, by;
	unsigned dstoff = off & 1 ? 8 : 0;
	uint8_t *d = out + 16 + dstoff;

	memset(work, 0xa5, sizeof(work));
	memcpy(work + off, im->px, len);
	memset(out, 0xee, sizeof(out));
	if (off == 5 || off == 10)	/* one block row per call */
		for (by = 0; by < im->h / 4; by++)
			dxt1_encode_rows(work + by * 16 * im->w, off, im->w, 1, d + by * 2 * im->w, mode);
	else if (off % 4 == 3)		/* the shift in the pointer */
		dxt1_encode_rows(work + off, 0, im->w, im->h / 4, d, mode);
	else
		dxt1_encode_rows(work, off, im->w, im->h / 4, d, mode);
	for (i = 0; i < blen; i += 8) {
		nblocks++;
		if (memcmp(d + i, im->ref[mode] + i, 8)) {
			nbad++;
			note_first(im, mode, off, dstoff, i / 8, d + i, im->ref[mode] + i);
		}
	}
	for (i = 0; i < sizeof(out); i++)
		if ((out + i < d || out + i >= d + blen) && out[i] != 0xee) {
			nguard++;
			note_first(im, mode, off, dstoff, 0, NULL, NULL);
			break;
		}
}

static unsigned long long ticks_now(void)
{
#ifdef __SPU__
	return spu_read_decrementer();	/* counts down */
#else
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);	/* as decrementer ticks, counting down */
	return ~0ull - (unsigned long long)((ts.tv_sec * 1000000000ull + ts.tv_nsec) * 798 / 10000);
#endif
}

/* decrementer ticks for TIME_ROWS block rows of the tiled stone crop, the best of 3 */
static unsigned long long time_mode(unsigned mode)
{
	unsigned long long best = ~0ull, t0, t;
	unsigned rep, k;

	for (rep = 0; rep < 3; rep++) {
		t0 = ticks_now();
		for (k = 0; k < TIME_ROWS / 4; k++)
			dxt1_encode_rows(work, 8, TIME_W, 4, out, mode);
		t = (t0 - ticks_now()) & 0xffffffffull;
		if (t < best)
			best = t;
	}
	return best;
}

#ifndef __SPU__
static unsigned fuzz(void)
{
	static uint8_t blk[16 * 4 + 32] __attribute__((aligned(16))), o[16] __attribute__((aligned(16)));
	uint8_t px[16][4], b[8];
	uint32_t s = 777;
	unsigned n, i, k, bad = 0, kind, base[4], range;

	for (n = 0; n < 2000000; n++) {
		kind = n % 5;
		for (k = 0; k < 4; k++) {
			s = s * 1664525 + 1013904223;
			base[k] = s >> 24;
		}
		s = s * 1664525 + 1013904223;
		range = (s >> 24) % 40 + 1;
		for (i = 0; i < 16; i++)
			for (k = 0; k < 4; k++) {
				s = s * 1664525 + 1013904223;
				switch (kind) {
				case 0:	/* anything */
					px[i][k] = s >> 24;
					break;
				case 1:	/* small range around a base: equal endpoints, ties */
					px[i][k] = base[k] + (s >> 24) % range > 255 ? 255 : base[k] + (s >> 24) % range;
					break;
				case 2:	/* two colours */
					px[i][k] = s >> 31 ? base[k] : base[(k + 1) & 3];
					break;
				case 3:	/* few levels: exact ties */
					px[i][k] = (s >> 30) * 85;
					break;
				default:	/* alpha near the cut */
					px[i][k] = k == 3 ? 120 + (s >> 24) % 16 : s >> 24;
				}
			}
		for (i = 0; i < 16; i++)
			memcpy(blk + 3 + i % 4 * 4 + i / 4 * 16, px[i], 4);
		for (k = 0; k < 2; k++) {
			dxt1_ref_block(b, (const uint8_t (*)[4])px, k);
			dxt1_encode_rows(blk, 3, 4, 1, o, k);
			if (memcmp(o, b, 8) && bad++ < 5)
				printf("fuzz %u mode %u: got %02x%02x%02x%02x%02x%02x%02x%02x want %02x%02x%02x%02x%02x%02x%02x%02x\n",
				       n, k, o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7],
				       b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
		}
	}
	printf("fuzz: %u of 4000000 blocks differ\n", bad);
	return bad;
}
#endif

int main(void)
{
	unsigned nimg = sizeof(test_imgs) / sizeof(test_imgs[0]), i, mode, off, x, y;
	const struct test_img *tim = &test_imgs[TEST_TIMING_IMG];
	unsigned long long ticks[2];
	int sumb_ok = 1;
	char *p;

#ifdef __SPU__
	{	/* the operand order dxt.c assumes: spu_sumb(a, b) puts b's sums in the even halfwords */
		vec_uchar16 a = { 1, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4 };
		vec_uchar16 b = { 10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40 };
		vec_ushort8 s = spu_sumb(a, b);

		sumb_ok = spu_extract(s, 0) == 100 && spu_extract(s, 1) == 10;
	}
#endif
	for (mode = 0; mode < 2; mode++)
		for (off = 0; off < 16; off++)
			for (i = 0; i < nimg; i++)
				run(&test_imgs[i], mode, off);

	/* timing source: the stone crop tiled to 256 x 16, at offset 8 like ioquake3's */
	for (y = 0; y < 16; y++)
		for (x = 0; x < TIME_W; x += tim->w)
			memcpy(work + 8 + (y * TIME_W + x) * 4, tim->px + (y % tim->h) * tim->w * 4, tim->w * 4);
#ifdef __SPU__
	spu_write_decrementer(0x7fffffff);
#endif
	ticks[0] = time_mode(0);
	ticks[1] = time_mode(1);

	p = put(result, nbad || nguard || !sumb_ok ? "FAIL " : "PASS ");
	p = putu(p, nbad);
	p = put(p, " of ");
	p = putu(p, nblocks);
	p = put(p, " blocks differ");
	if (nguard) {
		p = put(p, ", ");
		p = putu(p, nguard);
		p = put(p, " overruns");
	}
	if (!sumb_ok)
		p = put(p, ", spu_sumb operands swapped");
	for (mode = 0; mode < 2; mode++) {
		unsigned long long blocks = TIME_W / 4 * TIME_ROWS, pixels = blocks * 16;

		p = put(p, mode ? "; rgba " : "; rgb ");
		if (!ticks[mode]) {
			p = put(p, "decrementer not counting");
			continue;
		}
		p = putd1(p, ticks[mode] * CYC_PER_TICK * 10 / blocks);
		p = put(p, " cyc/blk ");
		p = putd1(p, pixels * (DEC_HZ / 100000) / ticks[mode]);
		p = put(p, " Mpx/s");
	}
	p = put(p, " (dec 79.8 MHz, 40 cyc/tick)");
	if (first[0]) {
		p = put(p, "; first: ");
		p = put(p, first);
	}
#ifndef __SPU__
	printf("%s\n", result);
	if (fuzz())
		return 1;
#endif
	return nbad || nguard || !sumb_ok ? 1 : 0;
}
