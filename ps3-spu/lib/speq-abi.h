/* The PPE/SPU protocol of libspeq, shared by the library and the SPU programs (spu-elf GCC):
 * macros only. The spufs constants below are the kernel's (arch/powerpc/include/asm/spu.h);
 * they are not in the uapi headers. */
#ifndef SPEQ_ABI_H
#define SPEQ_ABI_H

/* Bumped on any change to the control block, the job lines or the stop codes; an SPU program
 * carries it, and speq_open refuses one built for another version (-EPROTO) */
#define SPEQ_ABI_VERSION	1

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

/* Control block: 128-byte lines. Line 0: tail|parked in one word, head, quit, abi; then one
 * "held job" slot per worker, then the job ring */
#define SPEQ_LINE		128
#define SPEQ_JOBS		64
#define SPEQ_PARKED		0x80000000u	/* in the tail word */
#define SPEQ_WAITERS		0x80000000u	/* in speq_done.pending */

#endif
