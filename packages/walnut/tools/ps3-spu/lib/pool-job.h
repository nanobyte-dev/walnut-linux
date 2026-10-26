/* The deadline pool's SPU program, deadline.spu (spu/deadline.c), shared with the PPE side
 * (lib/pool.c and the services). One program holds the operations of every latency-bound
 * service (fluidsynth's voice DSP, vertex skinning; OpenAL's mixer would join them), so one
 * queue of SPEQ_DEADLINE_SPES workers serves them all and a process never runs more SPU
 * contexts than there are SPEs. The op numbers are the pool's; each op's job layout is in its
 * service's header (synth-job.h, skin-job.h).
 *
 * Local store: a worker runs one job at a time, so the ops' buffers overlay in one arena
 * (POOL_ARENA_BYTES): synth's 118 KB (entries, output, sample windows, scratch) and skinning's
 * 99 KB (palette, streams, output). What an op keeps between its jobs (synth's coefficient
 * tables) lives outside it. Code ~46 KB, the runtime ~3 KB, the stack the rest (~80 KB). */
#ifndef POOL_JOB_H
#define POOL_JOB_H

enum {
	POOL_OP_SYNTH,		/* synth-job.h SYNTH_OP_ROUND */
	POOL_OP_SKIN,		/* skin-job.h SKIN_OP_RANGE */
	POOL_NOPS,
	POOL_OP_TEST_SPIN = POOL_NOPS	/* fake builds only (tests/pool-test.c): spins arg[0] us */
};

#define POOL_PROG		"deadline.spu"
#define POOL_ARENA_BYTES	(120 * 1024)

#endif
