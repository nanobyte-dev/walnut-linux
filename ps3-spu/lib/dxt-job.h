/* dxt.spu's job (spu/dxt-op.c), shared with the PPE side (lib/s3tc.c): one DXT_OP_ROWS job
 * encodes a range of block rows of a tightly packed RGBA8 image to DXT1 or DXT5.
 *   arg[DXT_ARG_SRC]     EA of the image's first pixel (any alignment)
 *   arg[DXT_ARG_WIDTH]   width in pixels, a multiple of 4
 *   arg[DXT_ARG_ROW0]    first block row of the job
 *   arg[DXT_ARG_ROWS]    block rows in the job
 *   arg[DXT_ARG_DST]     EA of block row 0's output (8-aligned; DXT5 16-aligned)
 *   arg[DXT_ARG_STRIDE]  bytes from one block row's output to the next (a multiple of 8, DXT5
 *                        16, at least a block row: 2 * width, DXT5 4 * width)
 *   arg[DXT_ARG_MODE]    DXT_JOB_RGB, DXT_JOB_RGBA (spu/dxt.h's DXT1_MODE_*) or DXT_JOB_DXT5
 * Block row r is read from src + 16 * width * r (16 * width bytes) and written to
 * dst + stride * r (2 * width bytes, DXT5 4 * width, nothing past them). Errors: -EINVAL (22)
 * for arguments outside this. */
#ifndef DXT_JOB_H
#define DXT_JOB_H

enum {
	DXT_OP_ROWS,
	DXT_NOPS
};

enum {
	DXT_ARG_SRC,
	DXT_ARG_WIDTH,
	DXT_ARG_ROW0,
	DXT_ARG_ROWS,
	DXT_ARG_DST,
	DXT_ARG_STRIDE,
	DXT_ARG_MODE,
};

#define DXT_JOB_RGB	0	/* RGB DXT1: alpha ignored */
#define DXT_JOB_RGBA	1	/* RGBA DXT1: alpha <= 127 is transparent */
#define DXT_JOB_DXT5	2	/* RGBA DXT5: 16 bytes a block (lib/dxt-ref.c) */

/* The widest strip the SPU encodes at once: wider images go in column chunks of this many
 * pixels, each pixel row fetched on its own (local store budget, spu/dxt-op.c) */
#define DXT_CHUNK_W	1024

#endif
