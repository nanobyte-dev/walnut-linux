/* libspeq: work queues on the Cell's SPEs (resident SPU workers fed through spufs), and the
 * services built on them. Every function returns 0 (or a count) on success and -errno on
 * failure; callers fall back to their own code on any error and never retry.
 * Errors: ENOSYS (not built/not there yet), ENOENT (no spufs or SPU program), EPROTO (SPU
 * program of another ABI), EBUSY (no SPE free), ENOTSUP (request outside what is offloaded),
 * EIO (the queue can't run jobs: its workers broke, or this is a fork child), EFAULT (a job's
 * DMA faulted; its context was recreated), ECANCELED (still queued at speq_close). */
#ifndef SPEQ_H
#define SPEQ_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Work queue ---
 * One resident SPU worker per SPE (never more), each fed by a PPE thread in spu_run. Idle
 * workers sleep in the SPU's event wait, park after SPEQ_IDLE_US (default 10000) and free
 * their context after SPEQ_FREE_MS parked (default 5000); the next submit wakes or recreates
 * them. speq_wait spins SPEQ_SPIN_US (default 80) before it sleeps on a futex. */

struct speq;

/* One job: a 128-byte line of the queue, read by the SPU worker that claims it */
struct speq_job {
	uint32_t op;		/* operation, defined by the SPU program */
	uint32_t flags;
	uint64_t done;		/* effective address of the struct speq_done to complete */
	uint64_t arg[14];	/* defined by the SPU program: addresses, sizes */
} __attribute__((aligned(128)));

/* Completion of a group of jobs, alone on its 128-byte line (128-aligned, or -EINVAL); zero it
 * before the first submit. The SPUs write it by DMA: keep it valid until speq_wait returns. */
struct speq_done {
	uint32_t pending;	/* jobs not finished (low 31 bits) | waiters (bit 31) */
	int32_t error;		/* first failure: -errno, or an SPU program's code */
	uint32_t pad[30];
} __attribute__((aligned(128)));

/* Starts nworkers workers running SPU program prog (a path, or a name in /usr/lib/ps3/spu),
 * never more than the SPEs. nworkers 0: the SPEs no other open queue of this process holds
 * (at least one), so services with their own queues share the six SPEs instead of having
 * spufs time-slice them (80 ms slices: past any audio deadline).
 * flags: SPEQ_OPEN_BULK, a queue of long jobs without deadlines (texture compression): with
 * nworkers 0 it also leaves SPEQ_DEADLINE_SPES to libspeq's deadline pool (the one queue of
 * every latency-bound service: audio, skinning; opened before or after it), so their jobs never
 * wait behind its jobs or for a time slice. */
#define SPEQ_OPEN_BULK		1u
#define SPEQ_DEADLINE_SPES	2
int speq_open(struct speq **q, const char *prog, unsigned nworkers, unsigned flags);
/* Queues n jobs; each counts on done (job.done is filled in). Any number: past the ring's 64
 * it waits for room. Callable from several threads. */
int speq_submit(struct speq *q, const struct speq_job *jobs, unsigned n, struct speq_done *done);
/* Waits until done has no pending jobs; returns done->error (the first failure). Several
 * threads may wait on one done word. */
int speq_wait(struct speq *q, struct speq_done *done);
/* speq_wait with a deadline and a choice of spinning. timeout_us < 0: no deadline; 0: a poll.
 * flags: SPEQ_WAIT_NOSPIN sleeps on the futex at once instead of spinning SPEQ_SPIN_US first
 * (a thread sharing the PPE core with a game: the spin would take the game's cycles; the sleep
 * costs ~62 us more latency, measured). Returns done->error once nothing is pending, or
 * -ETIMEDOUT: the jobs still run, and done and every buffer they use stay theirs until a later
 * speq_wait/speq_wait_timeout on done returns something else (keep them, or give them up
 * without freeing). */
#define SPEQ_WAIT_NOSPIN	1u
int speq_wait_timeout(struct speq *q, struct speq_done *done, long timeout_us, unsigned flags);
/* speq_submit of one job, then speq_wait */
int speq_run(struct speq *q, const struct speq_job *job);
/* Per-queue tunables (-1: unchanged): speq_wait's spin, the idle time before a worker parks
 * (SPEQ_IDLE_US; a service with a steady job rate sets it above its period, so its workers stay
 * resident), the time a parked worker keeps its context (SPEQ_FREE_MS) */
void speq_tune(struct speq *q, int spin_us, int idle_us, int free_ms);
/* The queue's workers (speq_open's choice) */
unsigned speq_nworkers(struct speq *q);
/* Stops the workers (a running job finishes first) and fails what is still queued */
void speq_close(struct speq *q);

/* The PPE/SPU protocol version (control block layout, job lines): an SPU program built for
 * another one is refused with -EPROTO */
unsigned speq_abi_version(void);

/* --- S3TC/DXT texture compression --- */

#define SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT	0x83F0
#define SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT	0x83F1
#define SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT5_EXT	0x83F3	/* an older libspeq: -ENOTSUP */

/* Compresses a w x h image of srccomps (3: RGB, 4: RGBA) bytes per pixel, tightly packed
 * (srccomps * w bytes per row), to fmt (the GL enum: DXT1 RGB or RGBA, DXT5 RGBA) at dst, one
 * row of 4x4 blocks every dst_stride bytes (dst_stride below a block row, 2 * w for DXT1 and
 * 4 * w for DXT5: rows packed, as Mesa's tx_compress_dxt1/5 take it). RGBA DXT1: alpha <= 127
 * is transparent. DXT5: its colour block never relies on the 3-colour mode (decoders read
 * DXT5's in 4-colour mode).
 * Returns 0, or -ENOTSUP (DXT3, DXT5 with SPEQ_S3TC_DXT5=off, srccomps not 3/4, w or h not a
 * multiple of 4, SPEQ_S3TC=off, an RGB source with SPEQ_S3TC_RGB=off), -EINVAL, or with
 * SPEQ_S3TC=spu the error of a queue that can't open (-ENOENT, -EBUSY, ...) or broke (3 images in
 * a row failed); dst is then untouched. A failed SPU job never shows otherwise: the image is
 * redone on the PPE.
 * Env: SPEQ_S3TC=auto|spu|ppe|off (default auto: the SPEs for RGB and RGBA images of at least
 * SPEQ_S3TC_MIN pixels, a count or WxH, default 64x64, when dxt.spu's queue opens; the PPE
 * otherwise), SPEQ_S3TC_DXT5=off (DXT5 back to the caller's encoder), SPEQ_S3TC_RGB=ppe|off
 * (RGB sources to libspeq's PPE encoder, or back to the caller's), SPEQ_DEBUG=1 (one line per
 * call on stderr). Safe to call from several threads. */
int speq_s3tc_compress_v1(unsigned fmt, unsigned srccomps, const void *src, unsigned w,
			  unsigned h, void *dst, unsigned dst_stride);

/* --- MIDI synthesis: fluidsynth's voice DSP on the SPEs ---
 * fluidsynth keeps its control code (envelopes, LFOs, pitch, filter coefficients: its
 * fluid_rvoice_write up to the DSP); libspeq renders the DSP (the interpolation: none, linear,
 * 4th order; the custom and the resonant filter with the volume envelope; the mix into the
 * mixer's buffers) of a round of up to K blocks (SPEQ_SYNTH_BLOCK samples each) for all voices,
 * in single precision. The control part needs nothing the DSP computes from samples (the phase,
 * has_looped, the count and the filters' coefficients, cutoff, Q and gain after a block follow
 * from their values before it), so add runs that part of the DSP at once and hands the voice's
 * next state back: fluidsynth goes through every block of the round, then render does the rest
 * in one go, on the SPEs, split across the workers by voice. Per round:
 *   K = speq_synth_begin_v2(s, nvoices, blocks, sincos, n)   0: render these blocks yourself
 *   for each block b < K, each voice slot v < nvoices, after fluidsynth's control part:
 *       speq_synth_add_v2(s, v, b, &voice)    0: voice is its state after the block (all but
 *                                             the filters' histories); < 0 (only at a voice's
 *                                             first block in the round): render it yourself
 *   speq_synth_render_v2(s, &out, &hist)      out: K blocks x lines x 64 floats to add, hist:
 *                                             4 floats per voice slot (custom h1 h2, resonant
 *                                             h1 h2), the histories after the round
 * render always delivers: a job that fails or is late (SPEQ_SYNTH_TIMEOUT_US, 2000 + 5 per
 * voice block) is redone on the PPE from the round's entries. Policy is libspeq's:
 * SPEQ_SYNTH=auto|spu|ppe|off (auto, the default: the SPEs for rounds of at least SPEQ_SYNTH_MIN
 * voices (4), else begin returns 0; spu: every round; ppe: libspeq's DSP on the calling thread;
 * off: speq_synth_open_v2 fails), SPEQ_SYNTH_SPES (the most of the pool's workers a round is
 * split across, default all), SPEQ_SYNTH_SPIN_US (0: sleep at once), SPEQ_DEBUG=1 (a line of
 * counters every 10 s and at close). The SPEs: the process's deadline pool, shared with
 * skinning (SPEQ_POOL_SPES workers, 2: jobs taken by deadline). Repeated failures put the SPEs
 * aside for a while (begin returns 0); a pool that broke for good, for the process. One synth
 * per thread; synths share the pool. */

#define SPEQ_SYNTH_ABI		2	/* layout of the structs below (the _v2 calls) */
#define SPEQ_SYNTH_BLOCK	64	/* samples per block: FLUID_BUFSIZE */
#define SPEQ_SYNTH_BUFS		4	/* mix buffers per voice: FLUID_RVOICE_MAX_BUFS */
#define SPEQ_SYNTH_LINES	16	/* output lines at most */

enum {	/* struct speq_synth_voice.interp */
	SPEQ_SYNTH_NONE,		/* FLUID_INTERP_NONE */
	SPEQ_SYNTH_LINEAR,		/* FLUID_INTERP_LINEAR */
	SPEQ_SYNTH_CUBIC		/* FLUID_INTERP_4THORDER */
};
enum {	/* struct speq_synth_filter.type: fluid_iir_filter_type */
	SPEQ_SYNTH_IIR_OFF,
	SPEQ_SYNTH_IIR_LOWPASS,
	SPEQ_SYNTH_IIR_HIGHPASS
};
#define SPEQ_SYNTH_IIR_NO_GAIN	1u	/* .flags: FLUID_IIR_NO_GAIN_AMP (no 1/sqrt(Q) gain) */

/* fluid_iir_filter_t's DSP state */
struct speq_synth_filter {
	double hist1, hist2;		/* in: read at the voice's first block of a round */
	double last_fres, fres_incr;	/* cents */
	double last_q, q_incr;
	double amp, amp_incr;		/* the resonant filter's: the volume envelope */
	float b02, b1, a1, a2;
	int32_t fres_incr_count, q_incr_count;
	uint32_t type, flags;
};

/* A voice: fluid_rvoice_t after fluid_rvoice_write's control part; add writes the voice's
 * state after the block back (count, phase, has_looped, the filters but their histories,
 * cur_amp) */
struct speq_synth_voice {
	uint64_t data;			/* the int16 samples (fluid_sample_t.data); no 24-bit */
	uint64_t phase;			/* fluid_phase_t */
	double phase_incr;
	uint32_t start, end, loopstart, loopend;
	uint8_t interp, looping;	/* SPEQ_SYNTH_NONE..CUBIC; is_looping */
	uint8_t has_looped, pad0;
	int8_t line[SPEQ_SYNTH_BUFS];	/* each buffer's output line, -1: not mixed */
	double cur_amp[SPEQ_SYNTH_BUFS], tgt_amp[SPEQ_SYNTH_BUFS];
	struct speq_synth_filter custom, res;	/* custom filter first, as fluidsynth */
	int32_t count;			/* out: samples (fewer than a block: the sample ended) */
	uint32_t pad1;
};

struct speq_synth_params {
	uint32_t abi;			/* SPEQ_SYNTH_ABI */
	uint32_t voice_size;		/* sizeof(struct speq_synth_voice): 312 */
	uint32_t lines;			/* output lines (the mixer's buffers), <= SPEQ_SYNTH_LINES */
	uint32_t max_voices;		/* voice slots at most (0: 256) */
	const double *cubic;		/* 256 x 4 interpolation coefficients (interp_coeff), copied */
	const double *linear;		/* 256 x 2 (interp_coeff_linear), copied */
};

struct speq_synth;
/* 0, or -ENOTSUP (SPEQ_SYNTH=off), -EPROTO (another ABI), -EINVAL, or the queue's error
 * (-ENOENT, -EBUSY, ...): then render everything yourself */
int speq_synth_open_v2(struct speq_synth **s, const struct speq_synth_params *p);
/* sincos: the filters' table (float sin, cos for each cent from 1500: fluid_iir_sincos_t), n
 * entries; the same for every voice of the round */
unsigned speq_synth_begin_v2(struct speq_synth *s, unsigned nvoices, unsigned blocks,
			     const void *sincos, uint32_t sincos_n);
int speq_synth_add_v2(struct speq_synth *s, unsigned voice, unsigned block,
		      struct speq_synth_voice *v);
int speq_synth_render_v2(struct speq_synth *s, const float **out, const float **hist);
/* Waits for its jobs in flight (bounded), then frees it */
void speq_synth_close_v2(struct speq_synth *s);

/* --- Vertex skinning: a skeletal model's vertices moved by its bones, on the SPEs ---
 * What a game's CPU skinning does each frame before it uploads a vertex buffer: every vertex's
 * position, normal and tangent transformed by one palette entry (a dual quaternion or a 3x4
 * matrix: a bone, or a blend of bones the game made) or by a blend of up to 4 bones with 8-bit
 * weights. A batch is one output buffer, owned by libspeq, with any number of meshes at their
 * offsets in it (Sauerbraten: a mesh group's vertex buffer); the game uploads that buffer.
 *   speq_skin_begin_v1(s, out_bytes, nverts)   0: a batch; < 0: skin it yourself
 *   speq_skin_add_v1(s, &mesh)                 per mesh: its jobs start on the SPEs at once
 *   speq_skin_end_v1(s, &out)                  0: out holds every mesh added, each at its
 *                                              dst_offset; < 0: skin the batch yourself
 * Between begin and end the inputs (palettes, sources, templates) must stay as they are; after
 * an end that failed or timed out libspeq never hands out that batch's late results. out
 * stays valid (and writable) until the next begin on s. An add that fails spoils the batch:
 * end then returns its error.
 * Every field the op writes is replaced; the output's other bytes come from the template
 * (tmpl, dst_stride bytes per vertex: a game's interleaved buffer with its texture
 * coordinates), or are zero without one.
 * Policy is libspeq's: SPEQ_SKIN=auto|spu|ppe|off (auto, the default: the SPEs for batches of
 * at least SPEQ_SKIN_MIN vertices (256), else begin declines; spu: every batch; ppe: libspeq's
 * code on the calling thread, no SPEs; off: speq_skin_open_v1 fails), SPEQ_SKIN_SPES (the most
 * of the pool's workers a mesh is split across, default all), SPEQ_SKIN_TIMEOUT_US (end waits
 * 2000 + 0.05 per vertex, then declines; spinning SPEQ_SKIN_SPIN_US first, 300), SPEQ_DEBUG=1
 * (a line of counters every 10 s and at close). The SPEs: the process's deadline pool, shared
 * with the synth (SPEQ_POOL_SPES workers, 2: jobs taken by deadline). Three failed batches in a
 * row put the SPEs aside for 5 s; a pool that broke, for good. One speq_skin per thread; they
 * share the pool. */

#define SPEQ_SKIN_ABI		1	/* layout of struct speq_skin_mesh (the _v1 calls) */
#define SPEQ_SKIN_NONE		0xffff	/* an absent field's offset */

enum {	/* .palette_fmt */
	SPEQ_SKIN_DQ,		/* 8 floats: real x y z w, dual x y z w (Sauerbraten's dualquat) */
	SPEQ_SKIN_MAT		/* 12 floats: 3 rows of 4, x' = row0 . (x, y, z, 1) */
};
enum {	/* .influence */
	SPEQ_SKIN_IDX_S32,	/* the palette entry's index: int32 at index.off */
	SPEQ_SKIN_IDX_U16,
	SPEQ_SKIN_IDX_U8,
	SPEQ_SKIN_W4		/* 4 uint8 indices at index.off, 4 uint8 weights (/255) at
				 * weights.off: DQ blended and normalized as Sauerbraten blends,
				 * matrices summed */
};
enum {	/* .tan_in */
	SPEQ_SKIN_TAN_NONE,
	SPEQ_SKIN_TAN_VEC4,	/* float x y z rotated, w (handedness) kept */
	SPEQ_SKIN_TAN_QUAT	/* float quaternion of the tangent frame, DQ only: rotated (real *
				 * q) and its sign fixed as Sauerbraten's fixqtangent, w < 0 the
				 * bitangent's sign */
};
enum {	/* .tan_out */
	SPEQ_SKIN_OUT_F32,	/* 4 floats */
	SPEQ_SKIN_OUT_S16	/* 4 int16: (short)(x * 32767.5f - 0.5f), Sauerbraten's squat */
};

/* An input field: its stream (0, 1) and its byte offset in the stream's vertex */
struct speq_skin_field {
	uint16_t off;			/* SPEQ_SKIN_NONE: absent */
	uint8_t stream, pad;
};

/* One mesh. Positions and normals are 3 floats, tangents 4; every offset and stride a multiple
 * of 4, palettes 16-aligned, (tmpl - dst_offset) 16-aligned (the template at the output's
 * alignment); palette entries at most 1536 (DQ) or 1024 (MAT) in all. */
struct speq_skin_mesh {
	uint32_t nverts;
	uint8_t palette_fmt, influence, tan_in, tan_out;
	const void *palette[2];		/* entries [0, npal[0]) from palette[0], the next npal[1]
					 * from palette[1] (a game's bones, then its blends) */
	uint32_t npal[2];
	const void *src[2];		/* vertex streams */
	uint32_t src_stride[2];
	struct speq_skin_field pos, norm, tan, index, weights;	/* pos and index required */
	const void *tmpl;		/* nverts x dst_stride bytes, or NULL */
	uint32_t dst_offset;		/* the mesh's vertex 0 in the batch's output */
	uint32_t dst_stride;
	uint16_t dst_pos, dst_norm, dst_tan;	/* SPEQ_SKIN_NONE: not written */
	uint16_t pad;
};

struct speq_skin;
/* abi: SPEQ_SKIN_ABI, mesh_size: sizeof(struct speq_skin_mesh). 0, or -ENOTSUP (SPEQ_SKIN=off),
 * -EPROTO (another ABI), or the queue's error (-ENOENT, -EBUSY, ...): then skin yourself */
int speq_skin_open_v1(struct speq_skin **s, unsigned abi, unsigned mesh_size);
/* nverts: the batch's vertices (the auto policy's measure). -ENOTSUP: declined (small, the SPEs
 * set aside, no free buffer), -EINVAL, -ENOMEM */
int speq_skin_begin_v1(struct speq_skin *s, uint32_t out_bytes, uint32_t nverts);
/* 0, or -EINVAL / -ENOTSUP (a mesh outside the formats and limits above), -ENOSPC (too many
 * meshes in a batch), the queue's error */
int speq_skin_add_v1(struct speq_skin *s, const struct speq_skin_mesh *m);
/* 0 and *out; or the batch's error, -ETIMEDOUT (late), -EIO (the SPEs failed) */
int speq_skin_end_v1(struct speq_skin *s, void **out);
/* Waits for its batches in flight (bounded), then frees it */
void speq_skin_close_v1(struct speq_skin *s);

#ifdef __cplusplus
}
#endif

#endif
