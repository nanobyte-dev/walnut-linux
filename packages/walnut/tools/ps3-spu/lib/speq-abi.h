/* The PPE/SPU protocol of libspeq, shared by the library and the SPU programs (spu-elf GCC):
 * constants and the layout of the control block. The spufs constants below are the kernel's
 * (arch/powerpc/include/asm/spu.h); they are not in the uapi headers.
 *
 * The control block (posix_memalign 128, mlocked) is a row of 128-byte lines, the unit of the
 * SPU's getllar/putllc and of the PPE's lwarx/stwcx reservation:
 *   line 0       struct speq_ctl: tail|parked (one word), head, quit, abi, tunables
 *   lines 1..8   struct speq_wline: one per worker, written only by its SPU (the held job)
 *   lines 9..72  the job ring: struct speq_job (speq.h), SPEQ_JOBS of them
 *   lines 73..76 the ring's deadlines: one uint64_t per slot (deadline-ordered queues)
 * Every word one side changes atomically lives in the first 8 bytes of its line.
 *
 * Claim order. A queue in arrival order (ctl.order SPEQ_ORDER_FIFO, every queue but the
 * deadline pool) is claimed at head: a worker moves head past the job it takes. A queue in
 * deadline order (SPEQ_ORDER_DEADLINE: libspeq's deadline pool, lib/pool.c) is claimed by job:
 * the submitter writes each job's deadline into the slot's entry of the deadline lines, then
 * (after a barrier) the job's flags word as its sequence number; a worker looks at the jobs
 * from head on (SPEQ_SCAN at most), takes the one with the earliest deadline (the earliest
 * queued on a tie) by setting SPEQ_JOB_CLAIMED in its flags word with getllar/putllc (the
 * sequence number must still match: the slot can have been reused), then moves head past every
 * claimed job at head. A slot is reused only once head has passed it, so an unclaimed job stays
 * where it is until a worker takes it. A deadline is only a scheduling hint: a stale one picks
 * another job first, never a wrong one. A program built before the order field claims at head
 * in both orders, which stays correct (every worker of a queue runs the same program), just
 * in arrival order: no ABI step. */
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
#define SPEQ_DL_OFF		(SPEQ_RING_OFF + SPEQ_LINE * SPEQ_JOBS)
#define SPEQ_CB_SIZE		(SPEQ_DL_OFF + 8 * SPEQ_JOBS)

/* ctl.order */
#define SPEQ_ORDER_FIFO		0
#define SPEQ_ORDER_DEADLINE	1
/* A job's flags word in a deadline-ordered ring: its sequence number (SPEQ_SEQ_MASK), and
 * SPEQ_JOB_CLAIMED once a worker (or the PPE failing it) took it */
#define SPEQ_JOB_CLAIMED	0x80000000u
#define SPEQ_SCAN		16	/* jobs from head a worker compares */

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
	uint32_t order;		/* SPEQ_ORDER_*: how workers pick jobs */
	uint32_t pad[23];
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
