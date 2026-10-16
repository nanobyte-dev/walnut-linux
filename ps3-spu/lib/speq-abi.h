/* The PPE/SPU protocol of libspeq, shared by the library and the SPU programs (spu-elf GCC):
 * constants and the layout of the control block. The spufs constants below are the kernel's
 * (arch/powerpc/include/asm/spu.h); they are not in the uapi headers.
 *
 * The control block (posix_memalign 128, mlocked) is a row of 128-byte lines, the unit of the
 * SPU's getllar/putllc and of the PPE's lwarx/stwcx reservation:
 *   line 0       struct speq_ctl: tail|parked (one word), head, quit, abi, tunables
 *   lines 1..8   struct speq_wline: one per worker, written only by its SPU (the held job)
 *   lines 9..72  the job ring: struct speq_job (speq.h), SPEQ_JOBS of them
 * Every word one side changes atomically lives in the first 8 bytes of its line. */
#ifndef SPEQ_ABI_H
#define SPEQ_ABI_H

#include <stdint.h>

/* Bumped on any change to the control block, the job lines or the stop codes; an SPU program
 * carries it (section .speq_abi), and speq_open refuses one built for another version
 * (-EPROTO). 1 was milestone B's stubs; 2 is the first runtime. */
#define SPEQ_ABI_VERSION	2

/* spu_create flags. EVENTS_ENABLED is mandatory: without it an SPU fault is a signal to the
 * whole process; with it spu_run returns and reports the event (the context is then dead). */
#define SPU_CREATE_EVENTS_ENABLED	0x0001
#define SPU_CREATE_GANG			0x0002
#define SPU_CREATE_NOSCHED		0x0004
#define SPU_CREATE_ISOLATE		0x0008
#define SPU_CREATE_AFFINITY_SPU		0x0010
#define SPU_CREATE_AFFINITY_MEM		0x0020

/* spu_run's event word */
#define SPE_EVENT_DMA_ALIGNMENT		0x0008
#define SPE_EVENT_SPE_ERROR		0x0010
#define SPE_EVENT_SPE_DATA_SEGMENT	0x0020
#define SPE_EVENT_SPE_DATA_STORAGE	0x0040
#define SPE_EVENT_INVALID_DMA		0x0800

/* spu_run's status: stopped by "stop" with the code in the upper half */
#define SPU_STATUS_STOPPED_BY_STOP	0x2
#define SPU_STOP_CODE(status)		((status) >> 16)

/* Stop codes: 0x20xx is exit(xx) (newlib), 0x2100 the PPE-assisted call (not served),
 * 0x2104 a syscall the kernel runs on the worker's thread (futex among them),
 * 0x2110 a worker parking itself (idle) */
#define SPEQ_STOP_EXIT_MASK	0xff00
#define SPEQ_STOP_EXIT		0x2000
#define SPEQ_STOP_PPE_CALL	0x2100
#define SPEQ_STOP_SYSCALL	0x2104
#define SPEQ_STOP_PARK		0x2110
/* exit codes of the runtime: 0 after quit, these when it can't run */
#define SPEQ_EXIT_ABI		0x7e	/* the control block is of another ABI */

#define SPEQ_LINE		128
#define SPEQ_JOBS		64	/* job ring entries */
#define SPEQ_MAX_WORKERS	8	/* parked bits; the PS3 gives Linux 6 SPEs */

/* tail|parked: the tail (next job to fill, 24 bits) above a bitmask of parked workers. One
 * word, so a submit bumps the tail and takes the parked mask in one lwarx/stwcx; it is also
 * the futex parked worker threads sleep on. */
#define SPEQ_TAIL_SHIFT		8
#define SPEQ_PARKED_MASK	0xffu
#define SPEQ_SEQ_MASK		0xffffffu	/* tail and head count modulo 2^24 */
#define SPEQ_WAITERS		0x80000000u	/* in speq_done.pending: a waiter sleeps on it */

#define SPEQ_WLINE_OFF(i)	(SPEQ_LINE * (1 + (i)))
#define SPEQ_RING_OFF		(SPEQ_LINE * (1 + SPEQ_MAX_WORKERS))
#define SPEQ_CB_SIZE		(SPEQ_RING_OFF + SPEQ_LINE * SPEQ_JOBS)

/* Line 0 */
struct speq_ctl {
	uint32_t tail_parked;	/* tail << SPEQ_TAIL_SHIFT | parked: PPE lwarx/stwcx, SPU putllc */
	uint32_t head;		/* next job to claim: SPU putllc only */
	uint32_t quit;		/* set by speq_close: workers exit */
	uint32_t abi;		/* SPEQ_ABI_VERSION */
	uint32_t nworkers;
	uint32_t tb_hz;		/* timebase = decrementer frequency (/proc/cpuinfo) */
	uint32_t idle_ticks;	/* idle time before a worker parks, decrementer ticks; 0: never */
	uint32_t poke;		/* a word the PPE may store to (speq-bench P1); no meaning */
	uint32_t pad[24];
};

/* Lines 1..8: worker i's line, put by its SPU at every claim and completion. held is nonzero
 * while it runs a job, so the PPE can fail that job if the context dies. */
struct speq_wline {
	uint32_t held;		/* 1 while running job seq, else 0 */
	uint32_t seq;		/* the job's ring sequence number */
	uint64_t done;		/* its speq_done EA */
	uint32_t claims;	/* jobs claimed */
	uint32_t wakes;		/* returns from the event wait */
	uint32_t spurious;	/* ... that found nothing to do (no work, no quit, no timer) */
	uint32_t lost;		/* claims lost to another worker (putllc failed) */
	uint32_t parks;		/* stop 0x2110s */
	uint32_t timer;		/* wakes by the decrementer */
	uint32_t pad[22];
};

#endif
