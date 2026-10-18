/* The SPU DXT1 and DXT5 encoder (dxt.c): van Waveren's real-time DXT1 on 4x4 blocks and DXT5 on
 * top of it, byte for byte the same output as libspeq's scalar reference (lib/dxt-ref.c). Built for the SPU (spu-elf-gcc,
 * SIMD), with -DDXT_SCALAR as plain C (debugging), or for the host with dxt-emu.h emulating the
 * SPU intrinsics (tests). */
#ifndef DXT_H
#define DXT_H

#include <stdint.h>

#define DXT1_MODE_RGB	0	/* RGB_DXT1: alpha ignored */
#define DXT1_MODE_RGBA	1	/* RGBA_DXT1: alpha <= 127 is transparent (punch-through) */

/* Encodes block_rows block rows of a tightly packed RGBA8 image, width pixels wide (a multiple
 * of 4): the pixels start at src + src_offset (src_offset 0..15, the DMA fetched from the
 * 16-byte boundary below) and run for block_rows * 16 * width bytes, 4 pixel rows of 4 * width
 * bytes per block row. Up to 16 bytes past the end are read (never used): local store doesn't
 * fault. dst gets 8 bytes per block, little-endian as Mesa writes them, block rows one after
 * the other (2 * width bytes each); it must be 8-byte aligned, and the other half of a
 * partly written quadword is kept. alpha_mode is DXT1_MODE_RGB or DXT1_MODE_RGBA. */
void dxt1_encode_rows(const uint8_t *src, unsigned src_offset, unsigned width, unsigned block_rows,
		      uint8_t *dst, int alpha_mode);
/* The same with pixel rows src_stride bytes apart (a multiple of 16, at least 4 * width): a
 * column strip of a wider image, its rows fetched one by one (dxt.spu's wide images) */
void dxt1_encode_rows_stride(const uint8_t *src, unsigned src_offset, unsigned width,
			     unsigned src_stride, unsigned block_rows, uint8_t *dst, int alpha_mode);
/* DXT5 (lib/dxt-ref.c's dxt5_ref_block), the source as for dxt1_encode_rows_stride: 16 bytes a
 * block, block rows one after the other (4 * width bytes each); dst 16-byte aligned */
void dxt5_encode_rows_stride(const uint8_t *src, unsigned src_offset, unsigned width,
			     unsigned src_stride, unsigned block_rows, uint8_t *dst);

/* npix pixels of R, G, B bytes (3 a pixel) at src + src_offset (src_offset 0..15, src 16-byte
 * aligned: the DMA fetched from the boundary below) to RGBA8 with alpha 255 at dst (16-byte
 * aligned), the layout the encoders above take. Works in groups of 16 pixels: reads up to 80
 * bytes past the 3 * npix source bytes and writes up to 48 past the 4 * npix output bytes (local
 * store doesn't fault; the caller leaves room). */
void dxt_rgb_expand(uint8_t *dst, const uint8_t *src, unsigned src_offset, unsigned npix);

#endif
