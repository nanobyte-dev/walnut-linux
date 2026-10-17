/* deadline.spu's parts (spu/deadline.c): the arena the ops' buffers overlay and the ops */
#ifndef DEADLINE_H
#define DEADLINE_H

#include "pool-job.h"
#include "speq-spu.h"

extern SPEQ_LOCAL uint8_t pool_arena[POOL_ARENA_BYTES] __attribute__((aligned(128)));

int pool_op_synth(const struct speq_job *j);	/* spu/synth-op.c */
int pool_op_skin(const struct speq_job *j);	/* spu/skin-op.c */

#endif
