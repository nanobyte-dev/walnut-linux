// SPDX-License-Identifier: GPL-2.0
/* v2_poly1305.c: Poly1305 (RFC 8439 2.5) in ten 13-bit limbs, 32-bit arithmetic only.
 *
 * Why 13-bit limbs: the SPU multiplies only 16 x 16 -> 32 bits (mpyu, unsigned; mpya is signed and
 * can't take 5 * r's limbs), so every factor must fit 16 bits unsigned and every column sum 32
 * bits. The scheme and its bounds (checked at run time with V2_CHECKED):
 *
 *   p = 2^130 - 5. A number x is ten limbs x_0..x_9, x = sum x_i 2^(13 i), MASK = 2^13 - 1.
 *   r (clamped, RFC 8439: r < 2^124): r_i <= MASK, r_9 < 2^7. s5_i = 5 r_i <= 40955 < 2^16.
 *   Invariant I (between blocks): every h_i <= 2^13 + 2^10 = 9216 (H_MAX).
 *   A message block (16 bytes, plus the 2^128 bit) gives limbs m_i <= MASK, m_9 < 2^12.
 *
 *   Step 1, add: h_i + m_i <= 9216 + 8191 = 17407. Too big for the product step (17407 * 376786
 *   overflows 32 bits), so
 *   Step 2, a full carry ("carry_full"): limb by limb c = a_i >> 13, a_i &= MASK, a_{i+1} += c;
 *   the carry out of limb 9 re-enters limb 0 times 5 (2^130 = 5 mod p); one more carry from limb 0
 *   into limb 1. Afterwards a_i <= MASK for every i except a_1 <= MASK + 2^4 (see the code):
 *   all <= H_MAX, the input bound of step 3.
 *   Step 3, multiply: d_k = sum_{i+j=k} a_i r_j + sum_{i+j=k+10} a_i s5_j. Column k has k+1
 *   products with r_j <= 8191 and 9-k with s5_j <= 40955; the worst is k = 0:
 *       d_0 <= 9216 * (8191 + 9 * 40955) = 9216 * 376786 = 3,472,459,776 < 2^32 = 4,294,967,296.
 *   Every factor < 2^16 (a_i <= 9216, r_j <= 8191, s5_j <= 40955): one mpyu each, 100 a block.
 *   Step 4, carry the product: c = d_k >> 13 (< 2^19.1), d_{k+1} += c (< 3.48e9 + 2^20: fits);
 *   c9 = d_9 >> 13, h_0 = (d_0 & MASK) + 5 c9 < 2^13 + 5 * 2^19.1 < 2^22.2; then c = h_0 >> 13
 *   (< 2^9.2) into h_1: h_1 <= MASK + 2^9.2 + 1 < H_MAX. Invariant I holds again.
 *
 * Final: carry_full twice (h_i <= MASK for all i, h < 2^130 + small), h + 5 computed with carries;
 * if that reaches 2^130 (bit 13 of limb 9) the reduced value is h + 5 - 2^130, else h; then the
 * low 128 bits plus s (mod 2^128, 32-bit adds with carry) are the tag.
 */
#include <string.h>

#include "v2_crypto.h"

#define MASK	0x1fffu
#define H_MAX	(0x2000u + 0x400u)

#ifdef V2_CHECKED
#include <stdio.h>
#include <stdlib.h>
unsigned long v2_bound_checks;
#define BOUND(cond) do { v2_bound_checks++; if (!(cond)) { \
	fprintf(stderr, "v2_poly1305: bound violated: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
	abort(); } } while (0)
/* u32 accumulate that must not wrap */
static inline uint32_t acc(uint32_t a, uint32_t b)
{
	uint32_t r;

	BOUND(!__builtin_add_overflow(a, b, &r));
	return r;
}
#else
#define BOUND(cond) do { } while (0)
#define acc(a, b) ((a) + (b))
#endif

/* the SPU's mpyu: 16 x 16 -> 32 bits unsigned */
static inline uint32_t mul16(uint32_t a, uint32_t b)
{
	BOUND(a < 0x10000u && b < 0x10000u);
	return (uint32_t)(uint16_t)a * (uint16_t)b;
}

static inline uint32_t ld32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* 128 bits as four little-endian words -> ten 13-bit limbs (limb 9 gets bits 117..127) */
void v2_poly1305_limbs(const uint32_t w[4], uint32_t l[10])
{
	int i;

	for (i = 0; i < 10; i++) {
		unsigned int bit = 13 * i, wd = bit / 32, sh = bit % 32;
		uint32_t v = w[wd] >> sh;

		if (sh > 19 && wd + 1 < 4)
			v |= w[wd + 1] << (32 - sh);
		l[i] = v & MASK;
	}
}

static void carry_full(uint32_t a[10])
{
	uint32_t c;
	int i;

	for (i = 0; i < 9; i++) {
		c = a[i] >> 13;
		a[i] &= MASK;
		a[i + 1] = acc(a[i + 1], c);
	}
	c = a[9] >> 13;
	a[9] &= MASK;
	a[0] = acc(a[0], mul16(c, 5));	/* c <= 2 from block() (a_9 <= 17409), < 2^12 from finish (lane sums < 2^24) */
	c = a[0] >> 13;
	a[0] &= MASK;
	a[1] = acc(a[1], c);		/* c <= 1: a_0 <= 8191 + 5 * 2 before the carry */
	for (i = 0; i < 10; i++)
		BOUND(a[i] <= (i == 1 ? MASK + 16 : MASK));
}

static void block(struct v2_poly1305 *st, const uint8_t m[16], uint32_t hibit)
{
	uint32_t w[4], ml[10], d[10], c;
	int i, j, k;

	for (i = 0; i < 4; i++)
		w[i] = ld32(m + 4 * i);
	v2_poly1305_limbs(w, ml);
	ml[9] |= hibit << 11;		/* bit 128 = limb 9, bit 11 */
	for (i = 0; i < 10; i++) {
		BOUND(st->h[i] <= H_MAX);
		st->h[i] = acc(st->h[i], ml[i]);
	}
	carry_full(st->h);
	for (k = 0; k < 10; k++) {
		uint32_t s = 0;

		for (j = 0; j < 10; j++) {
			i = k - j;
			if (i >= 0)
				s = acc(s, mul16(st->h[i], st->r[j]));
			else
				s = acc(s, mul16(st->h[i + 10], st->s5[j]));
		}
		d[k] = s;
	}
	for (k = 0; k < 9; k++) {
		c = d[k] >> 13;
		d[k] &= MASK;
		d[k + 1] = acc(d[k + 1], c);
	}
	c = d[9] >> 13;
	d[9] &= MASK;
	BOUND(c < (1u << 20));
	/* 5c < 2^23: not a 16-bit product, so as shift and add (5c = 4c + c) */
	d[0] = acc(d[0], acc(c << 2, c));
	c = d[0] >> 13;
	d[0] &= MASK;
	d[1] = acc(d[1], c);
	for (i = 0; i < 10; i++) {
		st->h[i] = d[i];
		BOUND(st->h[i] <= H_MAX);
	}
}

void v2_poly1305_init(struct v2_poly1305 *st, const uint8_t key[32])
{
	uint32_t w[4];
	int i;

	w[0] = ld32(key) & 0x0fffffff;
	w[1] = ld32(key + 4) & 0x0ffffffc;
	w[2] = ld32(key + 8) & 0x0ffffffc;
	w[3] = ld32(key + 12) & 0x0ffffffc;
	v2_poly1305_limbs(w, st->r);
	for (i = 0; i < 10; i++) {
		BOUND(st->r[i] <= MASK && (i < 9 || st->r[i] < 128));
		st->s5[i] = mul16(st->r[i], 5);
		BOUND(st->s5[i] <= 40955);
		st->h[i] = 0;
	}
	for (i = 0; i < 4; i++)
		st->pad[i] = ld32(key + 16 + 4 * i);
	st->buflen = 0;
}

void v2_poly1305_update(struct v2_poly1305 *st, const uint8_t *m, size_t len)
{
	while (len) {
		if (st->buflen == 0 && len >= 16) {
			block(st, m, 1);
			m += 16;
			len -= 16;
			continue;
		}
		st->buf[st->buflen++] = *m++;
		len--;
		if (st->buflen == 16) {
			block(st, st->buf, 1);
			st->buflen = 0;
		}
	}
}

/* h (ten limbs, each < 2^24, no other bound) -> the tag: two full carries, reduce mod p, + s */
void v2_poly1305_finish(uint32_t h[10], const uint32_t pad[4], uint8_t tag[V2_TAG_BYTES])
{
	uint32_t g[10], w[4], c;
	int i;

	for (i = 0; i < 10; i++)
		BOUND(h[i] <= (1u << 24));	/* the four-lane sums are < 2^24 (four lanes < 2^22) */
	carry_full(h);
	carry_full(h);
	/* normalize without the wrap (h < 2^130 + 2^14 here): every limb but 9 <= MASK after it;
	 * limb 9 > MASK means h >= 2^130, and then h + 5 >= 2^130 too, so g below is taken */
	for (i = 0; i < 9; i++) {
		c = h[i] >> 13;
		h[i] &= MASK;
		h[i + 1] += c;
	}
	/* g = h + 5 */
	c = 5;
	for (i = 0; i < 10; i++) {
		g[i] = acc(h[i], c);
		c = g[i] >> 13;
		g[i] &= MASK;
	}
	/* c is the bit 130 of h + 5: h >= p, take g = h - p */
	{	/* without a branch: c is 0 or 1 */
		uint32_t take = 0u - c;

		for (i = 0; i < 10; i++)
			h[i] = (g[i] & take) | (h[i] & ~take);
	}
	for (i = 0; i < 10; i++)
		BOUND(h[i] <= MASK);
	/* ten limbs -> 128 bits, little-endian words (bits 128, 129 dropped) */
	memset(w, 0, sizeof(w));
	for (i = 0; i < 10; i++) {
		unsigned int bit = 13 * i, wd = bit / 32, sh = bit % 32;

		w[wd] |= h[i] << sh;
		if (sh > 19 && wd + 1 < 4)
			w[wd + 1] |= h[i] >> (32 - sh);
	}
	/* + s mod 2^128 */
	c = 0;
	for (i = 0; i < 4; i++) {
		uint32_t a = w[i], t = a + pad[i], c2 = t < a;

		w[i] = t + c;
		c = c2 | (w[i] < t);
	}
	for (i = 0; i < 4; i++) {
		tag[4 * i] = w[i];
		tag[4 * i + 1] = w[i] >> 8;
		tag[4 * i + 2] = w[i] >> 16;
		tag[4 * i + 3] = w[i] >> 24;
	}
}

void v2_poly1305_final(struct v2_poly1305 *st, uint8_t tag[V2_TAG_BYTES])
{
	if (st->buflen) {	/* RFC 8439: the last partial block gets a 1 byte, no 2^128 bit */
		st->buf[st->buflen++] = 1;
		while (st->buflen < 16)
			st->buf[st->buflen++] = 0;
		block(st, st->buf, 0);
	}
	v2_poly1305_finish(st->h, st->pad, tag);
	memset(st, 0, sizeof(*st));
}

void v2_poly1305(const uint8_t key[32], const uint8_t *m, size_t len, uint8_t tag[V2_TAG_BYTES])
{
	struct v2_poly1305 st;

	v2_poly1305_init(&st, key);
	v2_poly1305_update(&st, m, len);
	v2_poly1305_final(&st, tag);
}
