/* An interpreter of the SPU instruction set, enough to run spetest's programs on any host:
 * the 218 instructions of binutils' table, channels, mailboxes, signals, decrementer and an MFC
 * that moves data at once (so ordering faults cannot show). Single precision follows the SPU
 * model of ref.c, double precision is the host's. It is a test aid for the runner and the
 * programs' logic, not a model of the Cell's timing or of its failures. Fault injection for
 * the runner's tests: struct sim_fault. Debugging: SPT_SIM_DEBUG=1 explains MFC errors,
 * SPT_SIM_TRACE=N prints the first N instructions with their results, SPT_SIM_WATCH=ADDR
 * reports stores to that local store quadword. */
#ifndef SPUSIM_H
#define SPUSIM_H

#include <stddef.h>
#include <stdint.h>

struct sim_fault {
	int ls_addr;		/* a local store byte that reads back with ls_or ORed in (-1: none) */
	uint8_t ls_or;
	int hang;		/* the program never runs */
	int fp_bit;		/* flip a bit of the result of every fa */
	int dma_flip;		/* flip a bit in the data of every n-th DMA get (0: none) */
	int dec_div;		/* decrementer runs at 1/n of the rate (0: normal) */
	int reg_stuck;		/* register N bit 0 of word 0 reads as 1 after a write (-1: none) */
};

struct sim_spe;

struct sim_spe *sim_new(const uint8_t *elf, size_t size, uint64_t r3, uint64_t r4, uint64_t r5,
			const struct sim_fault *f, unsigned tb_hz);
/* runs until stop/halt/error/interrupt: the SPU status word, or -EINTR after sim_interrupt;
 * *event is set for a DMA error. */
long sim_run(struct sim_spe *s, unsigned *event);
void sim_interrupt(struct sim_spe *s);
int sim_mbox_put(struct sim_spe *s, uint32_t w);			/* 0, or -EAGAIN when full */
int sim_mbox_get(struct sim_spe *s, uint32_t *w);			/* 0, or -EAGAIN when empty */
int sim_ibox_get(struct sim_spe *s, uint32_t *w);
void sim_signal(struct sim_spe *s, int which, uint32_t v);
void sim_signal_type(struct sim_spe *s, int which, int or_mode);
void sim_set_fault(struct sim_spe *s, const struct sim_fault *f);
void sim_free(struct sim_spe *s);
uint64_t sim_icount(const struct sim_spe *s);

#endif
