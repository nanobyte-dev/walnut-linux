/* The reference DXT1 and DXT5 encoders (dxt-ref.c): the PPE path of speq_s3tc_compress_v1 and
 * what the SPU encoder must match byte for byte; plus decoders for the tests. Internal to libspeq
 * (not exported) and compiled into the tools. */
#ifndef DXT_REF_H
#define DXT_REF_H

#include <stdint.h>

#define DXT1_ALPHACUT	127	/* RGBA DXT1: alpha <= this is transparent (Mesa's ALPHACUT) */

/* One 4x4 block: px is 16 RGBA pixels, row by row; punch: RGBA DXT1 (alpha cut) */
void dxt1_ref_block(uint8_t out[8], const uint8_t px[16][4], int punch);
/* A whole image, w and h multiples of 4, srccomps 3 or 4, block rows dst_stride apart */
void dxt1_ref_image(const uint8_t *src, unsigned srccomps, unsigned w, unsigned h,
		    uint8_t *dst, unsigned dst_stride, int punch);
/* Decodes one block to 16 RGBA pixels as Mesa's decoder does (punch: index 3 of the
 * 3-colour mode is transparent, else opaque black) */
void dxt1_decode_block(uint8_t px[16][4], const uint8_t in[8], int punch);

/* DXT5 (RGBA_DXT5): 16 bytes a block, the alpha block (dxt5_alpha_block) then the colour block
 * (dxt5_color_block); dxt-ref.c's comment has both algorithms */
void dxt5_ref_block(uint8_t out[16], const uint8_t px[16][4]);
void dxt5_alpha_block(uint8_t out[8], const uint8_t a[16]);
void dxt5_color_block(uint8_t out[8], const uint8_t px[16][4]);
/* A whole image, w and h multiples of 4, srccomps 3 (opaque) or 4, block rows dst_stride apart */
void dxt5_ref_image(const uint8_t *src, unsigned srccomps, unsigned w, unsigned h,
		    uint8_t *dst, unsigned dst_stride);
/* Decodes one block as Mesa's decoder does (and the RSX: the colour block always in 4-colour
 * mode, the alpha palette by alpha0 > alpha1) */
void dxt5_decode_block(uint8_t px[16][4], const uint8_t in[16]);

#endif
