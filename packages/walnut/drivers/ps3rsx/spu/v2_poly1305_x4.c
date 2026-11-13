// SPDX-License-Identifier: GPL-2.0
/* v2_poly1305_x4.c: Poly1305 four blocks at a time, one block per 32-bit lane. Same result as
 * v2_poly1305.c (RFC 8439 2.5), checked against it and RFC 8439's vectors in ../test/vseal-test.c;
 * built for the PC (SSE through GCC's vector extensions), the PPE and the SPU.
 *
 * Four-way Horner. Lane j takes blocks j, j+4, j+8, ... of the message, so with n = 4G blocks
 *     H_j = (((m_j r^4 + m_{4+j}) r^4 + ...) r^4 + m_{4(G-1)+j}) r^(4-j),
 * i.e. every group multiplies by r^4 except the last, which multiplies lane j by r^(4-j); then
 * sum_j H_j = sum_k m_k r^(n-k), Poly1305's polynomial. A message whose block count isn't a multiple
 * of 4 gets z = (4 - n%4) % 4 blocks of value 0 *in front*: a zero block with no 2^128 bit adds 0 to
 * h, and h starts at 0, so they change nothing. r^2, r^3, r^4 are computed per key (per page) with
 * the same lane multiply.
 *
 * Limbs as in v2_poly1305.c: ten 13-bit limbs, each a vector (lane j = block j's limb). The SPU
 * multiplies 16 x 16 bits only; mpya (multiply and add) is SIGNED 16 x 16 + 32, so both factors must
 * be below 2^15; that rules out 5 r (up to 40,955), so the reduction's factor 5 is applied after the
 * column sums instead:
 *     L_k = sum_{i+j=k} a_i R_j,   T_k = sum_{i+j=k+10} a_i R_j,   d_k = L_k + 5 T_k = L_k + (T_k << 2) + T_k.
 * Every product is one mpya; nothing but adds, ands and shifts besides. The bounds (each checked at
 * run time per lane in the V2_CHECKED build; A_MAX, R_MAX, H_POST below):
 *   - a_i <= A_MAX = 8196 (the multiplicand after two parallel carries, below), R_j <= R_MAX = 8196
 *     (r's limbs are <= 8191; r^2..r^4 are normalized the same way): both < 2^15, so mpya's signed
 *     product equals the unsigned one, and each product < 2^26.1;
 *   - L_k + 5 T_k <= (k+1 + 5 (9-k)) * A_MAX * R_MAX <= 46 * 8196 * 8196 = 3,090,023,136 < 2^32 (k = 0
 *     is the worst; T_k alone <= 9 * 8196^2 < 2^30, so T_k << 2 < 2^32); the adds never wrap, so
 *     mpya's modular add gives the true sum;
 *   - one parallel carry after the multiply (c_k = d_k >> 13 for all k at once, d_k &= MASK,
 *     d_k += c_{k-1}, d_0 += 5 c_9): every limb <= 8191 + 5 * (2^32 >> 13) < 2^22 = H_POST (strict;
 *     the exact worst is 418,191 < 2^19);
 *   - the next group adds a block's limbs (<= 8191, limb 9 < 2^12): <= 2^22 + 8191; two more parallel
 *     carries: the first leaves c <= 2^9.1, so limbs <= 8191 + 5 * 2^9.1 < 11,000; the second leaves
 *     c <= 1, so limbs <= 8191 + 5 = 8196 = A_MAX.
 * The lanes are summed limb by limb at the end (< 4 * 2^22 = 2^24 per limb, no wrap) and finished
 * by the scalar code (v2_poly1305_finish: two full carries, the conditional subtraction of p, + s).
 */
#include <string.h>

#include "v2_crypto.h"
#ifdef __SPU__
#include <spu_intrinsics.h>
#endif

#define MASK	0x1fffu
#define A_MAX	8196u
#define R_MAX	8196u
#define H_POST	(1u << 22)

typedef uint32_t v4u __attribute__((vector_size(16)));
typedef uint8_t v16u8 __attribute__((vector_size(16)));
typedef int32_t v4i __attribute__((vector_size(16)));

#ifdef V2_CHECKED
#include <stdio.h>
#include <stdlib.h>
#define VBOUND(v, max) do { int _l; for (_l = 0; _l < 4; _l++) { v2_bound_checks++; \
	if ((v)[_l] > (max)) { fprintf(stderr, "v2_poly1305_x4: lane %d %s = %u > %s (%s:%d)\n", _l, \
	#v, (unsigned)(v)[_l], #max, __FILE__, __LINE__); abort(); } } } while (0)
/* lane-wise add that must not wrap */
static inline v4u vadd(v4u a, v4u b)
{
	v4u r;
	int l;

	for (l = 0; l < 4; l++) {
		v2_bound_checks++;
		if (__builtin_add_overflow(a[l], b[l], &r[l])) {
			fprintf(stderr, "v2_poly1305_x4: lane add wraps (%u + %u)\n", a[l], b[l]);
			abort();
		}
	}
	return r;
}
#else
#define VBOUND(v, max) do { } while (0)
#define vadd(a, b) ((a) + (b))
#endif

/* c + a * b per lane, 16 x 16 signed (the SPU's mpya); a, b < 2^15 */
static inline v4u mpya(v4u a, v4u b, v4u c)
{
#ifdef __SPU__
	return (v4u)spu_madd((vec_short8)a, (vec_short8)b, (vec_int4)c);
#else
	v4u r;
	int l;

	for (l = 0; l < 4; l++) {
#ifdef V2_CHECKED
		v2_bound_checks++;
		if (a[l] >= 0x8000u || b[l] >= 0x8000u) {
			fprintf(stderr, "v2_poly1305_x4: mpya factor over 2^15 (%u, %u)\n", a[l], b[l]);
			abort();
		}
#endif
		r[l] = (uint32_t)((int32_t)(int16_t)a[l] * (int16_t)b[l]) + c[l];
#ifdef V2_CHECKED
		if (r[l] < c[l]) {
			fprintf(stderr, "v2_poly1305_x4: mpya sum wraps\n");
			abort();
		}
#endif
	}
	return r;
#endif
}

/* d = a * R per lane, mod 2^130 - 5, column sums not yet carried */
static inline void vmul(v4u d[10], const v4u a[10], const v4u R[10])
{
	int k, j;

	for (k = 0; k < 10; k++) {
		VBOUND(a[k], A_MAX);
		VBOUND(R[k], R_MAX);
	}
#pragma GCC unroll 10
	for (k = 0; k < 10; k++) {
		v4u L = { 0, 0, 0, 0 }, T = { 0, 0, 0, 0 };

#pragma GCC unroll 10
		for (j = 0; j < 10; j++) {
			if (j <= k)
				L = mpya(a[k - j], R[j], L);
			else
				T = mpya(a[k - j + 10], R[j], T);
		}
		VBOUND(T, (1u << 30) - 1);	/* T << 2 must not wrap */
		d[k] = vadd(L, vadd(T << 2, T));
	}
}

/* one carry step on all limbs at once */
static inline void vcarry(v4u x[10])
{
	const v4u mask = { MASK, MASK, MASK, MASK };
	v4u c[10];
	int k;

#pragma GCC unroll 10
	for (k = 0; k < 10; k++) {
		c[k] = x[k] >> 13;
		x[k] &= mask;
	}
	x[0] = vadd(x[0], vadd(c[9] << 2, c[9]));
#pragma GCC unroll 9
	for (k = 1; k < 10; k++)
		x[k] = vadd(x[k], c[k - 1]);
}

static inline v4u ld_le(const uint8_t *p)	/* p 16-byte aligned */
{
	v4u v = *(const v4u *)p;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	const v16u8 sw = { 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12 };

	v = (v4u)__builtin_shuffle((v16u8)v, sw);
#endif
	return v;
}

/* four blocks (lane j = block j) -> their ten limbs, with the 2^128 bit where hib's lane says */
static inline void vlimbs(const uint8_t *b0, const uint8_t *b1, const uint8_t *b2, const uint8_t *b3,
			  v4u hib, v4u m[10])
{
	const v4i lo = { 0, 4, 1, 5 }, hi = { 2, 6, 3, 7 }, l2 = { 0, 1, 4, 5 }, h2 = { 2, 3, 6, 7 };
	const v4u mask = { MASK, MASK, MASK, MASK };
	v4u q0 = ld_le(b0), q1 = ld_le(b1), q2 = ld_le(b2), q3 = ld_le(b3), w[4];
	v4u t0 = __builtin_shuffle(q0, q1, lo), t1 = __builtin_shuffle(q2, q3, lo);
	v4u t2 = __builtin_shuffle(q0, q1, hi), t3 = __builtin_shuffle(q2, q3, hi);

	w[0] = __builtin_shuffle(t0, t1, l2);	/* word 0 of each block */
	w[1] = __builtin_shuffle(t0, t1, h2);
	w[2] = __builtin_shuffle(t2, t3, l2);
	w[3] = __builtin_shuffle(t2, t3, h2);
	m[0] = w[0] & mask;				/* bits 0-12 */
	m[1] = (w[0] >> 13) & mask;			/* 13-25 */
	m[2] = ((w[0] >> 26) | (w[1] << 6)) & mask;	/* 26-38 */
	m[3] = (w[1] >> 7) & mask;			/* 39-51 */
	m[4] = ((w[1] >> 20) | (w[2] << 12)) & mask;	/* 52-64 */
	m[5] = (w[2] >> 1) & mask;			/* 65-77 */
	m[6] = (w[2] >> 14) & mask;			/* 78-90 */
	m[7] = ((w[2] >> 27) | (w[3] << 5)) & mask;	/* 91-103 */
	m[8] = (w[3] >> 8) & mask;			/* 104-116 */
	m[9] = (w[3] >> 21) | (hib << 11);		/* 117-127, bit 128 */
}

/* h = (h + m) * R, carried */
static inline void vgroup(v4u h[10], const v4u m[10], const v4u R[10])
{
	v4u a[10], d[10];
	int k;

#pragma GCC unroll 10
	for (k = 0; k < 10; k++) {
		VBOUND(h[k], H_POST - 1);
		a[k] = vadd(h[k], m[k]);
	}
	vcarry(a);
	vcarry(a);
	vmul(d, a, R);
	vcarry(d);
#pragma GCC unroll 10
	for (k = 0; k < 10; k++)
		h[k] = d[k];
}

static inline uint32_t ld32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* r's limbs and r^2, r^3, r^4 (normalized: <= R_MAX); R4 every lane r^4, RL lanes r^4 r^3 r^2 r */
static void powers(const uint8_t key[16], v4u R4[10], v4u RL[10])
{
	uint32_t w[4], r[10];
	v4u R1[10], R2[10], A[10], B[10], d[10];
	int k;

	w[0] = ld32(key) & 0x0fffffff;
	w[1] = ld32(key + 4) & 0x0ffffffc;
	w[2] = ld32(key + 8) & 0x0ffffffc;
	w[3] = ld32(key + 12) & 0x0ffffffc;
	v2_poly1305_limbs(w, r);
	for (k = 0; k < 10; k++)
		R1[k] = (v4u){ r[k], r[k], r[k], r[k] };
	vmul(d, R1, R1);			/* r^2 in every lane */
	vcarry(d);
	vcarry(d);
	vcarry(d);
	for (k = 0; k < 10; k++) {
		R2[k] = d[k];
		A[k] = (v4u){ d[k][0], d[k][0], d[k][0], d[k][0] };	/* r^2 */
		B[k] = (v4u){ r[k], d[k][0], r[k], d[k][0] };		/* r, r^2 */
	}
	vmul(d, A, B);				/* lanes 0, 2: r^3; 1, 3: r^4 */
	vcarry(d);
	vcarry(d);
	vcarry(d);
	for (k = 0; k < 10; k++) {
		VBOUND(d[k], R_MAX);
		VBOUND(R2[k], R_MAX);
		R4[k] = (v4u){ d[k][1], d[k][1], d[k][1], d[k][1] };
		RL[k] = (v4u){ d[k][1], d[k][0], R2[k][0], r[k] };
	}
}

/* The polynomial over one 16-byte aligned message; the last block, if partial, padded (RFC 8439: a 1
 * byte, no 2^128 bit). Blocks are taken straight from m, z zero blocks in front. */
static void poly_x4(const uint8_t key[32], const uint8_t *m, size_t len, uint8_t tag[16])
{
	static const uint8_t zero[16] __attribute__((aligned(16)));
	uint8_t last[16] __attribute__((aligned(16)));
	const uint8_t *blk[4];
	v4u R4[10], RL[10], h[10], mv[10];
	uint32_t hs[10], pad[4];
	size_t nb = (len + 15) / 16, total, g, k;
	unsigned int z;

	for (k = 0; k < 4; k++)
		pad[k] = ld32(key + 16 + 4 * k);
	if (!nb) {
		memset(hs, 0, sizeof(hs));
		v2_poly1305_finish(hs, pad, tag);
		return;
	}
	if (len % 16) {
		memset(last, 0, sizeof(last));
		memcpy(last, m + len - len % 16, len % 16);
		last[len % 16] = 1;
	}
	powers(key, R4, RL);
	for (k = 0; k < 10; k++)
		h[k] = (v4u){ 0, 0, 0, 0 };
	z = (4 - nb % 4) % 4;
	total = nb + z;
	for (g = 0; g < total / 4; g++) {
		v4u hib = { 1, 1, 1, 1 };
		int j;

		for (j = 0; j < 4; j++) {
			size_t b = 4 * g + j;

			if (b < z) {
				blk[j] = zero;
				hib[j] = 0;
			} else if (b - z == nb - 1 && len % 16) {
				blk[j] = last;
				hib[j] = 0;
			} else {
				blk[j] = m + 16 * (b - z);
			}
		}
		vlimbs(blk[0], blk[1], blk[2], blk[3], hib, mv);
		vgroup(h, mv, g + 1 == total / 4 ? RL : R4);
	}
	for (k = 0; k < 10; k++) {
		VBOUND(h[k], H_POST - 1);
		hs[k] = h[k][0] + h[k][1] + h[k][2] + h[k][3];
	}
	v2_poly1305_finish(hs, pad, tag);
}

void v2_poly1305_x4(const uint8_t key[32], const uint8_t *m, size_t len, uint8_t tag[16])
{
	/* the segment walker reads 16-byte blocks from m: copy an unaligned message once */
	if ((uintptr_t)m & 15) {
		uint8_t buf[512] __attribute__((aligned(16)));
		struct v2_poly1305 st;

		if (len <= sizeof(buf)) {
			memcpy(buf, m, len);
			poly_x4(key, buf, len, tag);
			return;
		}
		v2_poly1305_init(&st, key);	/* (tests only: big unaligned input) */
		v2_poly1305_update(&st, m, len);
		v2_poly1305_final(&st, tag);
		return;
	}
	poly_x4(key, m, len, tag);
}

/* V2's page MAC input, RFC 8439 2.8 with a 16-byte AD: AD || ct (4096) || le64(16) || le64(4096),
 * 258 blocks. Two zero blocks in front make 260 = 65 groups: group 0 = zero, zero, AD, ct block 0;
 * groups 1-63 = ct blocks 4g-3 .. 4g (straight from ct, no per-block bookkeeping); group 64 = ct
 * blocks 253-255 and the lengths, multiplied by r^4 r^3 r^2 r. ct 16-byte aligned. */
void v2_poly1305_x4_page(const uint8_t key[32], const uint8_t ad[16], const uint8_t *ct, uint8_t tag[16])
{
	static const uint8_t zero[16] __attribute__((aligned(16)));
	uint8_t lens[16] __attribute__((aligned(16))) = { 16, 0, 0, 0, 0, 0, 0, 0, 0, 0x10, 0, 0, 0, 0, 0, 0 };
	uint8_t adc[16] __attribute__((aligned(16)));
	const v4u h01 = { 0, 0, 1, 1 }, h1 = { 1, 1, 1, 1 };
	v4u R4[10], RL[10], h[10], m[10];
	uint32_t hs[10], pad[4];
	const uint8_t *p;
	int g, k;

	if ((uintptr_t)ct & 15) {	/* never in V2 (constant LS offsets); correct, slower */
		struct v2_poly1305 st;

		v2_poly1305_init(&st, key);
		v2_poly1305_update(&st, ad, 16);
		v2_poly1305_update(&st, ct, 4096);
		v2_poly1305_update(&st, lens, 16);
		v2_poly1305_final(&st, tag);
		return;
	}
	memcpy(adc, ad, 16);
	for (k = 0; k < 4; k++)
		pad[k] = ld32(key + 16 + 4 * k);
	powers(key, R4, RL);
	for (k = 0; k < 10; k++)
		h[k] = (v4u){ 0, 0, 0, 0 };
	vlimbs(zero, zero, adc, ct, h01, m);
	vgroup(h, m, R4);
	for (g = 1, p = ct + 16; g < 64; g++, p += 64) {
		vlimbs(p, p + 16, p + 32, p + 48, h1, m);
		vgroup(h, m, R4);
	}
	vlimbs(p, p + 16, p + 32, lens, h1, m);	/* p = ct + 4048: blocks 253-255 */
	vgroup(h, m, RL);
	for (k = 0; k < 10; k++) {
		VBOUND(h[k], H_POST - 1);
		hs[k] = h[k][0] + h[k][1] + h[k][2] + h[k][3];
	}
	v2_poly1305_finish(hs, pad, tag);
}
