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
 * are never freed while the SPE may run), then writes the descriptor's index to the SPU's inbound
 * mailbox. The SPU fetches the descriptor, checks it (op, page count, alignment), runs the pages
 * through three LS buffers (one MFC tag group each; every get into a buffer fenced behind that buffer's
 * earlier put), writes each page's result back into the descriptor, then the job's done word, fenced
 * after everything else, and last its sequence number through the outbound interrupt mailbox. After
 * every page it also puts a progress counter into the status line, so a slow job can be told from a
 * stuck one.
 */
#ifndef PS3RSX_VSEAL_ABI_H
#define PS3RSX_VSEAL_ABI_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8 vs_u8;
typedef u32 vs_u32;
typedef u64 vs_u64;
#else
#include <stdint.h>
typedef uint8_t vs_u8;
typedef uint32_t vs_u32;
typedef uint64_t vs_u64;
#endif

#define VSEAL_MAGIC		0x7653414cu	/* "vSAL" */
#define VSEAL_ABI		1

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
/* test builds' ops (the kernel refuses them in a release build; the program has them always) */
#define VSEAL_OP_TEST_SLOW	16	/* as SEAL, waiting arg decrementer ticks before each page */
#define VSEAL_OP_TEST_HANG	17	/* spin without progress, never answer */
#define VSEAL_OP_TEST_NOIRQ	18	/* as ECHO, without the interrupt mailbox write */
#define VSEAL_OP_TEST_DIFF	19	/* each page src: the four-lane Poly1305 against the scalar one (key: the
					 * page's ad || tag, so the kernel can pick r at its clamp maximum) and the
					 * four-block ChaCha20 against the scalar one (nonce from ctr); status OK when
					 * both agree, BADTAG when not; nothing is put */

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

/* One page of a job: 64 bytes */
struct vseal_page {
	vs_u64 src, dst;		/* kernel EAs, 4 KiB aligned */
	vs_u64 ctr;			/* the nonce: 0^32 || le64(ctr) */
	vs_u8 ad[16];			/* le32 device page, le32 slot, format, 7 zero bytes */
	vs_u8 tag[16];			/* seal: out; open: in */
	vs_u32 status;			/* VSEAL_ST_*, out */
	vs_u32 pad;
};

/* The descriptor: a 128-byte header and its pages */
struct vseal_job {
	vs_u32 magic, op, npages, arg;
	vs_u64 seq;			/* the generation (upper 32) and the job's sequence number (lower 32) */
	vs_u64 done;			/* written last by the SPU: seq when finished */
	vs_u32 status;			/* VSEAL_ST_OK or VSEAL_ST_INVALID */
	vs_u32 fnv;			/* GOLDEN: the FNV-1a of the ciphertext */
	vs_u32 pad[22];
	struct vseal_page page[VSEAL_PAGES_MAX];
} __attribute__((aligned(128)));

#define VSEAL_DESC_BYTES	sizeof(struct vseal_job)

/* The status line the SPU keeps current (128 bytes) */
struct vseal_status {
	vs_u64 progress;		/* pages processed since load: rises while the SPU works */
	vs_u64 last_seq;		/* the job it is on */
	vs_u32 pad[28];
} __attribute__((aligned(128)));

/* GOLDEN's inputs (built into the program) and the answer the kernel compares against */
#define VSEAL_GOLDEN_CTR	0x0123456789abcdefull

_Static_assert(sizeof(struct vseal_page) == 64, "vseal_page");
_Static_assert(sizeof(struct vseal_job) == 128 + 64 * VSEAL_PAGES_MAX, "vseal_job");
_Static_assert(sizeof(struct vseal_job) <= 0x2000, "the descriptor fits its LS area");
_Static_assert(sizeof(struct vseal_job) <= VSEAL_PAGE, "one descriptor per 4 KiB ring page");
_Static_assert(sizeof(struct vseal_param) <= 0x1000, "vseal_param");
_Static_assert(sizeof(struct vseal_status) == 128, "vseal_status");
_Static_assert(VSEAL_LS_BUF0 + VSEAL_LS_NBUF * VSEAL_PAGE <= VSEAL_LS_STATUS, "LS layout");
_Static_assert((VSEAL_LS_DESC | VSEAL_LS_BUF0 | VSEAL_LS_STATUS | VSEAL_LS_PARAM) % 128 == 0, "LS alignment");
_Static_assert(VSEAL_LS_NBUF >= 3, "the pipeline is written for three buffers");

#endif
