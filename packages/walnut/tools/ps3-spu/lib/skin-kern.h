/* The skinning kernel, shared by the SPU program (spu/skin-op.c, on vertices in local store)
 * and libspeq's PPE path (lib/skin.c, on the game's memory): one vertex at a time, in 4-wide
 * float vectors (GCC's generic vectors: the SPU's, AltiVec on the PPE, SSE on the host; the
 * same code everywhere, so the host tests run what the SPU runs). Fields are read and written
 * through aligned quadwords and byte shuffles (the SPU has no unaligned access; on the PPE it
 * avoids the load-hit-store of a copy through memory), never touching a quadword that holds
 * none of the field's bytes, so nothing is read past a page the field isn't on.
 *
 * Dual quaternions follow Sauerbraten's geom.h (dualquat::transform, quat::rotate, quat::mul,
 * blendbones' accumulate and normalize, animmodel's fixqtangent, squat): same formulas and
 * mostly the same order, so the results are Sauerbraten's within float rounding (the SPU
 * rounds toward zero; fused multiply-adds where the compiler finds them). */
#ifndef SKIN_KERN_H
#define SKIN_KERN_H

#include <stdint.h>
#include "speq.h"
#include "skin-job.h"

typedef float sv4f __attribute__((vector_size(16), may_alias));
typedef int32_t sv4i __attribute__((vector_size(16), may_alias));
typedef int16_t sv8s __attribute__((vector_size(16), may_alias));
typedef uint8_t sv16u8 __attribute__((vector_size(16), may_alias));
typedef uint32_t sk_u32a __attribute__((may_alias));
typedef uint16_t sk_u16a __attribute__((may_alias));

#ifdef __SPU__
#include <spu_intrinsics.h>
#endif

#define SK_SH(v, a, b, c, d)	__builtin_shuffle((v), (sv4i){ a, b, c, d })
#define SK_SH2(v, w, a, b, c, d) __builtin_shuffle((v), (w), (sv4i){ a, b, c, d })

/* Byte k (memory order) of a 32-bit word loaded from memory */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SK_BYTE(u, k)	(((u) >> (24 - 8 * (k))) & 0xff)
#else
#define SK_BYTE(u, k)	(((u) >> (8 * (k))) & 0xff)
#endif

/* Shuffle masks. Load from a field at byte sh of its quadword: bytes sh.. of (q0, q1).
 * Store n bytes at sh: q0's bytes [sh, sh + n) and q1's [0, sh + n - 16) from the value. */
#define SK_I16(F, a, b)	{ F(a, b, 0), F(a, b, 1), F(a, b, 2), F(a, b, 3), F(a, b, 4), F(a, b, 5), \
			  F(a, b, 6), F(a, b, 7), F(a, b, 8), F(a, b, 9), F(a, b, 10), F(a, b, 11), \
			  F(a, b, 12), F(a, b, 13), F(a, b, 14), F(a, b, 15) }
#define SK_LDB(sh, n, i)	((sh) + (i))
#define SK_ST0(sh, n, i)	((i) >= (sh) && (i) < (sh) + (n) ? 16 + (i) - (sh) : (i))
#define SK_ST1(sh, n, i)	((i) + 16 < (sh) + (n) ? 32 + (i) - (sh) : (i))
#define SK_STN(sh, n)		{ SK_I16(SK_ST0, sh, n), SK_I16(SK_ST1, sh, n) }
#define SK_STS(sh)		{ SK_STN(sh, 4), SK_STN(sh, 8), SK_STN(sh, 12), SK_STN(sh, 16) }

static const sv16u8 sk_ldm[4] __attribute__((aligned(16))) = {
	SK_I16(SK_LDB, 0, 0), SK_I16(SK_LDB, 4, 0), SK_I16(SK_LDB, 8, 0), SK_I16(SK_LDB, 12, 0)
};
static const sv16u8 sk_stm[4][4][2] __attribute__((aligned(16))) = {
	SK_STS(0), SK_STS(4), SK_STS(8), SK_STS(12)
};

/* The n bytes (12 or 16) at p (4-aligned) as a vector */
static inline sv4f sk_ld(const void *p, unsigned n)
{
	uintptr_t a = (uintptr_t)p;
	const sv16u8 *q0 = (const sv16u8 *)(a & ~(uintptr_t)15);
	const sv16u8 *q1 = (const sv16u8 *)((a + n - 1) & ~(uintptr_t)15);

	return (sv4f)__builtin_shuffle(*q0, *q1, sk_ldm[(a & 15) >> 2]);
}

/* The first n bytes (4, 8, 12, 16) of v at p (4-aligned); the quadwords' other bytes kept. q1
 * first: when the field fits in q0, q1 is q0 written back unchanged, then q0 with the field. */
static inline void sk_st(void *p, sv4f v, unsigned n)
{
	uintptr_t a = (uintptr_t)p;
	sv16u8 *q0 = (sv16u8 *)(a & ~(uintptr_t)15);
	sv16u8 *q1 = (sv16u8 *)((a + n - 1) & ~(uintptr_t)15);
	const sv16u8 *m = sk_stm[(a & 15) >> 2][(n >> 2) - 1];
	sv16u8 o0 = *q0, o1 = *q1;

	*q1 = __builtin_shuffle(o1, (sv16u8)v, m[1]);
	*q0 = __builtin_shuffle(o0, (sv16u8)v, m[0]);
}

/* a > b lane by lane (all ones: true), and a select (b where m is set). GCC's SPU back end turns
 * generic vector comparisons into per-lane scalar code: the intrinsics there. */
#ifdef __SPU__
static inline sv4i sk_gt(sv4f a, sv4f b)
{
	return (sv4i)spu_cmpgt((vec_float4)a, (vec_float4)b);
}

static inline sv4f sk_sel(sv4f a, sv4f b, sv4i m)
{
	return (sv4f)spu_sel((vec_float4)a, (vec_float4)b, (vec_uint4)m);
}
#else
static inline sv4i sk_gt(sv4f a, sv4f b)
{
	return (sv4i)(a > b);
}

static inline sv4f sk_sel(sv4f a, sv4f b, sv4i m)
{
	return (sv4f)(((sv4i)a & ~m) | ((sv4i)b & m));
}
#endif

/* vec::cross: x = a.y*b.z - a.z*b.y, ... (w: 0) */
static inline sv4f sk_cross(sv4f a, sv4f b)
{
	return SK_SH(a, 1, 2, 0, 3) * SK_SH(b, 2, 0, 1, 3) - SK_SH(a, 2, 0, 1, 3) * SK_SH(b, 1, 2, 0, 3);
}

/* dualquat::transform(vec) */
static inline sv4f sk_dq_pos(sv4f r, sv4f d, sv4f v)
{
	const sv4f two = { 2, 2, 2, 2 };
	sv4f rw = SK_SH(r, 3, 3, 3, 3), dw = SK_SH(d, 3, 3, 3, 3);
	sv4f c1 = sk_cross(r, v) + v * rw + d;
	sv4f c2 = sk_cross(r, c1) + d * rw - r * dw;

	return c2 * two + v;
}

/* quat::rotate(vec): dualquat::transformnormal */
static inline sv4f sk_rot(sv4f r, sv4f n)
{
	const sv4f two = { 2, 2, 2, 2 };
	sv4f t = sk_cross(r, n) + n * SK_SH(r, 3, 3, 3, 3);

	return sk_cross(r, t) * two + n;
}

/* quat::mul(p, o): dualquat::transform(quat) */
static inline sv4f sk_qmul(sv4f p, sv4f o)
{
	const sv4i negw = { 0, 0, 0, (int32_t)0x80000000 };
	sv4f a = SK_SH(p, 3, 3, 3, 3) * o;
	sv4f b = SK_SH(p, 0, 1, 2, 0) * SK_SH(o, 3, 3, 3, 0);	/* px*ow py*ow pz*ow, px*ox */
	sv4f c = SK_SH(p, 1, 2, 0, 1) * SK_SH(o, 2, 0, 1, 1);	/* py*oz pz*ox px*oy, py*oy */
	sv4f e = SK_SH(p, 2, 0, 1, 2) * SK_SH(o, 1, 2, 0, 2);	/* pz*oy px*oz py*ox, pz*oz */

	b = (sv4f)((sv4i)b ^ negw);
	c = (sv4f)((sv4i)c ^ negw);
	return a + b + c - e;
}

/* animmodel's fixqtangent(q, bt) with bt = t's w, without branches: q's sign flipped when
 * (bt < 0) == (w >= 0) (w = 0 goes negative for bt < 0, stays for bt >= 0), then for bt < 0 a
 * w above the bias set to the bias (and xyz scaled by sqrt(1 - bias^2), which is 1 in float).
 * The comparisons run on whole vectors and their w lane is spread (a comparison of a spread
 * scalar becomes per-lane scalar code on the SPU). */
static inline sv4f sk_fixq(sv4f q, sv4f t)
{
	const float bias = -1.5f / 65535, biasscale = __builtin_sqrtf(1 - bias * bias);
	const sv4f zero = { 0, 0, 0, 0 }, bv = { bias, bias, bias, bias };
	const sv4i sign = { (int32_t)0x80000000, (int32_t)0x80000000, (int32_t)0x80000000,
			    (int32_t)0x80000000 };
	sv4i neg = SK_SH(sk_gt(zero, t), 3, 3, 3, 3), wlt = SK_SH(sk_gt(zero, q), 3, 3, 3, 3);
	sv4f qb;

	q = (sv4f)((sv4i)q ^ ((neg ^ wlt) & sign));
	qb = q * (sv4f){ biasscale, biasscale, biasscale, 0 } + (sv4f){ 0, 0, 0, bias };
	return sk_sel(q, qb, neg & SK_SH(sk_gt(q, bv), 3, 3, 3, 3));
}

/* squat::convert: 4 shorts in the first 8 bytes */
static inline sv4f sk_s16(sv4f q)
{
	const sv4f k = { 32767.5f, 32767.5f, 32767.5f, 32767.5f }, h = { 0.5f, 0.5f, 0.5f, 0.5f };
	sv8s s = (sv8s)__builtin_convertvector(q * k - h, sv4i);

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return (sv4f)__builtin_shuffle(s, (sv8s){ 1, 3, 5, 7, 1, 3, 5, 7 });
#else
	return (sv4f)__builtin_shuffle(s, (sv8s){ 0, 2, 4, 6, 0, 2, 4, 6 });
#endif
}

/* (r0 . v, r1 . v, r2 . v, -) */
static inline sv4f sk_mat(sv4f r0, sv4f r1, sv4f r2, sv4f v)
{
	sv4f p0 = r0 * v, p1 = r1 * v, p2 = r2 * v;
	sv4f s0 = SK_SH2(p0, p1, 0, 4, 1, 5) + SK_SH2(p0, p1, 2, 6, 3, 7);
	sv4f s1 = SK_SH(p2, 0, 0, 1, 1) + SK_SH(p2, 2, 2, 3, 3);

	return SK_SH2(s0, s1, 0, 1, 4, 5) + SK_SH2(s0, s1, 2, 3, 6, 7);
}

static inline float sk_dot4(sv4f a, sv4f b)
{
	sv4f p = a * b;

	return p[0] + p[1] + p[2] + p[3];
}

/* The palette: entries [0, n0) at p0, [n0, n) at p1; E quadwords each (DQ 2, MAT 3) */
struct sk_pal {
	const sv4f *p0, *p1;
	uint32_t n0, n;
};

/* The palette as sk_loop reads it: entry i at (i < n0 ? b0 : b1) + i * E quadwords */
struct sk_palx {
	uintptr_t b0, b1;
	uint32_t n0, n;
};

static inline struct sk_palx sk_palx(const struct sk_pal *pl, const unsigned E)
{
	struct sk_palx x = { (uintptr_t)pl->p0, (uintptr_t)pl->p1 - (uintptr_t)pl->n0 * E * 16,
			     pl->n0, pl->n };

	return x;
}

static inline const sv4f *sk_pent(const struct sk_palx *x, uint32_t i, const unsigned E)
{
	return (const sv4f *)((i < x->n0 ? x->b0 : x->b1) + (uintptr_t)i * E * 16);
}

/* n vertices from s0/s1 (the streams at the first vertex) into o (its output, the template's
 * bytes already there). Formats as constants when inlined (sk_run). 0, or -5 for an index past
 * the palette. */
static inline __attribute__((always_inline)) int
sk_loop(const struct skin_desc *d, const struct sk_pal *pl, const uint8_t *s0, const uint8_t *s1,
	uint8_t *o, uint32_t n, const int pf, const int inf, const int hasn, const int tin,
	const int tout)
{
	const unsigned E = pf == SPEQ_SKIN_DQ ? 2 : 3;
	const uint8_t *in[2] = { s0, s1 };
	const uint8_t *pp = in[d->f_stream[SKIN_F_POS] & 1] + d->f_off[SKIN_F_POS];
	const uint8_t *pi = in[d->f_stream[SKIN_F_INDEX] & 1] + d->f_off[SKIN_F_INDEX];
	const uint8_t *pn = hasn ? in[d->f_stream[SKIN_F_NORM] & 1] + d->f_off[SKIN_F_NORM] : 0;
	const uint8_t *pt = tin ? in[d->f_stream[SKIN_F_TAN] & 1] + d->f_off[SKIN_F_TAN] : 0;
	const uint8_t *pw = inf == SPEQ_SKIN_W4 ?
			    in[d->f_stream[SKIN_F_WEIGHTS] & 1] + d->f_off[SKIN_F_WEIGHTS] : 0;
	const uint32_t sp = d->src_stride[d->f_stream[SKIN_F_POS] & 1];
	const uint32_t si = d->src_stride[d->f_stream[SKIN_F_INDEX] & 1];
	const uint32_t sn = d->src_stride[d->f_stream[SKIN_F_NORM] & 1];
	const uint32_t stt = d->src_stride[d->f_stream[SKIN_F_TAN] & 1];
	const uint32_t sw = d->src_stride[d->f_stream[SKIN_F_WEIGHTS] & 1];
	const uint32_t ds = d->dst_stride, dpos = d->d_off[SKIN_D_POS];
	const uint32_t dnorm = d->d_off[SKIN_D_NORM], dtan = d->d_off[SKIN_D_TAN];
	const sv4f one = { 1, 1, 1, 1 }, zero = { 0, 0, 0, 0 };
	const struct sk_palx px = sk_palx(pl, E), *x = &px;
	uint32_t i, bad = 0;

	/* no early exit (an index past the palette reads entry 0 and fails the call at the end):
	 * the loop unrolls, and two vertices' dependency chains interleave */
#pragma GCC unroll 2
	for (i = 0; i < n; i++, pp += sp, pi += si, o += ds) {
		sv4f r = zero, dq = zero, m0 = zero, m1 = zero, m2 = zero, v, t;

		if (inf == SPEQ_SKIN_W4) {
			uint32_t bb = *(const sk_u32a *)pi, ww = *(const sk_u32a *)pw, k;

			for (k = 0; k < 4; k++)
				if (SK_BYTE(bb, k) >= x->n) {
					bad = 1;
					bb = 0;
				}
			if (pf == SPEQ_SKIN_DQ) {
				const sv4f *e = sk_pent(x, SK_BYTE(bb, 0), 2);
				float w = SK_BYTE(ww, 0) * (1 / 255.0f), inv;

				r = e[0] * (sv4f){ w, w, w, w };
				dq = e[1] * (sv4f){ w, w, w, w };
				for (k = 1; k < 4; k++) {
					w = SK_BYTE(ww, k) * (1 / 255.0f);
					if (w == 0)
						continue;
					e = sk_pent(x, SK_BYTE(bb, k), 2);
					if (sk_dot4(r, e[0]) < 0)
						w = -w;
					r += e[0] * (sv4f){ w, w, w, w };
					dq += e[1] * (sv4f){ w, w, w, w };
				}
				inv = 1 / __builtin_sqrtf(sk_dot4(r, r));
				r *= (sv4f){ inv, inv, inv, inv };
				dq *= (sv4f){ inv, inv, inv, inv };
			} else {
				for (k = 0; k < 4; k++) {
					const sv4f *e = sk_pent(x, SK_BYTE(bb, k), 3);
					float w = SK_BYTE(ww, k) * (1 / 255.0f);
					sv4f wv = { w, w, w, w };

					m0 += e[0] * wv;
					m1 += e[1] * wv;
					m2 += e[2] * wv;
				}
			}
		} else {
			uint32_t idx = inf == SPEQ_SKIN_IDX_S32 ? *(const sk_u32a *)pi :
				       inf == SPEQ_SKIN_IDX_U16 ? *(const sk_u16a *)pi : *pi;
			const sv4f *e;

			bad |= idx >= x->n;
			idx = idx < x->n ? idx : 0;
			e = sk_pent(x, idx, E);
			if (pf == SPEQ_SKIN_DQ) {
				r = e[0];
				dq = e[1];
			} else {
				m0 = e[0];
				m1 = e[1];
				m2 = e[2];
			}
		}
		v = sk_ld(pp, 12);
		if (pf == SPEQ_SKIN_DQ)
			sk_st(o + dpos, sk_dq_pos(r, dq, v), 12);
		else
			sk_st(o + dpos, sk_mat(m0, m1, m2, SK_SH2(v, one, 0, 1, 2, 4)), 12);
		if (hasn) {
			v = sk_ld(pn, 12);
			if (pf == SPEQ_SKIN_DQ)
				sk_st(o + dnorm, sk_rot(r, v), 12);
			else
				sk_st(o + dnorm, sk_mat(m0, m1, m2, SK_SH2(v, zero, 0, 1, 2, 4)), 12);
		}
		if (tin) {
			t = sk_ld(pt, 16);
			if (tin == SPEQ_SKIN_TAN_QUAT)
				v = sk_fixq(sk_qmul(r, t), t);
			else if (pf == SPEQ_SKIN_DQ)
				v = SK_SH2(sk_rot(r, t), t, 0, 1, 2, 7);
			else
				v = SK_SH2(sk_mat(m0, m1, m2, SK_SH2(t, zero, 0, 1, 2, 4)), t, 0, 1, 2, 7);
			if (tout == SPEQ_SKIN_OUT_S16)
				sk_st(o + dtan, sk_s16(v), 8);
			else
				sk_st(o + dtan, v, 16);
		}
		if (hasn)
			pn += sn;
		if (tin)
			pt += stt;
		if (inf == SPEQ_SKIN_W4)
			pw += sw;
	}
	return bad ? -5 : 0;
}

/* Whether a descriptor is one the kernel runs: formats, offsets, strides, palette size (the
 * SPU program checks it again, libspeq first). 0 or -EINVAL (22) / -ENOTSUP (95). */
static inline int sk_check(const struct skin_desc *d)
{
	unsigned f, need[SKIN_NF] = { 12, 12, 16, 4, 4 }, dneed[SKIN_ND] = { 12, 12, 16 };
	uint32_t npal = d->npal[0] + d->npal[1];

	if (d->palette_fmt > SPEQ_SKIN_MAT || d->influence > SPEQ_SKIN_W4 ||
	    d->tan_in > SPEQ_SKIN_TAN_QUAT || d->tan_out > SPEQ_SKIN_OUT_S16)
		return -95;
	if (d->tan_in == SPEQ_SKIN_TAN_QUAT && d->palette_fmt != SPEQ_SKIN_DQ)
		return -95;
	if (d->npal[0] > SKIN_DQ_MAX || d->npal[1] > SKIN_DQ_MAX || !npal ||
	    npal > (d->palette_fmt == SPEQ_SKIN_DQ ? SKIN_DQ_MAX : SKIN_MAT_MAX))
		return -95;
	if ((d->pal[0] & 15) || (d->pal[1] & 15) || (d->tmpl && (d->tmpl & 15) != (d->dst & 15)) ||
	    (d->dst & 3) || (d->npal[0] && !d->pal[0]) || (d->npal[1] && !d->pal[1]))
		return -22;
	if (d->dst_stride & 3 || !d->dst_stride || d->dst_stride > SKIN_STRIDE_MAX)
		return -22;
	if (d->tan_out == SPEQ_SKIN_OUT_S16)
		dneed[SKIN_D_TAN] = 8;
	if (d->influence == SPEQ_SKIN_IDX_U16)
		need[SKIN_F_INDEX] = 2;
	else if (d->influence == SPEQ_SKIN_IDX_U8)
		need[SKIN_F_INDEX] = 1;
	for (f = 0; f < SKIN_NF; f++) {
		int used = f == SKIN_F_POS || f == SKIN_F_INDEX ||
			   (f == SKIN_F_NORM && d->d_off[SKIN_D_NORM] != SPEQ_SKIN_NONE) ||
			   (f == SKIN_F_TAN && d->tan_in) ||
			   (f == SKIN_F_WEIGHTS && d->influence == SPEQ_SKIN_W4);
		uint32_t s = d->f_stream[f] & 1;

		if (!used)
			continue;
		if (d->f_off[f] == SPEQ_SKIN_NONE || d->f_stream[f] > 1 || !d->src[s] ||
		    (d->src_stride[s] & 3) || !d->src_stride[s] || d->src_stride[s] > SKIN_STRIDE_MAX ||
		    (d->f_off[f] & 3) || d->f_off[f] + need[f] > d->src_stride[s] || (d->src[s] & 3))
			return -22;
	}
	for (f = 0; f < SKIN_ND; f++) {
		int used = f == SKIN_D_POS || (f == SKIN_D_NORM && d->f_off[SKIN_F_NORM] != SPEQ_SKIN_NONE) ||
			   (f == SKIN_D_TAN && d->tan_in);

		if (!used)
			continue;
		if (d->d_off[f] == SPEQ_SKIN_NONE || (d->d_off[f] & 3) ||
		    d->d_off[f] + dneed[f] > d->dst_stride)
			return -22;
	}
	return 0;
}

/* The streams a descriptor's fields read (bit s: stream s) */
static inline unsigned sk_streams(const struct skin_desc *d)
{
	unsigned m = 1u << (d->f_stream[SKIN_F_POS] & 1) | 1u << (d->f_stream[SKIN_F_INDEX] & 1);

	if (d->f_off[SKIN_F_NORM] != SPEQ_SKIN_NONE && d->d_off[SKIN_D_NORM] != SPEQ_SKIN_NONE)
		m |= 1u << (d->f_stream[SKIN_F_NORM] & 1);
	if (d->tan_in)
		m |= 1u << (d->f_stream[SKIN_F_TAN] & 1);
	if (d->influence == SPEQ_SKIN_W4)
		m |= 1u << (d->f_stream[SKIN_F_WEIGHTS] & 1);
	return m;
}

/* sk_loop with the formats as constants for the common meshes (Sauerbraten's two: normals, or
 * tangent frames into shorts), the rest through the general loop */
static inline int sk_run(const struct skin_desc *d, const struct sk_pal *pl, const uint8_t *s0,
			 const uint8_t *s1, uint8_t *o, uint32_t n)
{
	int hasn = d->f_off[SKIN_F_NORM] != SPEQ_SKIN_NONE && d->d_off[SKIN_D_NORM] != SPEQ_SKIN_NONE;

	if (d->palette_fmt == SPEQ_SKIN_DQ && d->influence == SPEQ_SKIN_IDX_S32) {
		if (hasn && !d->tan_in)
			return sk_loop(d, pl, s0, s1, o, n, SPEQ_SKIN_DQ, SPEQ_SKIN_IDX_S32, 1, 0, 0);
		if (!hasn && d->tan_in == SPEQ_SKIN_TAN_QUAT && d->tan_out == SPEQ_SKIN_OUT_S16)
			return sk_loop(d, pl, s0, s1, o, n, SPEQ_SKIN_DQ, SPEQ_SKIN_IDX_S32, 0,
				       SPEQ_SKIN_TAN_QUAT, SPEQ_SKIN_OUT_S16);
	}
	return sk_loop(d, pl, s0, s1, o, n, d->palette_fmt, d->influence, hasn, d->tan_in, d->tan_out);
}

#endif
