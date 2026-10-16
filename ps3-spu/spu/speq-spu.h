/* The SPU side of libspeq: what an SPU program links against (libspeq-spu.a gives main and
 * the claim loop; the program gives speq_ops[]). On the SPU the primitives are the MFC
 * channels; built for the host (SPEQ_FAKE, the tests) they are emulated by lib/fake.c, and
 * the program runs on a thread per "SPE", so its state must be SPEQ_LOCAL.
 *
 * DMA rules (the MFC's): sizes 1, 2, 4, 8, 16 or a multiple of 16 up to 16 KB; local store
 * and EA 16-byte aligned (small sizes: naturally aligned, same low 4 bits); 128-byte
 * alignment for full speed. Tags 0..30 are the program's; 31 is the runtime's. */
#ifndef SPEQ_SPU_H
#define SPEQ_SPU_H

#include <stdint.h>
#include "speq-abi.h"
#include "speq.h"	/* struct speq_job, struct speq_done */

#define SPEQ_RT_TAG		31
#define SPEQ_OP_TAGS		0x7fffffffu	/* waited for before a job completes */
#define SPEQ_NR_getpid		20
#define SPEQ_NR_futex		221
#define SPEQ_FUTEX_WAKE_PRIVATE	129

/* One operation: runs a job (a copy in local store) and returns 0 or an error for the done
 * word (-errno, or the program's own code). Its output DMAs need not be waited for. */
typedef int (*speq_op_fn)(const struct speq_job *job);
extern const speq_op_fn speq_ops[];
extern const unsigned speq_nops;

/* From the runtime: this worker's index, the decrementer's frequency */
unsigned speq_worker(void);
uint32_t speq_tb_hz(void);

#ifdef __SPU__
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <sys/linux_syscalls.h>

#define SPEQ_LOCAL
#define SPEQ_MAIN main

static inline void speq_get(volatile void *ls, uint64_t ea, uint32_t size, unsigned tag)
{
	mfc_get(ls, ea, size, tag, 0, 0);
}
static inline void speq_put(volatile void *ls, uint64_t ea, uint32_t size, unsigned tag)
{
	mfc_put(ls, ea, size, tag, 0, 0);
}
static inline void speq_wait_tags(uint32_t mask)
{
	mfc_write_tag_mask(mask);
	mfc_read_tag_status_all();
}
/* 128 bytes, with a reservation; waits for it */
static inline void speq_getllar(volatile void *ls, uint64_t ea)
{
	mfc_getllar(ls, ea, 0, 0);
	mfc_read_atomic_status();
}
/* 0: stored; nonzero: the reservation was lost, nothing stored */
static inline int speq_putllc(volatile void *ls, uint64_t ea)
{
	mfc_putllc(ls, ea, 0, 0);
	return mfc_read_atomic_status() & MFC_PUTLLC_STATUS;
}
static inline uint32_t speq_dec_read(void) { return spu_read_decrementer(); }
static inline void speq_dec_write(uint32_t v) { spu_write_decrementer(v); }
static inline void speq_event_mask(uint32_t m) { spu_write_event_mask(m); }
/* Blocks in rdch SPU_RdEventStat until an enabled event is pending, acks it, returns it */
static inline uint32_t speq_event_wait(void)
{
	uint32_t ev = spu_read_event_status();

	spu_write_event_ack(ev);
	return ev;
}
/* Acks whatever is pending without blocking */
static inline void speq_event_drain(void)
{
	if (spu_stat_event_status())
		spu_write_event_ack(spu_read_event_status());
}
/* stop 0x2104: the kernel runs the syscall on the worker's PPE thread and restarts us */
static inline long long speq_syscall(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
				     uint64_t a3, uint64_t a4, uint64_t a5)
{
	struct spu_syscall_block s = { nr, { a0, a1, a2, a3, a4, a5 } };

	__linux_syscall(&s);
	return (long long)s.nr_ret;
}
/* stop 0x2110: the PPE thread parks; spu_run from npc continues after it */
static inline void speq_stop_park(void)
{
	__asm__ volatile ("stop 0x2110" ::: "memory");
}
static inline void speq_ibox_write(uint32_t v) { spu_write_out_intr_mbox(v); }

#define SPEQ_LLR_LOST_EVENT	MFC_LLR_LOST_EVENT
#define SPEQ_DECREMENTER_EVENT	MFC_DECREMENTER_EVENT

#else	/* host emulation (lib/fake.c) */

#define SPEQ_LOCAL __thread
#define SPEQ_MAIN speq_fake_main
int speq_fake_main(unsigned long long speid, unsigned long long argp, unsigned long long envp);

void speq_get(volatile void *ls, uint64_t ea, uint32_t size, unsigned tag);
void speq_put(volatile void *ls, uint64_t ea, uint32_t size, unsigned tag);
void speq_wait_tags(uint32_t mask);
void speq_getllar(volatile void *ls, uint64_t ea);
int speq_putllc(volatile void *ls, uint64_t ea);
uint32_t speq_dec_read(void);
void speq_dec_write(uint32_t v);
void speq_event_mask(uint32_t m);
uint32_t speq_event_wait(void);
void speq_event_drain(void);
long long speq_syscall(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
		       uint64_t a4, uint64_t a5);
void speq_stop_park(void);
void speq_ibox_write(uint32_t v);

#define SPEQ_LLR_LOST_EVENT	0x400
#define SPEQ_DECREMENTER_EVENT	0x020

#endif

#endif
