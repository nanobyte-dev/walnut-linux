/* The synth op's job (spu/synth-op.c, in the deadline pool's deadline.spu: pool-job.h), shared
 * with the PPE side (lib/synth.c). A round is K blocks
 * of a synth's voices; its entries (struct synth_ent, one per voice and block, the voice's
 * state before that block) sit voice-major: voice slot v's block b at entries[v * K + b]. One
 * SYNTH_OP_ROUND job renders a range of voices, every block of each in order, carrying the
 * filters' histories, into the job's own output: K blocks x lines x 64 floats.
 *   arg[SYNTH_ARG_ENTS]      EA of the job's first entry (voice v0, block 0; 16-aligned)
 *   arg[SYNTH_ARG_VOICES]    voices in the job (v0 .. v0 + n - 1)
 *   arg[SYNTH_ARG_BLOCKS]    K
 *   arg[SYNTH_ARG_OUT]       EA of the job's output (128-aligned)
 *   arg[SYNTH_ARG_LINES]     lines per block
 *   arg[SYNTH_ARG_HIST]      EA of voice v0's 4 histories in (float: custom h1 h2, resonant h1
 *                            h2), then the others'; the job puts each voice's new ones at
 *                            arg[SYNTH_ARG_HIST_OUT] (same layout)
 *   arg[SYNTH_ARG_CUBIC]     EA of the 256 x 4 cubic coefficients (float, 16-aligned)
 *   arg[SYNTH_ARG_LINEAR]    EA of the 256 x 2 linear coefficients (float)
 *   arg[SYNTH_ARG_SINCOS]    EA of the filters' sin/cos table (8 bytes per entry, 4-aligned)
 *   arg[SYNTH_ARG_SINCOS_N]  its entries
 * The SPU never writes the entries or the histories in: a job given up on can be redone on the
 * PPE from them while it still runs. Every sample and table entry it reads is in an entry's
 * windows and slices (the PPE planned them and touched their pages). Errors: -EINVAL (22) for
 * arguments outside this, -EIO (5) for an entry whose runs leave its windows or disagree with
 * the PPE's count. */
#ifndef SYNTH_JOB_H
#define SYNTH_JOB_H

#include <stdint.h>
#include "pool-job.h"

#define SYNTH_OP_ROUND	POOL_OP_SYNTH

enum {
	SYNTH_ARG_ENTS,
	SYNTH_ARG_VOICES,
	SYNTH_ARG_BLOCKS,
	SYNTH_ARG_OUT,
	SYNTH_ARG_LINES,
	SYNTH_ARG_HIST,
	SYNTH_ARG_HIST_OUT,
	SYNTH_ARG_CUBIC,
	SYNTH_ARG_LINEAR,
	SYNTH_ARG_SINCOS,
	SYNTH_ARG_SINCOS_N,
};

#define SYNTH_JOB_ENTS	128	/* entries per job (local store: 32 KB) */
#define SYNTH_OUT_MAX	64	/* blocks x lines per job (local store: 16 KB) */
#define SYNTH_WIN_MAX	2048	/* samples per window: a pitch up to ~30x the sample's rate */
#define SYNTH_TAB_MAX	512	/* sin/cos entries per filter and block (cents swept) */
#define SYNTH_INTERP_ROWS 256	/* FLUID_INTERP_MAX */

/* A filter, in float as the DSP runs it (struct speq_synth_filter's values before the block) */
struct sk_filt {
	float b02, b1, a1, a2;
	float fres, fres_incr, q, q_incr;
	float amp, amp_incr;
	int16_t fcount, qcount;		/* smoothing counts */
	uint8_t type, flags, off;	/* off: disabled, or Q below Q_MIN: no filter, no gain */
	uint8_t pad;
};

enum { SYNTH_PT_SP, SYNTH_PT_EP1, SYNTH_PT_EP2, SYNTH_PT_LE1 };
#define SYNTH_ENT_SKIP	1u	/* no block here (quiet, finished) */
#define SYNTH_ENT_PPE	2u	/* the voice is rendered on the PPE: the SPU skips it */

/* One voice block (256 bytes) */
struct synth_ent {
	uint64_t data;			/* EA of the int16 samples */
	uint64_t phase, incr;		/* 32.32 */
	uint32_t start, end, loopstart, loopend;
	int32_t elo[2], en[2];		/* windows: sample index of the first entry, entries */
	float pt[4];			/* SYNTH_PT_*: start_point (window 0), end_point1, 2, the
					 * start point after a wrap (window 1): what fluidsynth's
					 * interpolation uses in place of a sample */
	uint32_t tab_first[2], tab_n[2];	/* sin/cos slices: custom filter, resonant filter */
	uint8_t interp, looping, has_looped, flags;
	int8_t line[4];			/* each buffer's output line, -1: not mixed */
	uint16_t count, pad0;		/* samples, as the PPE's control found */
	uint32_t pad1;
	float cur[4], tgt[4];		/* each buffer's gain before and after */
	struct sk_filt custom, res;
	uint8_t pad2[24];
} __attribute__((aligned(16)));

#endif
