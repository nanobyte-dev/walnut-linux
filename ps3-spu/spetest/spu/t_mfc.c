/* The memory flow controller: DMA get and put of every size 16 .. 16 KB (and 1, 2, 4, 8 at their
 * alignments, 16-byte multiples at every 16-byte offset), DMA lists, fences and barriers, tag
 * status, and the atomic line commands (getllar/putllc against the PPE's stores, putlluc,
 * putqlluc). The buffers are the PPE's: ctl.src is a read-only pattern (byte i = spt_pat), ctl.out
 * takes the results. The SPU copies the pattern in once and compares everything it moves against
 * that copy; runner.c checks the checksum table and the last puts in main memory. The layout of
 * ctl.out is below (the runner has the same numbers). */
#include <string.h>
#include "common.h"

#define LSZ	16384
#define SRCSZ	65536
#define TBL_OFF	0u		/* 1024 checksums of the get test */
#define DST_OFF	8192u		/* guard 256, 16 KB, guard 256 */
#define GUARD	256u
#define LST_OFF	32768u		/* LST_ELEMS slots of 1536 */
#define LSLOT	1536u
#define FEN_OFF	73728u		/* 16 KB + guards for the fence tests */

static uint8_t lsX[SRCSZ] __attribute__((aligned(128)));	/* the PPE's pattern, copied in once */
static uint8_t lsA[LSZ + 512] __attribute__((aligned(128)));
static uint8_t lsB[LSZ + 512] __attribute__((aligned(128)));
static uint8_t lsS[LSZ + 512] __attribute__((aligned(128)));
static uint32_t tbl[1024] __attribute__((aligned(128)));
static uint8_t lsL[LST_ELEMS * 1536 + 128] __attribute__((aligned(128)));
static mfc_list_element_t lst[LST_ELEMS + 2] __attribute__((aligned(16)));
static union { uint32_t w[32]; uint8_t b[128]; } ln __attribute__((aligned(128)));

static uint64_t src, out, line;
static uint32_t seed;

typedef vec_uchar16 VB;
#define LDV(p) (*(const volatile VB *)(p))

static void wait_tag(unsigned tag)
{
	mfc_write_tag_mask(1u << tag);
	mfc_read_tag_status_all();
}

static void dma_get(void *ls, uint64_t ea, unsigned n, unsigned tag)
{
	while (n > LSZ) {
		mfc_get(ls, ea, LSZ, tag, 0, 0);
		ls = (char *)ls + LSZ;
		ea += LSZ;
		n -= LSZ;
	}
	if (n)
		mfc_get(ls, ea, n, tag, 0, 0);
}

static void dma_put(void *ls, uint64_t ea, unsigned n, unsigned tag)
{
	while (n > LSZ) {
		mfc_put(ls, ea, LSZ, tag, 0, 0);
		ls = (char *)ls + LSZ;
		ea += LSZ;
		n -= LSZ;
	}
	if (n)
		mfc_put(ls, ea, n, tag, 0, 0);
}

/* the n bytes at p equal those at e; false (and a record) at the first difference. Quadword
 * compares while both pointers are 16-aligned alike, bytes for the rest. */
static int cmp_exp(unsigned sub, const uint8_t *p, const uint8_t *e, unsigned n, uint32_t tag)
{
	unsigned i = 0;

	if (((uintptr_t)p ^ (uintptr_t)e) & 15)
		goto bytes;
	while (i < n && ((uintptr_t)(p + i) & 15))
		if (p[i] != e[i])
			goto bytes;
		else
			i++;
	for (; i + 16 <= n; i += 16)
		if (spu_extract(spu_gather(spu_cmpeq((vec_uint4)LDV(p + i), (vec_uint4)LDV(e + i))), 0) != 0xF)
			break;
bytes:
	for (; i < n; i++)
		if (p[i] != e[i]) {
			t_fail(sub, tag * 65536u + i, p[i], e[i]);
			return 0;
		}
	return 1;
}

/* the n bytes at p all equal v */
static int cmp_val(unsigned sub, const uint8_t *p, unsigned n, uint8_t v, uint32_t tag)
{
	unsigned i = 0;
	VB want = spu_splats(v);

	while (i < n && ((uintptr_t)(p + i) & 15))
		if (p[i] != v)
			goto bytes;
		else
			i++;
	for (; i + 16 <= n; i += 16)
		if (spu_extract(spu_gather(spu_cmpeq((vec_uint4)LDV(p + i), (vec_uint4)want)), 0) != 0xF)
			break;
bytes:
	for (; i < n; i++)
		if (p[i] != v) {
			t_fail(sub, tag * 65536u + i, p[i], v);
			return 0;
		}
	return 1;
}

/* get every size 16 .. 16384 from src at moving offsets, into moving LS offsets */
static void t_get(void)
{
	unsigned k;

	for (k = 1; k <= 1024; k++) {
		unsigned size = 16 * k, eo = 16 * (k % 8), lo = 16 * ((k / 8) % 8);
		uint8_t *d = lsA + 128 + lo;

		memset(lsA, 0xEE, LSZ + 512);
		mfc_get(d, src + eo, size, 3, 0, 0);
		wait_tag(3);
		cmp_exp(MF_GET, d, lsX + eo, size, k);
		cmp_val(MF_GET, d - 16, 16, 0xEE, k);
		cmp_val(MF_GET, d + size, 16, 0xEE, k);
		tbl[k - 1] = spt_ck32(d, size);
	}
	dma_put(tbl, out + TBL_OFF, sizeof(tbl), 4);
	wait_tag(4);
}

static void reset_dst(void)
{
	dma_put(lsS, out + DST_OFF, LSZ + 2 * GUARD, 2);
	wait_tag(2);
}

/* put every size (from the pattern at 16 * (k % 512)) and read it back through a get */
static void t_put(void)
{
	unsigned k;

	memset(lsS, 0xA5, LSZ + 512);
	for (k = 1; k <= 1024; k++) {
		unsigned size = 16 * k, eo = 16 * (k % 8);
		uint8_t *s = lsX + 16 * (k % 512);

		reset_dst();
		mfc_put(s, out + DST_OFF + GUARD + eo, size, 3, 0, 0);
		wait_tag(3);
		memset(lsB, 0x00, LSZ + 512);
		dma_get(lsB, out + DST_OFF, LSZ + 2 * GUARD, 3);
		wait_tag(3);
		cmp_val(MF_PUT, lsB, GUARD + eo, 0xA5, k);
		cmp_exp(MF_PUT, lsB + GUARD + eo, s, size, k);
		cmp_val(MF_PUT, lsB + GUARD + eo + size, LSZ + 2 * GUARD - (GUARD + eo + size), 0xA5, k);
	}
}

/* sizes 1, 2, 4, 8 at their natural alignments (low 4 bits equal on both sides); 16-byte
 * multiples at every 16-byte offset on each side independently */
static void t_small(void)
{
	static const unsigned sz[4] = { 1, 2, 4, 8 };
	unsigned a, off, m, k;

	for (a = 0; a < 4; a++)
		for (off = 0; off < 16; off += sz[a])
			for (m = 0; m < 3; m++) {
				unsigned eo = off + 16 * (m * 5), lo = off + 16 * m, i;
				uint8_t *d = lsA + 128 + lo;
				uint32_t tag = sz[a] * 1000 + off * 10 + m;

				memset(lsA, 0xEE, 512);
				mfc_get(d, src + eo, sz[a], 3, 0, 0);
				wait_tag(3);
				cmp_exp(MF_SMALL, d, lsX + eo, sz[a], tag);
				cmp_val(MF_SMALL, d - 1, 1, 0xEE, tag);
				cmp_val(MF_SMALL, d + sz[a], 1, 0xEE, tag);
				/* put: sentinels in main memory around it, read the 128 bytes back */
				memset(lsS, 0xA5, 128);
				dma_put(lsS, out + DST_OFF, 128, 2);
				wait_tag(2);
				for (i = 0; i < sz[a]; i++)
					d[i] = lsX[300 + off + i];
				mfc_put(d, out + DST_OFF + off, sz[a], 3, 0, 0);
				wait_tag(3);
				mfc_get(lsB, out + DST_OFF, 128, 3, 0, 0);
				wait_tag(3);
				for (i = 0; i < 128; i++) {
					uint8_t w = (i >= off && i < off + sz[a]) ? lsX[300 + i] : 0xA5;

					if (lsB[i] != w) {
						t_fail(MF_SMALL, tag * 256 + i, lsB[i], w);
						break;
					}
				}
			}
	for (k = 1; k <= 16; k++)
		for (a = 0; a < 8; a++)
			for (m = 0; m < 8; m++) {
				unsigned size = 16 * k, eo = 16 * a, lo = 16 * m;
				uint8_t *d = lsA + 128 + lo;
				uint32_t tag = k * 100 + a * 10 + m;

				memset(lsA, 0xEE, 512);
				mfc_get(d, src + 3 * 128 + eo, size, 3, 0, 0);
				wait_tag(3);
				cmp_exp(MF_ALIGN, d, lsX + 3 * 128 + eo, size, tag);
				cmp_val(MF_ALIGN, d - 16, 16, 0xEE, tag);
				cmp_val(MF_ALIGN, d + size, 16, 0xEE, tag);
			}
}

static void t_list(void)
{
	unsigned l, k;

	for (l = 0; l < 4; l++) {
		uint32_t off[LST_ELEMS], size[LST_ELEMS], cum = 0;

		for (k = 0; k < LST_ELEMS; k++) {
			spt_lst_elem(seed, l, k, &off[k], &size[k]);
			lst[k].notify = 0;
			lst[k].reserved = 0;
			lst[k].size = size[k];
			lst[k].eal = (uint32_t)(src + off[k]);
		}
		memset(lsL, 0xEE, sizeof(lsL));
		mfc_getl(lsL, src, lst, LST_ELEMS * sizeof(lst[0]), 3, 0, 0);
		wait_tag(3);
		for (k = 0; k < LST_ELEMS; k++) {
			cmp_exp(MF_LIST, lsL + cum, lsX + off[k], size[k], l * 100 + k);
			cum += size[k];
		}
		cmp_val(MF_LIST, lsL + cum, 16, 0xEE, l * 100 + 99);
		/* scatter: slot k of the output gets the pattern at k * slot + 16 * l */
		cum = 0;
		for (k = 0; k < LST_ELEMS; k++) {
			memcpy(lsL + cum, lsX + k * LSLOT + 16 * l, size[k]);
			lst[k].eal = (uint32_t)(out + LST_OFF + k * LSLOT);
			cum += size[k];
		}
		mfc_putl(lsL, out + LST_OFF, lst, LST_ELEMS * sizeof(lst[0]), 3, 0, 0);
		wait_tag(3);
		/* read the slots back one by one */
		for (k = 0; k < LST_ELEMS; k++) {
			mfc_get(lsB, out + LST_OFF + k * LSLOT, size[k], 3, 0, 0);
			wait_tag(3);
			cmp_exp(MF_LIST, lsB, lsX + k * LSLOT + 16 * l, size[k], l * 100 + 50 + k);
		}
	}
}

/* ordering inside a tag group: a big put, then a small one on top of it with a barrier or
 * a fence; the small one must win. The same for gets into local store. */
static void t_fence(void)
{
	unsigned r, v;

	memset(lsS, 0xA5, LSZ + 512);
	for (r = 0; r < 48; r++)
		for (v = 0; v < 2; v++) {
			unsigned sa = 128 * (1 + (r * 37) % 128);	/* 128 .. 16384 */

			memset(lsA, 0x11, LSZ);
			memset(lsB, 0x22, 128);
			dma_put(lsS, out + FEN_OFF, LSZ + 128, 2);
			wait_tag(2);
			mfc_put(lsA, out + FEN_OFF, sa, 4, 0, 0);
			if (v)
				mfc_putb(lsB, out + FEN_OFF, 128, 4, 0, 0);
			else
				mfc_putf(lsB, out + FEN_OFF, 128, 4, 0, 0);
			wait_tag(4);
			memset(lsA, 0, LSZ + 128);
			dma_get(lsA, out + FEN_OFF, LSZ + 128, 3);
			wait_tag(3);
			cmp_val(MF_FENCE, lsA, 128, 0x22, r * 2 + v);
			cmp_val(MF_FENCE, lsA + 128, sa - 128, 0x11, r * 2 + v);
			cmp_val(MF_FENCE, lsA + sa, 128, 0xA5, r * 2 + v);
		}
	/* gets: the second get (128 bytes from another place) must land last */
	for (r = 0; r < 48; r++)
		for (v = 0; v < 2; v++) {
			unsigned sa = 128 * (1 + (r * 41) % 128);

			memset(lsA, 0xEE, LSZ + 128);
			mfc_get(lsA, src, sa, 4, 0, 0);
			if (v)
				mfc_getb(lsA, src + 20000, 128, 4, 0, 0);
			else
				mfc_getf(lsA, src + 20000, 128, 4, 0, 0);
			wait_tag(4);
			cmp_exp(MF_FENCE, lsA, lsX + 20000, 128, 1000 + r * 2 + v);
			cmp_exp(MF_FENCE, lsA + 128, lsX + 128, sa - 128, 1000 + r * 2 + v);
		}
}

static void t_tags(void)
{
	uint32_t mask, st;

	/* three groups, all of them */
	memset(lsA, 0xEE, LSZ);
	memset(lsB, 0xEE, LSZ);
	mfc_get(lsA, src, 16384, 2, 0, 0);
	mfc_get(lsB, src + 4096, 4096, 9, 0, 0);
	memset(lsS, 0x3C, 8192);
	mfc_put(lsS, out + FEN_OFF, 8192, 17, 0, 0);
	mask = (1u << 2) | (1u << 9) | (1u << 17);
	mfc_write_tag_mask(mask);
	st = mfc_read_tag_status_all();
	if (st != mask)
		t_fail(MF_TAGS, 1, st, mask);
	st = mfc_read_tag_status_immediate();
	if (st != mask)
		t_fail(MF_TAGS, 2, st, mask);
	cmp_exp(MF_TAGS, lsA, lsX, 16384, 1);
	cmp_exp(MF_TAGS, lsB, lsX + 4096, 4096, 2);
	/* any of two */
	memset(lsA, 0xEE, LSZ);
	mfc_get(lsA, src, 16384, 5, 0, 0);
	mfc_get(lsB, src + 128, 128, 6, 0, 0);
	mask = (1u << 5) | (1u << 6);
	mfc_write_tag_mask(mask);
	st = mfc_read_tag_status_any();
	if (!(st & mask) || (st & ~mask))
		t_fail(MF_TAGS, 3, st, mask);
	st = mfc_read_tag_status_all();
	if (st != mask)
		t_fail(MF_TAGS, 4, st, mask);
	cmp_exp(MF_TAGS, lsA, lsX, 16384, 3);
	/* a masked-out tag is not reported */
	mfc_get(lsA, src, 1024, 12, 0, 0);
	mfc_write_tag_mask(1u << 13);
	st = mfc_read_tag_status_all();
	if (st != (1u << 13))
		t_fail(MF_TAGS, 5, st, 1u << 13);
	wait_tag(12);
}

static void t_atomic(void)
{
	unsigned i, tries;
	uint32_t st, old2;

	/* the PPE adds to word 1 of this line while we add to word 0 */
	out_msg(MSG_ATOM_START);
	for (i = 0; i < ATOM_N; i++) {
		do {
			mfc_getllar(&ln, line, 0, 0);
			(void)mfc_read_atomic_status();
			ln.w[0]++;
			mfc_putllc(&ln, line, 0, 0);
			st = mfc_read_atomic_status();
		} while (st & MFC_PUTLLC_STATUS);
	}
	out_msg(MSG_ATOM_DONE);
	/* a reservation the PPE takes away (it stores a new value into word 2) */
	mfc_getllar(&ln, line, 0, 0);
	(void)mfc_read_atomic_status();
	old2 = ln.w[2];
	out_msg(MSG_ATOM_LOST);
	(void)spu_read_in_mbox();
	ln.w[3] = 0x11111111u;
	mfc_putllc(&ln, line, 0, 0);
	st = mfc_read_atomic_status();
	if (!(st & MFC_PUTLLC_STATUS))
		t_fail(MF_ATOMIC, 1, st, MFC_PUTLLC_STATUS);
	mfc_getllar(&ln, line, 0, 0);
	(void)mfc_read_atomic_status();
	if (ln.w[2] == old2 || ln.w[3] == 0x11111111u)
		t_fail(MF_ATOMIC, 2, ln.w[3], ln.w[2]);
	/* and one that survives */
	for (tries = 0; tries < 8; tries++) {
		mfc_getllar(&ln, line, 0, 0);
		(void)mfc_read_atomic_status();
		ln.w[0]++;
		mfc_putllc(&ln, line, 0, 0);
		st = mfc_read_atomic_status();
		if (!(st & MFC_PUTLLC_STATUS))
			break;
	}
	if (tries == 8)
		t_fail(MF_ATOMIC, 3, st, 0);
	/* unconditional: immediate, then queued */
	mfc_getllar(&ln, line, 0, 0);
	(void)mfc_read_atomic_status();
	ln.w[3] = ATOM_UNCOND;
	mfc_putlluc(&ln, line, 0, 0);
	(void)mfc_read_atomic_status();
	mfc_getllar(&ln, line, 0, 0);
	(void)mfc_read_atomic_status();
	if (ln.w[3] != ATOM_UNCOND)
		t_fail(MF_ATOMIC, 4, ln.w[3], ATOM_UNCOND);
	ln.w[3] = ~ATOM_UNCOND;
	mfc_putqlluc(&ln, line, 5, 0, 0);
	wait_tag(5);
	mfc_getllar(&ln, line, 0, 0);
	(void)mfc_read_atomic_status();
	if (ln.w[3] != ~ATOM_UNCOND)
		t_fail(MF_ATOMIC, 5, ln.w[3], ~ATOM_UNCOND);
	res.aux[0] = ln.w[0];
}

int main(unsigned long long slot, unsigned long long ea, unsigned long long seed0)
{
	unsigned i;

	t_begin(slot, ea, T_MFC);
	seed = (uint32_t)seed0;
	src = EA64(ctl.src_hi, ctl.src_lo);
	out = EA64(ctl.out_hi, ctl.out_lo);
	line = EA64(ctl.line_hi, ctl.line_lo);
	/* the pattern, in four gets, checked against its definition once */
	dma_get(lsX, src, SRCSZ, 3);
	wait_tag(3);
	for (i = 0; i < SRCSZ; i++)
		if (lsX[i] != spt_pat(seed, i)) {
			t_fail(MF_GET, 0xFFFF0000u + i, lsX[i], spt_pat(seed, i));
			break;
		}
	do {
		t_get();
		t_put();
		t_small();
		t_list();
		t_fence();
		t_tags();
		t_atomic();
	} while (t_more());
	return t_end();
}
