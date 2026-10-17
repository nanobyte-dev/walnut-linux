/* deadline.spu: the deadline pool's worker (lib/pool.c, lib/pool-job.h). Links libspeq-spu.a
 * (main, the claim loop, here in deadline order) and the ops of the latency-bound services:
 * fluidsynth's voice DSP (spu/synth-op.c) and vertex skinning (spu/skin-op.c). A worker runs one
 * job at a time, so their buffers share one arena; an op keeps nothing in it from one of its
 * jobs to the next. */
#include "deadline.h"

SPEQ_LOCAL uint8_t pool_arena[POOL_ARENA_BYTES] __attribute__((aligned(128)));

#ifdef __SPU__
SPEQ_PROGRAM(deadline, POOL_PROG, POOL_NOPS) = {
	[POOL_OP_SYNTH] = pool_op_synth,
	[POOL_OP_SKIN] = pool_op_skin,
};
#else
/* host builds (the fake backend): a job as long as the test wants, for the deadline tests */
static int op_test_spin(const struct speq_job *j)
{
	uint32_t t0 = speq_dec_read(), n = (uint64_t)j->arg[0] * speq_tb_hz() / 1000000;

	while (t0 - speq_dec_read() < n)
		;
	return 0;
}

SPEQ_PROGRAM(deadline, POOL_PROG, POOL_NOPS + 1) = {
	[POOL_OP_SYNTH] = pool_op_synth,
	[POOL_OP_SKIN] = pool_op_skin,
	[POOL_OP_TEST_SPIN] = op_test_spin,
};
#endif
