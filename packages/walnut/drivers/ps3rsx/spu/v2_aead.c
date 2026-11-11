// SPDX-License-Identifier: GPL-2.0
/* v2_aead.c: AEAD_CHACHA20_POLY1305 (RFC 8439 2.8) and the VRAM swap's page form. */
#include <string.h>

#include "v2_crypto.h"

static const uint8_t zero16[16];

int v2_ct_differs(const uint8_t *a, const uint8_t *b, size_t n)
{
	uint32_t d = 0;
	size_t i;

	for (i = 0; i < n; i++)
		d |= a[i] ^ b[i];
	return (int)((d + 0xff) >> 8);	/* 1 if any bit differed, without a data-dependent branch */
}

static void le64(uint8_t *p, uint64_t v)
{
	int i;

	for (i = 0; i < 8; i++)
		p[i] = (uint8_t)(v >> (8 * i));
}

/* the tag over (aad, ct), RFC 8439 2.8: otk from block 0 */
static void mac(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t alen,
		const uint8_t *ct, size_t len, uint8_t tag[16])
{
	struct v2_poly1305 st;
	uint8_t otk[64], lens[16];

	v2_chacha20_block(key, nonce, 0, otk);
	v2_poly1305_init(&st, otk);
	v2_poly1305_update(&st, aad, alen);
	if (alen & 15)
		v2_poly1305_update(&st, zero16, 16 - (alen & 15));
	v2_poly1305_update(&st, ct, len);
	if (len & 15)
		v2_poly1305_update(&st, zero16, 16 - (len & 15));
	le64(lens, alen);
	le64(lens + 8, len);
	v2_poly1305_update(&st, lens, 16);
	v2_poly1305_final(&st, tag);
	memset(otk, 0, sizeof(otk));
}

void v2_aead_seal(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
		  const uint8_t *aad, size_t alen, const uint8_t *pt, size_t len, uint8_t *ct,
		  uint8_t tag[V2_TAG_BYTES])
{
	v2_chacha20_xor(key, nonce, 1, ct, pt, len);
	mac(key, nonce, aad, alen, ct, len, tag);
}

int v2_aead_open(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
		 const uint8_t *aad, size_t alen, const uint8_t *ct, size_t len, uint8_t *pt,
		 const uint8_t tag[V2_TAG_BYTES])
{
	uint8_t t[16];
	int bad;

	mac(key, nonce, aad, alen, ct, len, t);
	bad = v2_ct_differs(t, tag, 16);
	memset(t, 0, sizeof(t));
	if (bad)
		return -1;
	v2_chacha20_xor(key, nonce, 1, pt, ct, len);
	return 0;
}

#ifndef V2_SCALAR_ONLY	/* (the kernel's PPE copy: the scalar code only) */
/* the one-time key: block 0 of the nonce's keystream, from one four-block run (blocks 0-3) */
static void otk_x4(const uint8_t key[32], const uint8_t nonce[12], uint8_t otk[32])
{
	static const uint8_t zero[256] __attribute__((aligned(16)));
	uint8_t ks[256] __attribute__((aligned(16)));

	v2_chacha20_xor_x4(key, nonce, 0, ks, zero, 256);
	memcpy(otk, ks, 32);
	memset(ks, 0, sizeof(ks));
}

void v2_seal_page(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
		  const uint8_t ad[V2_AD_BYTES], const uint8_t *pt, uint8_t *ct,
		  uint8_t tag[V2_TAG_BYTES])
{
	uint8_t otk[32];

	v2_chacha20_xor_x4(key, nonce, 1, ct, pt, V2_PAGE_BYTES);
	otk_x4(key, nonce, otk);
	v2_poly1305_x4_page(otk, ad, ct, tag);
	memset(otk, 0, sizeof(otk));
}

void v2_seal_page_ref(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
		      const uint8_t ad[V2_AD_BYTES], const uint8_t *pt, uint8_t *ct,
		      uint8_t tag[V2_TAG_BYTES])
{
	v2_chacha20_xor_x4(key, nonce, 1, ct, pt, V2_PAGE_BYTES);
	mac(key, nonce, ad, V2_AD_BYTES, ct, V2_PAGE_BYTES, tag);
}

int v2_open_page(const uint8_t key[V2_KEY_BYTES], const uint8_t nonce[V2_NONCE_BYTES],
		 const uint8_t ad[V2_AD_BYTES], const uint8_t *ct, uint8_t *pt,
		 const uint8_t tag[V2_TAG_BYTES])
{
	uint8_t otk[32], t[16];
	int bad;

	otk_x4(key, nonce, otk);
	v2_poly1305_x4_page(otk, ad, ct, t);
	memset(otk, 0, sizeof(otk));
	bad = v2_ct_differs(t, tag, 16);
	memset(t, 0, sizeof(t));
	if (bad)
		return -1;
	v2_chacha20_xor_x4(key, nonce, 1, pt, ct, V2_PAGE_BYTES);
	return 0;
}

#endif

void v2_nonce_from_counter(uint64_t ctr, uint8_t nonce[V2_NONCE_BYTES])
{
	memset(nonce, 0, 4);
	le64(nonce + 4, ctr);
}

void v2_ad_make(uint32_t dev_page, uint32_t slot, uint8_t format, uint8_t ad[V2_AD_BYTES])
{
	int i;

	memset(ad, 0, V2_AD_BYTES);
	for (i = 0; i < 4; i++) {
		ad[i] = (uint8_t)(dev_page >> (8 * i));
		ad[4 + i] = (uint8_t)(slot >> (8 * i));
	}
	ad[8] = format;
}
