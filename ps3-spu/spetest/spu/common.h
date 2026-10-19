/* Shared by the SPU test programs: start-up (control block, the go word), the failure list and
 * the end (result block). A program is main(slot, ctl EA, seed) and returns 0 when nothing failed. */
#ifndef SPT_SPU_COMMON_H
#define SPT_SPU_COMMON_H

#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <stdint.h>
#include "../proto.h"
#include "../gen.h"

extern struct spt_ctl ctl;
extern struct spt_res res;
extern uint64_t ctl_ea;

#define EA64(hi, lo) (((uint64_t)(hi) << 32) | (lo))

/* DMA the control block in, then wait for the PPE's go word (it has read our physical SPE id by
 * then). */
void t_begin(unsigned long long slot, unsigned long long ea, unsigned test);
void t_fail(unsigned sub, uint32_t addr, uint32_t got, uint32_t want);
/* put the result block back; returns the exit code */
int t_end(void);
/* stress: true until run_ticks have passed since t_begin (at least one pass is always made) */
int t_more(void);
/* decrementer ticks since t_begin */
uint32_t t_elapsed(void);
void t_rebase(uint32_t spent);

static inline void out_msg(uint32_t w)
{
	spu_write_out_mbox(w);
}

/* 16-byte aligned LS bytes the program has not used: [t_free(), top of local store - stack) */
uint32_t t_free_start(void);
uint32_t t_free_end(void);

#endif
