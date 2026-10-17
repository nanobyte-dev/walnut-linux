/* Host emulation of the SPU intrinsics dxt.c uses, so its SIMD path can be checked against the
 * reference on x86 before it runs on an SPE. Not a general spu_intrinsics.h.
 *
 * Layout: a host vector holds the SPU quadword byte-reversed (host byte j = SPU byte 15 - j).
 * On a little-endian host every element type then has the SPU element's value, at index
 * n - 1 - i for SPU element i, so casts between vector types behave as on the SPU and the
 * element-wise operations are plain GCC vector arithmetic. Only the operations that look at
 * positions (shuffle, quadword rotate, sumb, mulo, orx, loads, stores, literals) convert to the
 * SPU byte order (emu_be/emu_host) and back. */
#ifndef DXT_EMU_H
#define DXT_EMU_H

#include <stdint.h>
#include <string.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "dxt-emu.h assumes a little-endian host"
#endif

typedef unsigned char vec_uchar16 __attribute__((vector_size(16)));
typedef signed char vec_char16 __attribute__((vector_size(16)));
typedef unsigned short vec_ushort8 __attribute__((vector_size(16)));
typedef short vec_short8 __attribute__((vector_size(16)));
typedef unsigned int vec_uint4 __attribute__((vector_size(16)));
typedef int vec_int4 __attribute__((vector_size(16)));

static inline void emu_be(uint8_t be[16], vec_uchar16 v)
{
	int i;

	for (i = 0; i < 16; i++)
		be[i] = v[15 - i];
}

static inline vec_uchar16 emu_host(const uint8_t be[16])
{
	vec_uchar16 v;
	int i;

	for (i = 0; i < 16; i++)
		v[i] = be[15 - i];
	return v;
}

/* literals, written in SPU element order (dxt.c's VU8/VU32) */
static inline vec_uchar16 emu_lit8(const uint8_t be[16])
{
	return emu_host(be);
}

static inline vec_uint4 emu_lit32(const uint32_t w[4])
{
	vec_uint4 v = { w[3], w[2], w[1], w[0] };

	return v;
}

static inline vec_uchar16 emu_load(const void *p)
{
	return emu_host((const uint8_t *)p);
}

static inline void emu_store(void *p, vec_uchar16 v)
{
	emu_be((uint8_t *)p, v);
}

static inline vec_uchar16 emu_shuffle(vec_uchar16 a, vec_uchar16 b, vec_uchar16 pat)
{
	uint8_t A[16], B[16], P[16], R[16];
	int i;

	emu_be(A, a);
	emu_be(B, b);
	emu_be(P, pat);
	for (i = 0; i < 16; i++) {
		uint8_t p = P[i];

		if ((p & 0xc0) == 0x80)
			R[i] = 0;
		else if ((p & 0xe0) == 0xc0)
			R[i] = 0xff;
		else if ((p & 0xe0) == 0xe0)
			R[i] = 0x80;
		else
			R[i] = p & 16 ? B[p & 15] : A[p & 15];
	}
	return emu_host(R);
}

static inline vec_uchar16 emu_rlqwbyte(vec_uchar16 a, int n)
{
	uint8_t A[16], R[16];
	int i;

	emu_be(A, a);
	for (i = 0; i < 16; i++)
		R[i] = A[(i + n) & 15];
	return emu_host(R);
}

/* sumb rt,ra,rb: per word, the sum of rb's 4 bytes in the even (left) halfword, ra's in the odd */
static inline vec_ushort8 emu_sumb(vec_uchar16 a, vec_uchar16 b)
{
	uint8_t A[16], B[16], R[16];
	int w;

	emu_be(A, a);
	emu_be(B, b);
	for (w = 0; w < 4; w++) {
		unsigned sb = B[4 * w] + B[4 * w + 1] + B[4 * w + 2] + B[4 * w + 3];
		unsigned sa = A[4 * w] + A[4 * w + 1] + A[4 * w + 2] + A[4 * w + 3];

		R[4 * w] = sb >> 8;
		R[4 * w + 1] = sb;
		R[4 * w + 2] = sa >> 8;
		R[4 * w + 3] = sa;
	}
	return (vec_ushort8)emu_host(R);
}

/* mpyu: the odd (right) halfwords of each word, unsigned, to 32 bits */
static inline vec_uint4 emu_mulo(vec_ushort8 a, vec_ushort8 b)
{
	vec_uint4 r = { 0 };
	int i;

	for (i = 0; i < 4; i++)	/* SPU halfword 2i+1 is host halfword 6-2i, word i is host 3-i */
		r[3 - i] = (unsigned)a[6 - 2 * i] * b[6 - 2 * i];
	return r;
}

/* mpyhhu: the even (left) halfwords of each word, unsigned, to 32 bits */
static inline vec_uint4 emu_mule(vec_ushort8 a, vec_ushort8 b)
{
	vec_uint4 r = { 0 };
	int i;

	for (i = 0; i < 4; i++)	/* SPU halfword 2i is host halfword 7-2i */
		r[3 - i] = (unsigned)a[7 - 2 * i] * b[7 - 2 * i];
	return r;
}

/* mpy and mpyhh: the same, signed */
static inline vec_int4 emu_mulo_s(vec_short8 a, vec_short8 b)
{
	vec_int4 r = { 0 };
	int i;

	for (i = 0; i < 4; i++)
		r[3 - i] = (int)a[6 - 2 * i] * b[6 - 2 * i];
	return r;
}

static inline vec_int4 emu_mule_s(vec_short8 a, vec_short8 b)
{
	vec_int4 r = { 0 };
	int i;

	for (i = 0; i < 4; i++)
		r[3 - i] = (int)a[7 - 2 * i] * b[7 - 2 * i];
	return r;
}

/* avgb: (a + b + 1) >> 1 per byte */
static inline vec_uchar16 emu_avg(vec_uchar16 a, vec_uchar16 b)
{
	vec_uchar16 r = { 0 };
	int i;

	for (i = 0; i < 16; i++)
		r[i] = (a[i] + b[i] + 1) >> 1;
	return r;
}

static inline vec_uint4 emu_orx(vec_uint4 a)
{
	vec_uint4 r = { 0, 0, 0, a[0] | a[1] | a[2] | a[3] };	/* SPU word 0 = host 3 */

	return r;
}

static inline vec_uchar16 emu_absd(vec_uchar16 a, vec_uchar16 b)
{
	vec_uchar16 gt = (vec_uchar16)(a > b);

	return ((a - b) & gt) | ((b - a) & ~gt);
}

#define EMU_UNS(x) _Generic((x), \
	vec_char16: (vec_uchar16)(x), vec_uchar16: (vec_uchar16)(x), \
	vec_short8: (vec_ushort8)(x), vec_ushort8: (vec_ushort8)(x), \
	vec_int4: (vec_uint4)(x), vec_uint4: (vec_uint4)(x))

#define spu_and(a, b)		((a) & (b))
#define spu_or(a, b)		((a) | (b))
#define spu_andc(a, b)		((a) & ~(b))
#define spu_orc(a, b)		((a) | ~(b))
#define spu_nor(a, b)		(~((a) | (b)))
#define spu_add(a, b)		((a) + (b))
#define spu_sub(a, b)		((a) - (b))
#define spu_cmpgt(a, b)		EMU_UNS((a) > (b))
#define spu_cmpeq(a, b)		EMU_UNS((a) == (b))
#define spu_sel(a, b, m)	({ __typeof__(a) _m = (__typeof__(a))(m); \
				   ((a) & ~_m) | ((__typeof__(a))(b) & _m); })
#define spu_rlmask(a, n)	((a) >> -(n))
#define spu_sl(a, n)		((a) << (n))
/* word rotate, scalar or per-word counts (mod 32) */
static inline vec_uint4 emu_cnt_v(vec_int4 n) { return (vec_uint4)n & 31; }
static inline vec_uint4 emu_cnt_uv(vec_uint4 n) { return n & 31; }
static inline vec_uint4 emu_cnt_s(int n) { return ((vec_uint4){} + (unsigned)n) & 31; }
#define spu_rl(a, n)		({ vec_uint4 _a = (vec_uint4)(a), _n = _Generic((n), \
				   vec_int4: emu_cnt_v, vec_uint4: emu_cnt_uv, default: emu_cnt_s)(n); \
				   (__typeof__(a))((_a << _n) | (_a >> ((32 - _n) & 31))); })
#define spu_absd(a, b)		emu_absd(a, b)
#define spu_sumb(a, b)		emu_sumb(a, b)
#define spu_mulo(a, b)		_Generic((a), vec_short8: emu_mulo_s, default: emu_mulo)(a, b)
#define spu_mule(a, b)		_Generic((a), vec_short8: emu_mule_s, default: emu_mule)(a, b)
#define spu_avg(a, b)		emu_avg(a, b)
#define spu_orx(a)		emu_orx(a)
#define spu_shuffle(a, b, p)	((__typeof__(a))emu_shuffle((vec_uchar16)(a), (vec_uchar16)(b), p))
#define spu_rlqwbyte(a, n)	((__typeof__(a))emu_rlqwbyte((vec_uchar16)(a), n))
#define spu_extract(a, i)	((a)[sizeof(a) / sizeof((a)[0]) - 1 - (i)])

#endif
