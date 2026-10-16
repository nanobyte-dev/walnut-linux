/* The SPU DXT1 encoder (dxt.c): van Waveren's real-time DXT1 on 4x4 blocks, byte for byte the
 * same output as libspeq's scalar reference (lib/dxt-ref.c). Built for the SPU (spu-elf-gcc,
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

#endif
