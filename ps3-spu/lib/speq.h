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

/* Starts nworkers (0: one per SPE) workers running SPU program prog (a path, or a name in
 * /usr/lib/ps3/spu); flags: SPEQ_OPEN_* (none defined yet) */
int speq_open(struct speq **q, const char *prog, unsigned nworkers, unsigned flags);
/* Queues n jobs; each counts on done (job.done is filled in). Any number: past the ring's 64
 * it waits for room. Callable from several threads. */
int speq_submit(struct speq *q, const struct speq_job *jobs, unsigned n, struct speq_done *done);
/* Waits until done has no pending jobs; returns done->error (the first failure). Several
 * threads may wait on one done word. */
int speq_wait(struct speq *q, struct speq_done *done);
/* speq_submit of one job, then speq_wait */
int speq_run(struct speq *q, const struct speq_job *job);
/* Stops the workers (a running job finishes first) and fails what is still queued */
void speq_close(struct speq *q);

/* The PPE/SPU protocol version (control block layout, job lines): an SPU program built for
 * another one is refused with -EPROTO */
unsigned speq_abi_version(void);

/* --- S3TC/DXT texture compression --- */

#define SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT	0x83F0
#define SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT	0x83F1

/* Compresses a w x h image of srccomps (3: RGB, 4: RGBA) bytes per pixel, tightly packed
 * (srccomps * w bytes per row), to fmt (the GL enum: DXT1 RGB or RGBA) at dst, one row of
 * 4x4 blocks every dst_stride bytes (dst_stride < 2 * w: rows packed, as Mesa's
 * tx_compress_dxt1 takes it). RGBA DXT1: alpha <= 127 is transparent.
 * Returns 0, or -ENOTSUP (DXT3/5, srccomps not 3/4, w or h not a multiple of 4, SPEQ_S3TC=off),
 * -EINVAL, or with SPEQ_S3TC=spu the error of a queue that can't open (-ENOENT, -EBUSY, ...);
 * dst is then untouched. A failed SPU job never shows: the image is redone on the PPE.
 * Env: SPEQ_S3TC=auto|spu|ppe|off (default auto: the SPEs for RGBA images of at least
 * SPEQ_S3TC_MIN pixels, a count or WxH, default 64x64, when dxt.spu's queue opens; the PPE
 * otherwise), SPEQ_DEBUG=1 (one line per call on stderr). Safe to call from several threads. */
int speq_s3tc_compress_v1(unsigned fmt, unsigned srccomps, const void *src, unsigned w,
			  unsigned h, void *dst, unsigned dst_stride);

#ifdef __cplusplus
}
#endif

#endif
