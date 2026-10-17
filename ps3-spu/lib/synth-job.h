/* synth.spu's job (spu/synth-op.c), shared with the PPE side (lib/synth.c): one SYNTH_OP_BLOCK
 * job renders one block of a range of voices (struct speq_synth_voice, speq.h) into its own
 * output lines.
 *   arg[SYNTH_ARG_VOICES]  EA of the first voice (16-aligned, contiguous)
 *   arg[SYNTH_ARG_COUNT]   voices, at most SYNTH_JOB_MAX
 *   arg[SYNTH_ARG_OUT]     EA of the job's lines: lines x 64 doubles (128-aligned)
 *   arg[SYNTH_ARG_LINES]   lines, at most SPEQ_SYNTH_LINES
 *   arg[SYNTH_ARG_CUBIC]   EA of the 256 x 4 cubic coefficients (doubles, 16-aligned)
 *   arg[SYNTH_ARG_LINEAR]  EA of the 256 x 2 linear coefficients
 *   arg[SYNTH_ARG_SINCOS]  EA of the filters' sin/cos table (8 bytes per entry, 4-aligned)
 *   arg[SYNTH_ARG_SINCOS_N] its entries
 * The SPU zeroes the lines, renders voice after voice (skipping those whose plan says so) and
 * puts the lines and the voices (their new state and count) back. Every sample, table entry
 * and coefficient it reads is in the plan's ranges, fetched by DMA (the PPE computed them and
 * touched their pages). Errors: -EINVAL (22) for arguments outside this, -EIO (5) for a voice
 * the DSP code can't finish (it would loop forever on the PPE too), -EFAULT (14) in host builds
 * with SK_CHECK for a read outside the plan's ranges. */
#ifndef SYNTH_JOB_H
#define SYNTH_JOB_H

#include <stdint.h>

enum {
	SYNTH_OP_BLOCK,
	SYNTH_NOPS
};

enum {
	SYNTH_ARG_VOICES,
	SYNTH_ARG_COUNT,
	SYNTH_ARG_OUT,
	SYNTH_ARG_LINES,
	SYNTH_ARG_CUBIC,
	SYNTH_ARG_LINEAR,
	SYNTH_ARG_SINCOS,
	SYNTH_ARG_SINCOS_N,
};

#define SYNTH_JOB_MAX	64	/* voices per job (local store: 64 x 384 bytes) */
#define SYNTH_WIN_MAX	4096	/* samples per window: a pitch up to ~60x the sample's rate */
#define SYNTH_TAB_MAX	1024	/* sin/cos entries per filter and block (cents swept) */
#define SYNTH_INTERP_ROWS 256	/* FLUID_INTERP_MAX */

/* libspeq's part of a voice (struct speq_synth_voice.priv): what the job reads besides it */
struct synth_plan {
	uint32_t win_first[2], win_n[2];	/* samples: window 0 until the first loop wrap, 1 after */
	uint32_t tab_first[2], tab_n[2];	/* sin/cos entries: custom filter, resonant filter */
	int32_t pt[5];				/* fluid_rvoice_get_sample16 of start, end, loopstart,
						 * loopstart + 1, loopend - 1 (those the DSP reads) */
	uint32_t skip;				/* not in the job: count stays SPEQ_SYNTH_PPE */
};
enum { SYNTH_PT_START, SYNTH_PT_END, SYNTH_PT_LS, SYNTH_PT_LS1, SYNTH_PT_LE1 };

#endif
