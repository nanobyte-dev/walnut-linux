/* Shared by the SPU test programs and the PPE reference: the pseudo-random generator, the
 * input generator for the execution tests and the scalar kernels (branches, unaligned
 * accesses). Plain C99 on 32-bit words, so both sides compute the same thing. Byte level
 * only where an endianness could show (the kernels). */
#ifndef SPT_GEN_H
#define SPT_GEN_H

#include <stdint.h>
#include <string.h>

#ifdef __SPU__
#define SPT_GENATTR __attribute__((noinline, unused))
#else
#define SPT_GENATTR inline __attribute__((unused))
#endif

static inline uint32_t spt_rnd(uint32_t *s)
{
	uint32_t x = *s;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*s = x;
	return x;
}

static inline uint32_t spt_seed(uint32_t a, uint32_t b)
{
	uint32_t s = a * 2654435761u ^ (b + 0x9E3779B9u) * 40503u;

	return s ? s : 0x1234567u;
}

/* input classes */
enum { IN_INT, IN_SP, IN_SPE, IN_DP, IN_DPD };

/* One quadword each of a, b, c (word 0 first), from the state s */
static SPT_GENATTR void spt_gen(int cls, uint32_t *s, uint32_t a[4], uint32_t b[4], uint32_t c[4])
{
	static const uint32_t edge[] = {
		0x00000000u, 0x80000000u, 0x00000001u, 0x007FFFFFu, 0x80400000u, 0x00800000u,
		0x3F800000u, 0xBF800000u, 0x7F7FFFFFu, 0x40490FDBu, 0x01000000u, 0x7E800000u,
		0xFF7FFFFFu, 0x80800000u, 0x3F000000u, 0x40000000u };
	uint32_t *v[3] = { a, b, c };
	int k, i;

	for (k = 0; k < 3; k++) {
		if (cls == IN_INT || cls == IN_SP || cls == IN_SPE) {
			for (i = 0; i < 4; i++) {
				uint32_t r = spt_rnd(s), m = spt_rnd(s);

				if (cls == IN_INT)
					v[k][i] = r;
				else if (cls == IN_SP)	/* 2^-15 .. 2^16 */
					v[k][i] = (r & 0x80000000u) | ((0x70u + ((r >> 8) & 0x1F) % 32u) << 23)
						| (m & 0x7FFFFFu);
				else
					v[k][i] = edge[r & 15];
			}
		} else {
			for (i = 0; i < 2; i++) {
				uint32_t r = spt_rnd(s), hi, lo = spt_rnd(s);
				uint32_t sign = r & 0x80000000u;

				if (cls == IN_DP) {	/* 2^-95 .. 2^96 */
					hi = sign | ((0x3A0u + (r >> 8) % 192u) << 20) | (spt_rnd(s) & 0xFFFFFu);
				} else {
					switch ((r >> 4) % 6u) {
					case 0:	/* denormal */
						hi = sign | (spt_rnd(s) & 0xFFFFFu);
						break;
					case 1:	/* tiny normal */
						hi = sign | ((1u + ((r >> 8) & 3u)) << 20) | (spt_rnd(s) & 0xFFFFFu);
						break;
					case 2:	/* zero */
						hi = sign;
						lo = 0;
						break;
					case 3:	/* around 1 */
						hi = sign | ((0x3F0u + ((r >> 8) & 31u)) << 20) | (spt_rnd(s) & 0xFFFFFu);
						break;
					case 4:	/* small denormal */
						hi = sign;
						lo &= 0x3FFu;
						break;
					default:	/* larger */
						hi = sign | ((0x300u + (r >> 8) % 256u) << 20) | (spt_rnd(s) & 0xFFFFFu);
					}
				}
				v[k][2 * i] = hi;
				v[k][2 * i + 1] = lo;
			}
		}
	}
	/* make b equal a in a quarter of the lanes (the compares need equal pairs) */
	if (cls != IN_SPE && (spt_rnd(s) & 3) == 0) {
		if (cls == IN_DP || cls == IN_DPD) {
			b[0] = a[0]; b[1] = a[1];
		} else {
			b[1] = a[1];
			b[3] = a[3];
		}
	}
}

/* ---- scalar kernels: branches (indirect calls, jump tables, data dependent loops) ---- */
#define SPT_NOINLINE __attribute__((noinline))
static SPT_NOINLINE uint32_t spt_k0(uint32_t x) { return x * 3u + 1u; }
static SPT_NOINLINE uint32_t spt_k1(uint32_t x) { return (x << 7) ^ (x >> 3); }
static SPT_NOINLINE uint32_t spt_k2(uint32_t x) { return x + 0x9E3779B9u; }
static SPT_NOINLINE uint32_t spt_k3(uint32_t x) { return ~x ^ (x >> 11); }
static SPT_NOINLINE uint32_t spt_k4(uint32_t x) { return (x >> 5) | (x << 27); }
static SPT_NOINLINE uint32_t spt_k5(uint32_t x) { return x * 5u ^ 0x55AA55AAu; }
static SPT_NOINLINE uint32_t spt_k6(uint32_t x) { return x - (x >> 9); }
static SPT_NOINLINE uint32_t spt_k7(uint32_t x) { return x ^ (x << 13); }
static uint32_t (*const spt_ktab[8])(uint32_t) = {
	spt_k0, spt_k1, spt_k2, spt_k3, spt_k4, spt_k5, spt_k6, spt_k7 };

static SPT_NOINLINE uint32_t spt_depth(uint32_t x, unsigned d)
{
	if (d == 0)
		return x;
	return spt_depth(x * 33u + d, d - 1) ^ (x >> d);
}

static SPT_NOINLINE __attribute__((unused)) uint32_t spt_branch_kernel(uint32_t seed, uint32_t n)
{
	uint32_t x = seed | 1u, acc = 0, i;

	for (i = 0; i < n; i++) {
		uint32_t sel = (x >> 29) & 7u;

		switch (sel) {	/* a jump table */
		case 0: acc += x; break;
		case 1: acc ^= x >> 3; break;
		case 2: acc = (acc << 1) | (acc >> 31); break;
		case 3: acc -= x << 2; break;
		case 4: acc ^= spt_k3(acc); break;
		case 5: acc += i; break;
		case 6: acc = acc * 3u + 7u; break;
		default: acc ^= 0xA5A5A5A5u; break;
		}
		x = spt_ktab[(x >> 7) & 7u](x);	/* an indirect call */
		if (x & 1u)
			acc += x >> 16;
		else if (x & 2u)
			acc ^= x;
		else
			acc -= 3u;
		if ((i & 31u) == 0)
			acc ^= spt_depth(x, 6);
		{
			uint32_t t = x & 0x3Fu;	/* a short data dependent loop */

			while (t--)
				acc += t ^ (acc >> 7);
		}
		if (x == 0)
			x = seed | 3u;
	}
	return acc;
}

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SPT_BE16(v) ((uint16_t)__builtin_bswap16(v))
#define SPT_BE32(v) __builtin_bswap32(v)
#else
#define SPT_BE16(v) (v)
#define SPT_BE32(v) (v)
#endif

/* checksum of n bytes (a multiple of 4) as big-endian words, the same on both sides */
static inline uint32_t spt_ck32(const uint8_t *p, unsigned n)
{
	uint32_t h = 0, w;
	unsigned i;

	for (i = 0; i < n; i += 4) {
		memcpy(&w, p + i, 4);
		h = ((h << 5) | (h >> 27)) ^ (SPT_BE32(w) + i);
	}
	return h;
}

/* ---- unaligned and mixed-size memory accesses; buf is 16-aligned and 1024+ bytes ---- */
static inline uint32_t spt_mix(uint32_t h, uint32_t v)
{
	return (h ^ v) * 16777619u + (h >> 15);
}

static SPT_NOINLINE __attribute__((unused)) uint32_t spt_mem_kernel(uint8_t *buf, uint32_t seed)
{
	uint32_t s = spt_seed(seed, 77), h = 2166136261u, i, n, off;
	uint8_t tmp[16];

	for (i = 0; i < 1024; i++)
		buf[i] = (uint8_t)(spt_rnd(&s) >> 11);
	/* every size 1..16 at every offset: copy out, fold, copy back changed */
	for (n = 1; n <= 16; n++)
		for (off = 0; off + n <= 64 + 16; off++) {
			uint32_t j;

			memcpy(tmp, buf + off, n);
			for (j = 0; j < n; j++) {
				h = spt_mix(h, tmp[j]);
				tmp[j] = (uint8_t)(tmp[j] + j + n);
			}
			memcpy(buf + 512 + off, tmp, n);
		}
	/* byte, halfword, word and doubleword reads at their natural alignments, by value */
	for (i = 0; i < 1024; i++)
		h = spt_mix(h, buf[i]);
	for (i = 0; i < 1024; i += 2) {
		uint16_t v = *(volatile uint16_t *)(buf + i);

		h = spt_mix(h, (uint32_t)SPT_BE16(v) | 0x10000u);	/* big-endian view */
	}
	for (i = 0; i < 1024; i += 4) {
		uint32_t v = *(volatile uint32_t *)(buf + i);

		h = spt_mix(h, SPT_BE32(v) ^ i);
	}
	/* single byte and halfword stores into data other than their own */
	for (i = 0; i < 1024; i += 3) {
		buf[i] = (uint8_t)(h >> (i & 15));
		h = spt_mix(h, buf[i ^ 1]);
	}
	for (i = 0; i < 1024; i++)
		h = spt_mix(h, buf[i]);
	return h;
}

#endif
