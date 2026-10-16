/* The SPE work queue: resident SPU workers fed through spufs. Stubs until milestone C; every
 * caller already handles -ENOSYS by doing the work itself. */
#include <errno.h>
#include <stddef.h>
#include "speq-abi.h"
#include "speq.h"

int speq_open(struct speq **q, const char *prog, unsigned nworkers, unsigned flags)
{
	if (q)
		*q = NULL;
	return -ENOSYS;
}

int speq_submit(struct speq *q, const struct speq_job *jobs, unsigned n, struct speq_done *done)
{
	return -ENOSYS;
}

int speq_wait(struct speq *q, struct speq_done *done)
{
	return -ENOSYS;
}

int speq_run(struct speq *q, const struct speq_job *job)
{
	return -ENOSYS;
}

void speq_close(struct speq *q)
{
}

unsigned speq_abi_version(void)
{
	return SPEQ_ABI_VERSION;
}
