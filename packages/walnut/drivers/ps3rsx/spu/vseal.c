// SPDX-License-Identifier: GPL-2.0
/*
 * vseal.c: the SPU program of ps3rsx's SPE client (../src/spe.c). Bare: no newlib, no stop-and-signal
 * calls; it only reads its mailbox, DMAs, uses the decrementer, and runs ChaCha20-Poly1305 (v2_*.c).
 * The ABI, the LS layout and the job format are in ../src/vseal_abi.h.
 *
 * Rules this program keeps (each a host-test case in ../test/vseal-test.c, against an MFC model that runs
 * unfenced commands in any order):
 *  - every address and count it uses comes from the PPE's descriptor and is checked here first (op, page
 *    count, 4 KiB alignment, non-zero); nothing is taken from DMA'd page data;
 *  - three LS buffers, each with its own MFC tag group; every get into a buffer is fenced (getf) on that
 *    buffer's tag, so it runs only after the buffer's earlier put has read it: a page is never refilled
 *    while its previous contents are still on their way out;
 *  - open: one get of the ciphertext into a buffer, the tag checked over that buffer, and only on a match
 *    is that same buffer decrypted and put; on a mismatch nothing is put from it and it is cleared;
 *  - the job's results, then its done word, go last: after waiting for every data put and an MFC sync,
 *    the descriptor, then the done word fenced behind it, then the interrupt mailbox.
 */
#include <stddef.h>
#include <stdint.h>

#include "vseal_abi.h"
#include "v2_crypto.h"

#ifndef VSEAL_HOST
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#define LSP(off)		((void *)(uintptr_t)(off))
#define DMA_GET(ls, ea, n, t)	mfc_get((volatile void *)(ls), (ea), (n), (t), 0, 0)
#define DMA_GETF(ls, ea, n, t)	mfc_getf((volatile void *)(ls), (ea), (n), (t), 0, 0)
#define DMA_PUT(ls, ea, n, t)	mfc_put((volatile void *)(ls), (ea), (n), (t), 0, 0)
#define DMA_PUTF(ls, ea, n, t)	mfc_putf((volatile void *)(ls), (ea), (n), (t), 0, 0)
#define DMA_SYNC(t)		mfc_sync(t)
#define DMA_WAIT(mask)		do { mfc_write_tag_mask(mask); mfc_read_tag_status_all(); } while (0)
#define MBOX_READ()		spu_read_in_mbox()
#define MBOX_IRQ_WRITE(v)	spu_write_out_intr_mbox(v)
#define DEC_READ()		spu_read_decrementer()
#define DEC_START()		spu_writech(SPU_WrDec, 0xffffffffu)
#else
#include "vseal-sim.h"		/* the host tests' LS and MFC model */
#endif

/* H1's fix-off (the host tests build it to show the reordering model catches it): a plain get */
#ifdef VSEAL_FIXOFF_PLAIN_GET
#undef DMA_GETF
#define DMA_GETF DMA_GET
#endif

#define TAG_BUF(b)	(b)		/* 0-2: the buffers */
#define TAG_STATUS	4
#define TAG_DESC	5
#define TAG_SYNC	6

#define PARAM		((struct vseal_param *)LSP(VSEAL_LS_PARAM))
#define JOB		((struct vseal_job *)LSP(VSEAL_LS_DESC))
#define STATUS		((struct vseal_status *)LSP(VSEAL_LS_STATUS))
#define BUF(b)		((uint8_t *)LSP(VSEAL_LS_BUF0 + (b) * VSEAL_PAGE))

#ifndef VSEAL_HOST
/* the compiler may call these; no newlib */
void *memcpy(void *d, const void *s, size_t n)
{
	uint8_t *dp = d;
	const uint8_t *sp = s;

	while (n--)
		*dp++ = *sp++;
	return d;
}

void *memset(void *d, int c, size_t n)
{
	uint8_t *dp = d;

	while (n--)
		*dp++ = (uint8_t)c;
	return d;
}
#endif

static uint64_t progress;

static void status_update(uint64_t seq)
{
	DMA_WAIT(1u << TAG_STATUS);		/* the earlier status put has read the LS line */
	STATUS->progress = progress;
	STATUS->last_seq = seq;
	DMA_PUT(STATUS, PARAM->status_ea, sizeof(struct vseal_status), TAG_STATUS);
}

static int page_ops(uint32_t op)
{
	return op == VSEAL_OP_SEAL || op == VSEAL_OP_OPEN || op == VSEAL_OP_TEST_SLOW;
}

/* TEST_DIFF: the vector code against the scalar code on this SPU, page by page (no pipeline: one get
 * into buffer 0, the scalar ChaCha20 into buffer 1, the vector one into buffer 2) */
static void diff(uint64_t seq)
{
	uint32_t n = JOB->npages, i;

	for (i = 0; i < n; i++) {
		struct vseal_page *pg = &JOB->page[i];
		uint8_t key[32], t1[16], t2[16], nonce[12];

		DMA_WAIT(1u << TAG_BUF(0));
		DMA_GETF(BUF(0), pg->src, VSEAL_PAGE, TAG_BUF(0));
		DMA_WAIT(1u << TAG_BUF(0));
		memcpy(key, pg->ad, 16);
		memcpy(key + 16, pg->tag, 16);
		v2_poly1305_x4(key, BUF(0), VSEAL_PAGE, t1);
		v2_poly1305(key, BUF(0), VSEAL_PAGE, t2);
		v2_nonce_from_counter(pg->ctr, nonce);
		v2_chacha20_xor(PARAM->key, nonce, 1, BUF(1), BUF(0), VSEAL_PAGE);
		v2_chacha20_xor_x4(PARAM->key, nonce, 1, BUF(2), BUF(0), VSEAL_PAGE);
		pg->status = !v2_ct_differs(t1, t2, 16) && !v2_ct_differs(BUF(1), BUF(2), VSEAL_PAGE) ?
			     VSEAL_ST_OK : VSEAL_ST_BADTAG;
		memcpy(pg->tag, t1, 16);
		progress++;
		status_update(seq);
	}
}

static int job_ok(void)
{
	uint32_t i, n = JOB->npages;

	if (JOB->magic != VSEAL_MAGIC)
		return 0;
	switch (JOB->op) {
	case VSEAL_OP_ECHO:
	case VSEAL_OP_GOLDEN:
	case VSEAL_OP_TEST_HANG:
	case VSEAL_OP_TEST_NOIRQ:
		return 1;
	case VSEAL_OP_SEAL:
	case VSEAL_OP_OPEN:
	case VSEAL_OP_TEST_SLOW:
	case VSEAL_OP_TEST_DIFF:
		break;
	default:
		return 0;
	}
	if (n == 0 || n > VSEAL_PAGES_MAX)
		return 0;
	for (i = 0; i < n; i++) {
		const struct vseal_page *pg = &JOB->page[i];

		if (!pg->src || !pg->dst || (pg->src & (VSEAL_PAGE - 1)) || (pg->dst & (VSEAL_PAGE - 1)))
			return 0;
	}
	return 1;
}

/* the page in buffer b, for page i of the job */
static void process(uint32_t op, uint32_t i, uint8_t *buf)
{
	struct vseal_page *pg = &JOB->page[i];
	uint8_t nonce[12];

	v2_nonce_from_counter(pg->ctr, nonce);
	if (op == VSEAL_OP_OPEN) {
		if (v2_open_page(PARAM->key, nonce, pg->ad, buf, buf, pg->tag)) {
			memset(buf, 0, VSEAL_PAGE);	/* nothing from this copy goes anywhere */
			pg->status = VSEAL_ST_BADTAG;
		} else {
			pg->status = VSEAL_ST_OK;
		}
	} else {
		v2_seal_page(PARAM->key, nonce, pg->ad, buf, buf, pg->tag);
		pg->status = VSEAL_ST_OK;
	}
}

static void wait_ticks(uint32_t ticks)	/* TEST_SLOW */
{
	uint32_t t0 = DEC_READ();

	while (t0 - DEC_READ() < ticks)
		;
}

static void pages(uint32_t op, uint64_t seq)
{
	uint32_t n = JOB->npages, i;

	DMA_GETF(BUF(0), JOB->page[0].src, VSEAL_PAGE, TAG_BUF(0));
	for (i = 0; i < n; i++) {
		uint32_t b = i % VSEAL_LS_NBUF;

		if (i + 1 < n) {	/* the next page into the next buffer, behind that buffer's put */
			uint32_t nb = (i + 1) % VSEAL_LS_NBUF;

			DMA_GETF(BUF(nb), JOB->page[i + 1].src, VSEAL_PAGE, TAG_BUF(nb));
		}
		DMA_WAIT(1u << TAG_BUF(b));	/* this page's get (and the buffer's older put) done */
		if (op == VSEAL_OP_TEST_SLOW)
			wait_ticks(JOB->arg);
		process(op == VSEAL_OP_TEST_SLOW ? VSEAL_OP_SEAL : op, i, BUF(b));
		if (JOB->page[i].status == VSEAL_ST_OK)
			DMA_PUT(BUF(b), JOB->page[i].dst, VSEAL_PAGE, TAG_BUF(b));
		progress++;
		status_update(seq);
	}
}

/* the built-in page, sealed under the built-in key: the kernel compares tag and FNV with its constants */
static void golden(void)
{
	uint8_t key[32], nonce[12], ad[16], *buf = BUF(0);
	uint32_t s = 7, i, fnv = 2166136261u;

	for (i = 0; i < 32; i++)
		key[i] = 0x80 + i;
	for (i = 0; i < VSEAL_PAGE; i++) {
		s = s * 1103515245u + 12345u;
		buf[i] = s >> 16;
	}
	v2_nonce_from_counter(VSEAL_GOLDEN_CTR, nonce);
	v2_ad_make(0x1234, 77, 0, ad);
	v2_seal_page(key, nonce, ad, buf, buf, JOB->page[0].tag);
	for (i = 0; i < VSEAL_PAGE; i++)
		fnv = (fnv ^ buf[i]) * 16777619u;
	JOB->fnv = fnv;
	memset(key, 0, sizeof(key));
}

/* one job: descriptor idx of the ring */
void vseal_job(uint32_t idx)
{
	uint64_t ea = PARAM->ring_ea + (uint64_t)idx * VSEAL_PAGE, seq;
	uint32_t op;

	DMA_WAIT(0x7fu);		/* nothing of an earlier job still reads or writes the LS */
	DMA_GET(JOB, ea, VSEAL_DESC_BYTES, TAG_DESC);
	DMA_WAIT(1u << TAG_DESC);
	seq = JOB->seq;
	op = JOB->op;
	JOB->done = 0;
	if (!job_ok()) {
		JOB->status = VSEAL_ST_INVALID;
	} else {
		JOB->status = VSEAL_ST_OK;
		if (op == VSEAL_OP_TEST_HANG)
			for (;;)
				wait_ticks(1u << 20);	/* no progress, no answer */
		if (op == VSEAL_OP_GOLDEN)
			golden();
		else if (op == VSEAL_OP_TEST_DIFF)
			diff(seq);
		else if (page_ops(op))
			pages(op, seq);
	}
	/* every data put done, then a sync, then the results, then the done word fenced behind them */
	DMA_WAIT((1u << TAG_BUF(0)) | (1u << TAG_BUF(1)) | (1u << TAG_BUF(2)) | (1u << TAG_STATUS));
	DMA_SYNC(TAG_SYNC);
	DMA_WAIT(1u << TAG_SYNC);
	DMA_PUT(JOB, ea, VSEAL_DESC_BYTES, TAG_DESC);
	DMA_WAIT(1u << TAG_DESC);	/* the descriptor put has read the LS before done changes in it */
	JOB->done = seq;
	DMA_PUTF(&JOB->done, ea + offsetof(struct vseal_job, done), sizeof(JOB->done), TAG_DESC);
	DMA_WAIT(1u << TAG_DESC);
	if (op != VSEAL_OP_TEST_NOIRQ)
		MBOX_IRQ_WRITE((uint32_t)seq);
}

#ifndef VSEAL_HOST
int main(void)
{
	uint32_t idx;

	DEC_START();
	for (;;) {
		idx = MBOX_READ();
		if (idx < VSEAL_JOBS)
			vseal_job(idx);
	}
}
#endif
