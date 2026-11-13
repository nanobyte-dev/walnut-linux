/* SPDX-License-Identifier: GPL-2.0 */
/* v2_crypto.h: ChaCha20-Poly1305 (RFC 8439) for the encrypted VRAM swap, one portable C core.
 *
 * Built unchanged for the PC (the host tests, against the RFC 8439 vectors), the SPU (spu-elf-gcc:
 * vseal.c, the program of ps3rsx's SPE client) and the PPE. Only 32-bit
 * integer arithmetic: no 64-bit operation anywhere, so the SPU needs no carry emulation, and every
 * multiply is 16 x 16 -> 32 bits unsigned (the SPU's mpyu). The bounds that make that true are in
 * v2_poly1305.c's header comment and are checked at run time in the V2_CHECKED build.
 *
 * Byte order: RFC 8439 is little-endian on the wire; the code reads and writes bytes explicitly
 * (scalar paths) or byte-swaps quadwords (the vector path) on a big-endian target, so the same
 * ciphertext comes out on the PC, the PPE and the SPU.
 */
#ifndef V2_CRYPTO_H
#define V2_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#define V2_KEY_BYTES	32
#define V2_NONCE_BYTES	12
#define V2_TAG_BYTES	16
#define V2_PAGE_BYTES	4096
#define V2_AD_BYTES	16

/* ChaCha20 (RFC 8439 2.4): dst = src ^ keystream(key, nonce, counter...). Any length; dst may be src. */
void v2_chacha20_xor(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
		     uint32_t counter, uint8_t *dst, const uint8_t *src, size_t len);
/* One 64-byte block (RFC 8439 2.3) */
void v2_chacha20_block(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
		       uint32_t counter, uint8_t out[64]);
/* Four blocks at a time in the four 32-bit lanes (GCC vector extensions: the SPU's 128-bit
 * registers, SSE on the PC). len a multiple of 256, dst and src 16-byte aligned. */
void v2_chacha20_xor_x4(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
			uint32_t counter, uint8_t *dst, const uint8_t *src, size_t len);

/* Poly1305 (RFC 8439 2.5), 13-bit limbs (v2_poly1305.c) */
struct v2_poly1305 {
	uint32_t r[10], s5[10];		/* r's limbs, 5 * r's limbs */
	uint32_t h[10];			/* the accumulator's limbs */
	uint32_t pad[4];		/* s, the key's second half, as 4 little-endian words */
	uint8_t buf[16];
	uint32_t buflen;
};
void v2_poly1305_init(struct v2_poly1305 *st, const uint8_t key[32]);
void v2_poly1305_update(struct v2_poly1305 *st, const uint8_t *m, size_t len);
void v2_poly1305_final(struct v2_poly1305 *st, uint8_t tag[V2_TAG_BYTES]);
void v2_poly1305(const uint8_t key[32], const uint8_t *m, size_t len, uint8_t tag[V2_TAG_BYTES]);

/* AEAD_CHACHA20_POLY1305 (RFC 8439 2.8). seal: ct = E(pt), tag over (aad, ct). open: the tag is
 * checked over ct first; only if it matches is ct decrypted into pt (pt may be ct). Returns 0, or
 * -1 on a tag mismatch, in which case pt is left untouched. */
void v2_aead_seal(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
		  const uint8_t *aad, size_t alen, const uint8_t *pt, size_t len, uint8_t *ct,
		  uint8_t tag[V2_TAG_BYTES]);
int v2_aead_open(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
		 const uint8_t *aad, size_t alen, const uint8_t *ct, size_t len, uint8_t *pt,
		 const uint8_t tag[V2_TAG_BYTES]);

/* The page form V2 uses: a 4096-byte page, a 16-byte AD, the vector ChaCha20, 16-byte aligned
 * buffers. open checks the tag over the one copy at ct and decrypts that same buffer, so whatever
 * changes ct's source afterwards can only fail the tag. */
void v2_seal_page(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
		  const uint8_t ad[V2_AD_BYTES], const uint8_t *pt, uint8_t *ct,
		  uint8_t tag[V2_TAG_BYTES]);
int v2_open_page(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
		 const uint8_t ad[V2_AD_BYTES], const uint8_t *ct, uint8_t *pt,
		 const uint8_t tag[V2_TAG_BYTES]);

/* The swap's nonce: 4 zero bytes, then the 64-bit per-key write counter,
 * little-endian (the kernel's chacha20poly1305_encrypt(..., u64 nonce, ...) layout). */
void v2_nonce_from_counter(uint64_t ctr, uint8_t nonce[V2_NONCE_BYTES]);
/* The swap's AD: device page (u32), slot (u32), format (u8), 7 zero bytes; little-endian */
void v2_ad_make(uint32_t dev_page, uint32_t slot, uint8_t format, uint8_t ad[V2_AD_BYTES]);

/* constant-time compare of n bytes: 0 if equal */
int v2_ct_differs(const uint8_t *a, const uint8_t *b, size_t n);

/* --- the four-lane Poly1305 ---------------------------------------------------------------------- */

/* scalar Poly1305's pieces the four-lane form shares (v2_poly1305.c) */
void v2_poly1305_limbs(const uint32_t w[4], uint32_t l[10]);
void v2_poly1305_finish(uint32_t h[10], const uint32_t pad[4], uint8_t tag[V2_TAG_BYTES]);
/* Poly1305, four blocks per vector (v2_poly1305_x4.c); same tags as v2_poly1305 */
void v2_poly1305_x4(const uint8_t key[32], const uint8_t *m, size_t len, uint8_t tag[V2_TAG_BYTES]);
/* the page's MAC input (AD 16 || ct 4096 || lengths), 16-byte aligned ct */
void v2_poly1305_x4_page(const uint8_t key[32], const uint8_t ad[16], const uint8_t *ct,
			 uint8_t tag[V2_TAG_BYTES]);
/* the page form with the scalar Poly1305 (the scalar reference the x4 form is checked against) */
void v2_seal_page_ref(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
		      const uint8_t ad[V2_AD_BYTES], const uint8_t *pt, uint8_t *ct,
		      uint8_t tag[V2_TAG_BYTES]);

#ifdef V2_CHECKED
/* the checked build's counters: every bound in v2_poly1305.c that held (for the test's report) */
extern unsigned long v2_bound_checks;
#endif

#endif
