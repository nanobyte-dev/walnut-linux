/* SPDX-License-Identifier: GPL-2.0 */
/*
 * vseal_abi.h: what ps3rsx's SPE client (spe.c) and its SPU program (spu/vseal.c) share. Both sides are
 * big-endian; the host tests (test/vseal-test.c) build the SPU program on the PC, so nothing here
 * depends on byte order beyond "the same on both sides".
 *
 * The SPU program seals and opens 4 KiB pages with ChaCha20-Poly1305 (RFC 8439) for the VRAM swap: a
 * page's ciphertext sits in VRAM, its tag and nonce counter only in kernel RAM.
 *
 * The local store (LS). The PPE writes the program and the parameter block while LS decode is on, then
 * turns it off (SR1 0x32): from then on nothing reaches the LS through an effective address, and every
 * exchange goes by DMA through the job ring in kernel RAM and the SPU's mailboxes.
 *
 * A job. The kernel fills one of VSEAL_JOBS descriptors in the ring (one 4 KiB page each, in pages that
 * are never freed while the SPE may run), then writes the job's word (vseal_word(): its index, its job
 * number and the load's generation) to the SPU's inbound mailbox. The SPU fetches the descriptor's
 * input part, runs it only if its seq gives the same word (a word left over from before a restart, or
 * for another descriptor, is dropped unanswered), checks it (op, page count, alignment), runs the pages
 * through three LS buffers (one MFC tag group each; every get into a buffer fenced behind that buffer's
 * earlier put), writes the results into the descriptor's result part only (never its inputs), then the
 * job's done word, fenced after everything else, and last the job's word through the outbound
 * interrupt mailbox. After every page it puts a progress counter into the status line, so a slow job
 * can be told from a stuck one.
 */
#ifndef PS3RSX_VSEAL_ABI_H
#define PS3RSX_VSEAL_ABI_H

#ifdef __KERNEL__
#include <linux/stddef.h>
#include <linux/types.h>
typedef u8 vs_u8;
typedef u16 vs_u16;
typedef s16 vs_s16;
typedef u32 vs_u32;
typedef u64 vs_u64;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t vs_u8;
typedef uint16_t vs_u16;
typedef int16_t vs_s16;
typedef uint32_t vs_u32;
typedef uint64_t vs_u64;
#endif

#define VSEAL_MAGIC		0x7653414cu	/* "vSAL" */
#define VSEAL_ABI		3		/* 3: the interrupt only when the job's flag asks for it */

/* LS layout (offsets); every buffer 128-byte aligned, the program below VSEAL_LS_PROG_MAX */
#define VSEAL_LS_SIZE		0x40000
#define VSEAL_LS_PROG_MAX	0x10000
#define VSEAL_LS_PARAM		0x10000		/* struct vseal_param, written by the PPE at load */
#define VSEAL_LS_DESC		0x11000		/* the current job's descriptor (VSEAL_DESC_BYTES) */
#define VSEAL_LS_BUF0		0x13000		/* three page buffers, 4 KiB each */
#define VSEAL_LS_NBUF		3
#define VSEAL_LS_STATUS		0x16000		/* the status line the SPU puts */
#define VSEAL_LS_STACK_TOP	0x3fff0

#define VSEAL_JOBS		4		/* descriptors in the ring */
#define VSEAL_PAGES_MAX		32		/* pages per job */
#define VSEAL_PAGE		4096

/* ops */
#define VSEAL_OP_ECHO		1	/* nothing but the done word: the round trip */
#define VSEAL_OP_SEAL		2	/* page src (plaintext) -> dst (ciphertext), tag out */
#define VSEAL_OP_OPEN		3	/* page src (ciphertext) -> dst (plaintext), only if the tag in matches */
#define VSEAL_OP_GOLDEN		4	/* seal the program's built-in page under its built-in key: tag and FNV out */
/* test builds' ops: only in the test image (VSEAL_TEST_OPS; vseal_spu_test.h), and the kernel refuses them
 * outside test builds */
#define VSEAL_OP_TEST_SLOW	16	/* as SEAL, waiting arg decrementer ticks before each page */
#define VSEAL_OP_TEST_HANG	17	/* spin without progress, never answer */
#define VSEAL_OP_TEST_NOIRQ	18	/* as ECHO, without the interrupt mailbox write */
#define VSEAL_OP_TEST_DIFF	19	/* each page src: the four-lane Poly1305 against the scalar one (key: the
					 * page's ad || tag, so the kernel can pick r at its clamp maximum) and the
					 * four-block ChaCha20 against the scalar one (nonce from ctr); status OK when
					 * both agree, BADTAG when not; nothing is put */
#define VSEAL_OP_TEST_COPY	21	/* as SEAL's pages, without the cipher: src copied to dst as it is (the
					 * measurement of the cipher's own cost; test image and test module only) */
#define VSEAL_OP_TEST_BADADDR	20	/* one get from page[0].src, an address the kernel picked to have no
					 * translation (a vmalloc guard page): the MFC faults, the job never ends */

/* per-page and job status */
#define VSEAL_ST_NONE		0
#define VSEAL_ST_OK		1
#define VSEAL_ST_BADTAG		2	/* open: the tag didn't match; nothing was put */
#define VSEAL_ST_INVALID	3	/* the descriptor was refused by the SPU */

/* The parameter block (LS VSEAL_LS_PARAM), written once per load with LS decode on */
struct vseal_param {
	vs_u32 magic, abi;
	vs_u64 ring_ea;			/* VSEAL_JOBS descriptors, one per 4 KiB, 4 KiB aligned */
	vs_u64 status_ea;		/* the status line, 128-byte aligned */
	vs_u8 key[32];			/* the boot's key */
	vs_u32 pad[20];
} __attribute__((aligned(128)));

/* One page of a job, its inputs: 64 bytes */
struct vseal_page {
	vs_u64 src, dst;		/* kernel EAs, 4 KiB aligned */
	vs_u64 ctr;			/* the nonce: 0^32 || le64(ctr) */
	vs_u8 ad[16];			/* le32 device page, le32 slot, format, 7 zero bytes */
	vs_u8 tag[16];			/* open: the tag to check (seal: unused) */
	vs_u32 pad[2];
};

/* One page's result: 32 bytes */
struct vseal_result {
	vs_u8 tag[16];			/* seal: the tag; open, diff: the computed tag */
	vs_u32 status;			/* VSEAL_ST_* */
	vs_u32 pad[3];
};

/* The descriptor: inputs (a 128-byte header and the pages), which the SPU only reads, then results,
 * which it only writes */
struct vseal_job {
	vs_u32 magic, op, npages, arg;
	vs_u64 seq;			/* the generation (upper 32) and the job's number (lower 32) */
	vs_u32 flags;			/* VSEAL_JF_*: read again by the SPU after its done word */
	vs_u32 pad[25];		/* (room for more inputs: e.g. a compressed op's lengths) */
	struct vseal_page page[VSEAL_PAGES_MAX];
	/* results */
	vs_u64 done;			/* written last by the SPU: seq when finished */
	vs_u32 status;			/* VSEAL_ST_OK or VSEAL_ST_INVALID */
	vs_u32 fnv;			/* GOLDEN: the FNV-1a of the ciphertext */
	vs_u32 rpad[28];
	struct vseal_result res[VSEAL_PAGES_MAX];
} __attribute__((aligned(4096)));	/* one descriptor a 4 KiB page: ring[idx] is at ring_ea + idx * 4 KiB */

#define VSEAL_IN_BYTES		offsetof(struct vseal_job, done)
#define VSEAL_OUT_OFF		offsetof(struct vseal_job, done)
#define VSEAL_DESC_BYTES	(offsetof(struct vseal_job, res) + VSEAL_PAGES_MAX * sizeof(struct vseal_result))
#define VSEAL_OUT_BYTES		(VSEAL_DESC_BYTES - VSEAL_OUT_OFF)

/* VSEAL_JF_IRQ: answer through the interrupt mailbox. A polled job goes without; if the poll runs out, the PPE
 * sets the flag in the ring, a full barrier, then reads the done word again, while the SPU, after its done word
 * and a sync, reads the flag: one of the two always sees the other's write. */
#define VSEAL_JF_IRQ		1

/* The inbound mailbox word of a job: the load's generation (8 bits), the job's number (22 bits), the
 * descriptor's index (2 bits). The SPU answers with the same word. */
static inline vs_u32 vseal_word(vs_u64 seq, unsigned int idx)
{
	return (vs_u32)((seq >> 32) & 0xff) << 24 | ((vs_u32)seq & 0x3fffff) << 2 | (idx & 3);
}

/* The status line the SPU keeps current (128 bytes; the kernel zeroes it before every load) */
struct vseal_status {
	vs_u64 progress;		/* pages processed since load: rises while the SPU works */
	vs_u64 last_seq;		/* the job it is on */
	vs_u32 pad[28];
} __attribute__((aligned(128)));

/* GOLDEN's inputs (built into the program) and the answer the kernel compares against */
#define VSEAL_GOLDEN_CTR	0x0123456789abcdefull

_Static_assert(sizeof(struct vseal_page) == 64, "vseal_page");
_Static_assert(sizeof(struct vseal_result) == 32, "vseal_result");
_Static_assert(VSEAL_DESC_BYTES == 128 + 64 * VSEAL_PAGES_MAX + 128 + 32 * VSEAL_PAGES_MAX, "vseal_job");
_Static_assert(sizeof(struct vseal_job) == VSEAL_PAGE, "one descriptor a page");
_Static_assert(offsetof(struct vseal_job, flags) == 24, "seq and flags in one quadword");
_Static_assert(VSEAL_OUT_OFF % 128 == 0 && VSEAL_IN_BYTES % 16 == 0 && VSEAL_OUT_BYTES % 16 == 0, "DMA sizes");
_Static_assert(VSEAL_JOBS == 4, "vseal_word() has two index bits");
_Static_assert(sizeof(struct vseal_job) <= 0x2000, "the descriptor fits its LS area");
_Static_assert(sizeof(struct vseal_job) <= VSEAL_PAGE, "one descriptor per 4 KiB ring page");
_Static_assert(sizeof(struct vseal_param) <= 0x1000, "vseal_param");
_Static_assert(sizeof(struct vseal_status) == 128, "vseal_status");
_Static_assert(VSEAL_LS_BUF0 + VSEAL_LS_NBUF * VSEAL_PAGE <= VSEAL_LS_STATUS, "LS layout");
_Static_assert((VSEAL_LS_DESC | VSEAL_LS_BUF0 | VSEAL_LS_STATUS | VSEAL_LS_PARAM) % 128 == 0, "LS alignment");
_Static_assert(VSEAL_LS_NBUF >= 3, "the pipeline is written for three buffers");

#endif
