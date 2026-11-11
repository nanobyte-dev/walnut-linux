// SPDX-License-Identifier: GPL-2.0
/* v2_chacha20.c: ChaCha20 (RFC 8439 2.3, 2.4), scalar and four blocks per vector (for vseal.c). */
#include <string.h>

#include "v2_crypto.h"

static inline uint32_t ld32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline void st32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static inline uint32_t rotl(uint32_t v, int n)
{
	return v << n | v >> (32 - n);
}

#define QR(a, b, c, d) do {					\
	a += b; d ^= a; d = rotl(d, 16);			\
	c += d; b ^= c; b = rotl(b, 12);			\
	a += b; d ^= a; d = rotl(d, 8);				\
	c += d; b ^= c; b = rotl(b, 7);				\
} while (0)

static void init_state(uint32_t s[16], const uint8_t key[32], const uint8_t nonce[12], uint32_t counter)
{
	int i;

	s[0] = 0x61707865;
	s[1] = 0x3320646e;
	s[2] = 0x79622d32;
	s[3] = 0x6b206574;
	for (i = 0; i < 8; i++)
		s[4 + i] = ld32(key + 4 * i);
	s[12] = counter;
	s[13] = ld32(nonce);
	s[14] = ld32(nonce + 4);
	s[15] = ld32(nonce + 8);
}

void v2_chacha20_block(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
		       uint32_t counter, uint8_t out[64])
{
	uint32_t s[16], x[16];
	int i;

	init_state(s, key, nonce, counter);
	memcpy(x, s, sizeof(x));
	for (i = 0; i < 10; i++) {
		QR(x[0], x[4], x[8], x[12]);
		QR(x[1], x[5], x[9], x[13]);
		QR(x[2], x[6], x[10], x[14]);
		QR(x[3], x[7], x[11], x[15]);
		QR(x[0], x[5], x[10], x[15]);
		QR(x[1], x[6], x[11], x[12]);
		QR(x[2], x[7], x[8], x[13]);
		QR(x[3], x[4], x[9], x[14]);
	}
	for (i = 0; i < 16; i++)
		st32(out + 4 * i, x[i] + s[i]);
}

void v2_chacha20_xor(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
		     uint32_t counter, uint8_t *dst, const uint8_t *src, size_t len)
{
	uint8_t ks[64];
	size_t i, n;

	while (len) {
		v2_chacha20_block(key, nonce, counter++, ks);
		n = len < 64 ? len : 64;
		for (i = 0; i < n; i++)
			dst[i] = src[i] ^ ks[i];
		dst += n;
		src += n;
		len -= n;
	}
}

/* --- four blocks per vector ------------------------------------------------------------------ */

typedef uint32_t v4u __attribute__((vector_size(16)));
typedef uint8_t v16u8 __attribute__((vector_size(16)));
typedef int32_t v4i __attribute__((vector_size(16)));
typedef int8_t v16i8 __attribute__((vector_size(16)));

static inline v4u vrotl(v4u v, int n)
{
	return v << n | v >> (32 - n);
}

#define VQR(a, b, c, d) do {					\
	a += b; d ^= a; d = vrotl(d, 16);			\
	c += d; b ^= c; b = vrotl(b, 12);			\
	a += b; d ^= a; d = vrotl(d, 8);			\
	c += d; b ^= c; b = vrotl(b, 7);			\
} while (0)

/* in-memory order of a quadword of four little-endian keystream words */
static inline v4u to_le(v4u v)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	const v16u8 sw = { 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12 };

	return (v4u)__builtin_shuffle((v16u8)v, sw);
#else
	return v;
#endif
}

/* lanes a0 b0 a1 b1 and so on: the 4x4 transpose, from word-major to block-major */
static inline void transpose(v4u a, v4u b, v4u c, v4u d, v4u out[4])
{
	const v4i lo = { 0, 4, 1, 5 }, hi = { 2, 6, 3, 7 }, l2 = { 0, 1, 4, 5 }, h2 = { 2, 3, 6, 7 };
	v4u t0 = __builtin_shuffle(a, b, lo), t1 = __builtin_shuffle(c, d, lo);
	v4u t2 = __builtin_shuffle(a, b, hi), t3 = __builtin_shuffle(c, d, hi);

	out[0] = __builtin_shuffle(t0, t1, l2);
	out[1] = __builtin_shuffle(t0, t1, h2);
	out[2] = __builtin_shuffle(t2, t3, l2);
	out[3] = __builtin_shuffle(t2, t3, h2);
}

void v2_chacha20_xor_x4(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
			uint32_t counter, uint8_t *dst, const uint8_t *src, size_t len)
{
	uint32_t s[16];
	v4u in[16], x[16], q[4];
	const v4u inc = { 0, 1, 2, 3 }, four = { 4, 4, 4, 4 };
	size_t off;
	int i, g, r;

	if (((uintptr_t)dst | (uintptr_t)src) & 15) {	/* the SPU's quadword loads would align silently */
		v2_chacha20_xor(key, nonce, counter, dst, src, len - len % 256);
		return;
	}
	init_state(s, key, nonce, counter);
	for (i = 0; i < 16; i++)
		in[i] = (v4u){ s[i], s[i], s[i], s[i] };
	in[12] += inc;
	for (off = 0; off + 256 <= len; off += 256) {
		for (i = 0; i < 16; i++)
			x[i] = in[i];
		for (r = 0; r < 10; r++) {
			VQR(x[0], x[4], x[8], x[12]);
			VQR(x[1], x[5], x[9], x[13]);
			VQR(x[2], x[6], x[10], x[14]);
			VQR(x[3], x[7], x[11], x[15]);
			VQR(x[0], x[5], x[10], x[15]);
			VQR(x[1], x[6], x[11], x[12]);
			VQR(x[2], x[7], x[8], x[13]);
			VQR(x[3], x[4], x[9], x[14]);
		}
		for (i = 0; i < 16; i++)
			x[i] += in[i];
		/* word group g (words 4g..4g+3) of blocks 0-3 */
		for (g = 0; g < 4; g++) {
			transpose(x[4 * g], x[4 * g + 1], x[4 * g + 2], x[4 * g + 3], q);
			for (i = 0; i < 4; i++) {	/* block i, bytes 16g..16g+15 */
				size_t o = off + 64 * i + 16 * g;

				*(v4u *)(dst + o) = *(const v4u *)(src + o) ^ to_le(q[i]);
			}
		}
		in[12] += four;
	}
}
