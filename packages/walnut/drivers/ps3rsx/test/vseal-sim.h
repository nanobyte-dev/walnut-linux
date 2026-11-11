/* vseal-sim.h: the SPU side of spu/vseal.c on the PC (vseal-test.c builds vseal.c with VSEAL_HOST).
 * The LS is an array; DMA goes through a model of the MFC that is as unhelpful as the MFC may be:
 *  - a command moves in 256-byte units, and commands interleave unit by unit;
 *  - any queued command may run next, except a fenced one (getf, putf), which waits for every earlier
 *    command of its own tag group, and a sync, which waits for every earlier command;
 *  - the newest eligible command goes first three times in four (old puts starve);
 *  - at every issue, a few units of whatever is eligible move; a tag wait runs units until the group is
 *    empty.
 * An optional hook (vs_sim_hook) runs between units: the open tests use it to change ciphertext that a
 * get is reading (a torn or changed copy). */
#ifndef VSEAL_SIM_H
#define VSEAL_SIM_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VS_UNIT 256
#define VS_MAXQ 128

static uint8_t vs_ls[VSEAL_LS_SIZE] __attribute__((aligned(128)));
#define LSP(off) ((void *)(vs_ls + (off)))

struct vs_cmd {
	int kind;	/* 0 get, 1 put, 2 sync */
	int fenced, tag, done;
	uint8_t *ls, *ea;
	uint32_t size, moved;
	unsigned long seq;
};

static struct vs_cmd vs_q[VS_MAXQ];
static int vs_nq;
static unsigned long vs_seq;
static uint32_t vs_rs = 1, vs_dec;
static uint32_t vs_irq_value;
static int vs_irq_count, vs_overflow;
static void (*vs_sim_hook)(void);

static uint32_t vs_rnd(void)
{
	vs_rs ^= vs_rs << 13;
	vs_rs ^= vs_rs >> 17;
	vs_rs ^= vs_rs << 5;
	return vs_rs;
}

static int vs_eligible(int k)
{
	int j;

	if (vs_q[k].done)
		return 0;
	if (vs_q[k].kind == 2) {
		for (j = 0; j < vs_nq; j++)
			if (!vs_q[j].done && vs_q[j].seq < vs_q[k].seq)
				return 0;
		return 1;
	}
	if (!vs_q[k].fenced)
		return 1;
	for (j = 0; j < vs_nq; j++)
		if (!vs_q[j].done && vs_q[j].tag == vs_q[k].tag && vs_q[j].seq < vs_q[k].seq)
			return 0;
	return 1;
}

static void vs_compact(void)
{
	int i, n = 0;

	for (i = 0; i < vs_nq; i++)
		if (!vs_q[i].done)
			vs_q[n++] = vs_q[i];
	vs_nq = n;
}

/* one unit of one eligible command; 0 if nothing can move */
static int vs_step(void)
{
	int el[VS_MAXQ], n = 0, j;
	struct vs_cmd *c;
	uint32_t u;

	for (j = 0; j < vs_nq; j++)
		if (vs_eligible(j))
			el[n++] = j;
	if (!n)
		return 0;
	c = &vs_q[el[(vs_rnd() % 4) ? (uint32_t)(n - 1) : vs_rnd() % (uint32_t)n]];
	if (c->kind == 2) {
		c->done = 1;
	} else {
		u = c->size - c->moved < VS_UNIT ? c->size - c->moved : VS_UNIT;
		if (c->kind == 0)
			memcpy(c->ls + c->moved, c->ea + c->moved, u);
		else
			memcpy(c->ea + c->moved, c->ls + c->moved, u);
		c->moved += u;
		if (c->moved == c->size)
			c->done = 1;
	}
	if (vs_sim_hook)
		vs_sim_hook();
	return 1;
}

static void vs_issue(int kind, int fenced, void *ls, uint64_t ea, uint32_t size, int tag)
{
	int k;

	vs_compact();
	if (vs_nq == VS_MAXQ) {
		vs_overflow = 1;
		return;
	}
	if (kind != 2 && (size < 16 ? (size != 1 && size != 2 && size != 4 && size != 8) ||
			  ((uintptr_t)ls % size) || (ea % size) || (((uintptr_t)ls ^ ea) & 15) :
			  ((uintptr_t)ls & 15) || (ea & 15) || (size & 15) || size > 16384)) {
		fprintf(stderr, "vseal-sim: a DMA the MFC would refuse: ls %p ea %#llx size %u\n", ls,
			(unsigned long long)ea, size);
		abort();
	}
	vs_q[vs_nq++] = (struct vs_cmd){ kind, fenced, tag, 0, ls, (uint8_t *)(uintptr_t)ea, size, 0, vs_seq++ };
	for (k = vs_rnd() % 4; k > 0; k--)
		vs_step();
}

static void vs_wait(uint32_t mask)
{
	for (;;) {
		int j, busy = 0;

		for (j = 0; j < vs_nq; j++)
			if (!vs_q[j].done && (mask >> vs_q[j].tag & 1))
				busy = 1;
		if (!busy)
			return;
		if (!vs_step()) {
			fprintf(stderr, "vseal-sim: a wait that can never finish\n");
			abort();
		}
	}
}

static void vs_drain(void)
{
	while (vs_step())
		;
	vs_compact();
}

#define DMA_GET(ls, ea, n, t)	vs_issue(0, 0, (void *)(ls), (ea), (n), (t))
#define DMA_GETF(ls, ea, n, t)	vs_issue(0, 1, (void *)(ls), (ea), (n), (t))
#define DMA_PUT(ls, ea, n, t)	vs_issue(1, 0, (void *)(ls), (ea), (n), (t))
#define DMA_PUTF(ls, ea, n, t)	vs_issue(1, 1, (void *)(ls), (ea), (n), (t))
#define DMA_SYNC(t)		vs_issue(2, 0, vs_ls, 0, 0, (t))
#define DMA_WAIT(mask)		vs_wait(mask)
#define MBOX_READ()		0
#define MBOX_IRQ_WRITE(v)	do { vs_irq_value = (v); vs_irq_count++; } while (0)
#define DEC_READ()		(vs_dec -= 1000)
#define DEC_START()		(vs_dec = 0xffffffffu)

#endif
