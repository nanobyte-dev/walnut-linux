/* spetest: what the runner (PPE) and the test programs (SPU) agree on.
 * Every field is a 32-bit word and the blocks are big-endian on the wire (the SPU's byte order),
 * so a little-endian host (the unit tests) swaps whole blocks with spt_swap_words(). */
#ifndef SPT_PROTO_H
#define SPT_PROTO_H

#include <stdint.h>

#define SPT_MAGIC	0x53505431u	/* "SPT1" */
#define SPT_GO		0x60000000u	/* PPE -> SPU first mailbox word: | test id */
#define SPT_LS_SIZE	0x40000u
#define SPT_STACK_RESERVE 0x800u	/* top of the local store left to the stack */

enum spt_test { T_LS, T_REG, T_EXEC, T_CHAN, T_MFC, T_N };

#define SPT_F_STRESS	1u	/* loop until run_ticks have passed */

/* PPE -> SPU: the SPU DMAs it in at start (256 bytes, 128-aligned in main memory). The result
 * block follows at +256. */
struct spt_ctl {
	uint32_t magic, test, iters, run_ticks;
	uint32_t seed, flags, slot, nspe;
	uint32_t tb_hz;
	uint32_t out_hi, out_lo, out_len;	/* a work/output buffer (128-aligned) */
	uint32_t src_hi, src_lo, src_len;	/* read-only pattern buffer (128-aligned) */
	uint32_t line_hi, line_lo;		/* a 128-byte line for the atomic ops */
	uint32_t spare[47];
};

#define SPT_NFAIL	8
#define SPT_NSUB	12

/* SPU -> PPE (put at the end of the run) */
struct spt_res {
	uint32_t magic, test, state;	/* state 1: the program ran to its end */
	uint32_t passes, nfail;		/* passes completed, failures counted */
	uint32_t sub[SPT_NSUB];		/* failures per sub-test */
	struct { uint32_t what, addr, got, want; } f[SPT_NFAIL];	/* the first ones */
	uint32_t aux[8];		/* test specific (ticks, digests) */
	uint32_t spare[7];
};

_Static_assert(sizeof(struct spt_ctl) == 256, "spt_ctl is 256 bytes");
_Static_assert(sizeof(struct spt_res) == 256, "spt_res is 256 bytes");

#define SPT_CTL_BYTES	256
#define SPT_RES_BYTES	256
#define SPT_OFF_RES	256

/* main() of every test: (slot, ctl EA, seed) as libspeq's workers get them */

/* ---- sub-test codes (res.sub[] index, f[].what) ---- */
/* ls */
enum { LS_MARCH, LS_ADDR, LS_RAND, LS_SETUP };
/* reg */
enum { REG_ZERO, REG_ONES, REG_55, REG_AA, REG_INDEX, REG_UNIQUE, REG_WALK1, REG_WALK0,
       REG_RAND1, REG_RAND2, REG_RAND3, REG_SETUP };
/* exec: SPT_NSUB is small, so exec reports per op class through aux/digests; sub[] holds */
enum { EX_INT, EX_SP, EX_DP, EX_SHUF, EX_BRANCH, EX_MEM, EX_SELF };
/* chan */
enum { CH_MBOX, CH_SIG, CH_DEC, CH_DECEV, CH_RATE };
/* mfc */
enum { MF_GET, MF_PUT, MF_SMALL, MF_LIST, MF_FENCE, MF_TAGS, MF_ATOMIC, MF_ALIGN };

/* ---- the channels and atomic test's messages (SPU -> PPE through the out mailbox) ---- */
#define MSG_PING	0xC1000000u	/* | n: n mailbox round trips follow */
#define MSG_SIG		0xC2000000u	/* PPE writes the signal registers, then sends an ack word */
#define MSG_RATE_START	0xC3000000u
#define MSG_RATE_STOP	0xC4000000u	/* followed by one word: elapsed decrementer ticks */
#define MSG_ATOM_START	0xA7000001u	/* PPE starts contending on the line */
#define MSG_ATOM_DONE	0xA7000002u
#define MSG_ATOM_LOST	0xA7000003u	/* SPU holds a reservation; PPE stores into the line, acks */
#define MSG_DONE	0xD0DE0000u

/* the atomic line: word 0 the SPU's counter, 1 the PPE's, 2 the PPE's "lost" marker (ATOM_MARK + n),
 * 3 the SPU's unconditional store */
/* increments of word 0 per pass: ATOM_N in the loop plus one after the lost reservation */
#define ATOM_N		4000u
#define ATOM_MARK	0x5EC0DE17u
#define ATOM_UNCOND	0xB16B00B5u

/* byte i of the source pattern buffer */
static inline uint8_t spt_pat(uint32_t seed, uint32_t i)
{
	return (uint8_t)((i * 167u) ^ (i >> 7) ^ (i >> 13) ^ seed ^ (seed >> 8));
}

/* The DMA list test's layout: element k of list `l` (all multiples of 16) */
static inline void spt_lst_elem(uint32_t seed, uint32_t l, uint32_t k, uint32_t *off, uint32_t *size)
{
	uint32_t x = (seed + l * 977u + k * 131u) * 2654435761u;

	*size = 16u * (1u + ((x >> 12) % 96u));	/* 16 .. 1536 */
	*off = 16u * ((x >> 3) % 2048u);	/* within the first 32 KB of the buffer */
}
#define LST_ELEMS	16

static inline void spt_swap_words(void *p, unsigned bytes)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	uint32_t *w = p;
	unsigned i;

	for (i = 0; i < bytes / 4; i++)
		w[i] = __builtin_bswap32(w[i]);
#else
	(void)p;
	(void)bytes;
#endif
}

#endif
