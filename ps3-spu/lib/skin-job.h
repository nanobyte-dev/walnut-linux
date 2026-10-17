/* skin.spu's job (spu/skin-op.c), shared with the PPE side (lib/skin.c). A batch's meshes each
 * have a descriptor (struct skin_desc, one 128-byte line, libspeq's memory); one SKIN_OP_RANGE
 * job skins a range of one mesh's vertices into the batch's output.
 *   arg[SKIN_ARG_DESC]   EA of the mesh's descriptor (128-aligned)
 *   arg[SKIN_ARG_FIRST]  first vertex of the job
 *   arg[SKIN_ARG_COUNT]  vertices in the job
 * Vertex v is read from src[s] + v * src_stride[s] (its fields at their offsets) and from
 * tmpl + v * dst_stride, and written to dst + v * dst_stride (dst_stride bytes, nothing outside
 * them: neighbouring jobs and meshes share 16-byte lines). The SPU never writes the inputs.
 * Errors: -EINVAL (22) for a descriptor or range outside this, -EIO (5) for a palette index
 * past the palette's end. */
#ifndef SKIN_JOB_H
#define SKIN_JOB_H

#include <stdint.h>

enum {
	SKIN_OP_RANGE,
	SKIN_NOPS
};

enum {
	SKIN_ARG_DESC,
	SKIN_ARG_FIRST,
	SKIN_ARG_COUNT,
};

#define SKIN_PAL_BYTES	49152	/* the palette in local store: 1536 DQ, 1024 matrices */
#define SKIN_DQ_MAX	(SKIN_PAL_BYTES / 32)
#define SKIN_MAT_MAX	(SKIN_PAL_BYTES / 48)
#define SKIN_STRIDE_MAX	1024	/* bytes per vertex of a stream or the output */
#define SKIN_CHUNK	8192	/* bytes of a stream (or the output) per chunk in local store */

enum { SKIN_F_POS, SKIN_F_NORM, SKIN_F_TAN, SKIN_F_INDEX, SKIN_F_WEIGHTS, SKIN_NF };
enum { SKIN_D_POS, SKIN_D_NORM, SKIN_D_TAN, SKIN_ND };

/* A mesh as the kernel (lib/skin-kern.h) runs it: struct speq_skin_mesh checked and with EAs */
struct skin_desc {
	uint64_t pal[2];		/* 16-aligned */
	uint64_t src[2];
	uint64_t tmpl;			/* 0: none (zeros) */
	uint64_t dst;			/* vertex 0's output */
	uint32_t npal[2];
	uint32_t src_stride[2];
	uint32_t dst_stride, nverts;
	uint16_t f_off[SKIN_NF];	/* SPEQ_SKIN_NONE: absent */
	uint16_t d_off[SKIN_ND];	/* SPEQ_SKIN_NONE: not written */
	uint8_t f_stream[SKIN_NF];
	uint8_t palette_fmt, influence, tan_in, tan_out;
} __attribute__((aligned(128)));

_Static_assert(sizeof(struct skin_desc) == 128, "a descriptor is one line");

#endif
