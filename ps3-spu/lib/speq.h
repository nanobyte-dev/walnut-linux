/* libspeq: work queues on the Cell's SPEs (resident SPU workers fed through spufs), and the
 * services built on them. Every function returns 0 (or a count) on success and -errno on
 * failure; callers fall back to their own code on any error and never retry.
 * Errors: ENOSYS (not built/not there yet), ENOENT (no spufs or SPU program), EPROTO (SPU
 * program of another ABI), EBUSY (no SPE free), ENOTSUP (request outside what is offloaded). */
#ifndef SPEQ_H
#define SPEQ_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Work queue (milestone C; until then every call returns -ENOSYS) --- */

struct speq;

/* One job: a 128-byte line of the queue, read by the SPU worker that claims it */
struct speq_job {
	uint32_t op;		/* operation, defined by the SPU program */
	uint32_t flags;
	uint64_t done;		/* effective address of the struct speq_done to complete */
	uint64_t arg[14];	/* defined by the SPU program: addresses, sizes */
} __attribute__((aligned(128)));

/* Completion of a group of jobs, alone on its 128-byte line; zero it before the first submit */
struct speq_done {
	uint32_t pending;	/* jobs not finished (low 31 bits) | waiters (bit 31) */
	int32_t error;		/* first failure: -errno, or an SPU program's code */
	uint32_t pad[30];
} __attribute__((aligned(128)));

/* Starts nworkers (0: one per SPE) workers running SPU program prog (a path, or a name in
 * /usr/lib/ps3/spu); flags: SPEQ_OPEN_* (none defined yet) */
int speq_open(struct speq **q, const char *prog, unsigned nworkers, unsigned flags);
/* Queues n jobs; each counts on done (job.done is filled in) */
int speq_submit(struct speq *q, const struct speq_job *jobs, unsigned n, struct speq_done *done);
/* Waits until done has no pending jobs; returns done->error */
int speq_wait(struct speq *q, struct speq_done *done);
/* speq_submit of one job, then speq_wait */
int speq_run(struct speq *q, const struct speq_job *job);
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
 * -ENOSYS (SPEQ_S3TC=spu before the SPU path exists), -EINVAL; dst is then untouched.
 * Env: SPEQ_S3TC=off|ppe|spu (default ppe), SPEQ_DEBUG=1 (one line per call on stderr). */
int speq_s3tc_compress_v1(unsigned fmt, unsigned srccomps, const void *src, unsigned w,
			  unsigned h, void *dst, unsigned dst_stride);

#ifdef __cplusplus
}
#endif

#endif
