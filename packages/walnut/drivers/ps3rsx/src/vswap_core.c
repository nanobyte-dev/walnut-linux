// SPDX-License-Identifier: GPL-2.0
/*
 * vswap_core.c: the sealed VRAM swap's page logic (vswap_core.h), built into ps3rsx and into the host model
 * (test/vswap-test.c). The fix-offs (VSX_FIXOFF_*) are the host model's: each takes one rule out, and the
 * model must fail.
 */
#include "vswap_core.h"
#include "v2_crypto.h"

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#define EA(p)	((vs_u64)(unsigned long)(p))

void vsx_ad(vs_u32 p, vs_u32 slot, vs_u8 fmt, vs_u8 ad[16])
{
#ifdef VSX_FIXOFF_AD_NO_PAGE			/* (the fix-off: the AD without the device page) */
	p = 0;
#endif
	v2_ad_make(p, slot, fmt, ad);
}

static int can_vram(struct vsx *x)
{
#ifdef VSX_PLAIN
	if (x->plain == 2)
		return x->mode == VSX_SEAL;	/* the SPE's copy */
	if (x->plain)
		return x->mode != VSX_DRAIN;
#endif
	return x->mode == VSX_SEAL && !x->locked;
}

static void gone(struct vsx *x)
{
	if (x->mode == VSX_DRAIN)
		return;
	x->mode = VSX_DRAIN;
	vsx_hw_event(x->hw, VSX_EV_GONE, 0, 0);
	vsx_hw_drain(x->hw);
}

/* a map entry leaves VRAM: its slot is freed */
static void leave_vram(struct vsx *x, vs_u32 e)
{
	if (vsx_in_vram(e)) {
		vsx_hw_slot_free(x->hw, e - 1);
		x->in_vram--;
	}
}

void vsx_set_disk(struct vsx *x, vs_u32 p)
{
	vs_u32 e = x->map[p];

	leave_vram(x, e);
	if (e == VSX_BAD)
		x->bad--;
	if (e != VSX_ON_DISK)
		x->on_disk++;
	x->map[p] = VSX_ON_DISK;
}

/* a write-back is writing page p to the disk right now: a request's disk I/O of p waits for it */
static int wb_busy(struct vsx *x, vs_u32 p)
{
	unsigned int i;

#ifdef VSX_FIXOFF_NO_DEFER			/* (the fix-off: the two writes unordered) */
	return 0;
#endif
	for (i = 0; i < x->wb_n; i++)
		if (x->wb_pages[i] == p)
			return 1;
	return 0;
}

static void to_disk(struct vsx *x, struct vsx_piece *pc)
{
	vsx_set_disk(x, pc->p);
	vsx_hw_disk(x->hw, pc, 1, wb_busy(x, pc->p));
}

/* the page now lives in slot (sealed and complete): the map switches, its old slot goes */
static void install(struct vsx *x, vs_u32 p, vs_u32 slot)
{
	vs_u32 e = x->map[p];

	leave_vram(x, e);
	if (e == VSX_ON_DISK)
		x->on_disk--;
	if (e == VSX_BAD)
		x->bad--;
	x->map[p] = slot + 1;
	vsx_hw_slot_page(x->hw, slot, p);
	x->in_vram++;
}

/* Seal n whole pages (pc[i].buf) into their fresh slots (pc[i].slot); ok[i] 1 if installed. A failed job
 * frees the fresh slots (kept out of use for the boot after -EHWPOISON, with the pages pinned). */
static void seal_pieces(struct vsx *x, struct vsx_piece *pc, unsigned int n, int *ok)
{
	unsigned int i, attempt;
	int ret = 0;

#ifdef VSX_PLAIN
	if (x->plain == 1) {
		for (i = 0; i < n; i++) {
			struct vsx_meta *m = vsx_hw_meta(x->hw, pc[i].slot);

			vsx_hw_write_slot(x->hw, pc[i].slot, pc[i].buf);
			memset(m, 0, sizeof(*m));
			m->flags = VSX_M_PLAIN;
			install(x, pc[i].p, pc[i].slot);
			ok[i] = 1;
			x->st.plain_writes++;
		}
		return;
	}
#endif
	for (attempt = 0;; attempt++) {
		for (i = 0; i < n; i++) {
			struct vseal_page *jp = &x->jp[i];

			memset(jp, 0, sizeof(*jp));
#ifdef VSX_PLAIN
			if (x->plain == 2) {	/* the same job, without the cipher */
				jp->src = EA(pc[i].buf);
				jp->dst = vsx_hw_slot_ea(x->hw, pc[i].slot);
				vsx_hw_touch(x->hw, pc[i].slot);
				continue;
			}
#endif
			jp->src = EA(pc[i].buf);
			jp->dst = vsx_hw_slot_ea(x->hw, pc[i].slot);
#ifdef VSX_FIXOFF_SAME_CTR			/* (the fix-off: the counter not bumped) */
			jp->ctr = x->next_ctr;
#else
			jp->ctr = x->next_ctr++;	/* fresh for every submission, retries too */
#endif
			vsx_ad(pc[i].p, pc[i].slot, VSX_FMT_RAW, jp->ad);
			vsx_hw_touch(x->hw, pc[i].slot);
		}
#ifdef VSX_PLAIN
		if (x->plain == 2)
			ret = vsx_hw_job(x->hw, VSEAL_OP_TEST_COPY, n, x->jp, x->jr, SPE_PATH_RAM);
		else
#endif
		ret = vsx_hw_job(x->hw, VSEAL_OP_SEAL, n, x->jp, x->jr, SPE_PATH_RAM);
		x->st.seal_jobs++;
		if (ret == SPE_RET_TIMEDOUT && attempt == 0) {
			x->st.resubmits++;
			continue;
		}
		break;
	}
	if (ret == 0) {
		for (i = 0; i < n; i++) {
			struct vsx_meta *m = vsx_hw_meta(x->hw, pc[i].slot);

			m->ctr = x->jp[i].ctr;
			memcpy(m->tag, x->jr[i].tag, 16);
			m->flags = VSX_M_SEALED;
#ifdef VSX_PLAIN
			if (x->plain == 2)
				m->flags = VSX_M_COPY;
#endif
			install(x, pc[i].p, pc[i].slot);
			ok[i] = 1;
			x->st.seals++;
		}
		return;
	}
	x->st.seal_fails++;
	vsx_hw_event(x->hw, VSX_EV_SEAL_FAIL, (vs_u32)-ret, n);
	for (i = 0; i < n; i++) {
		ok[i] = 0;
		if (ret == SPE_RET_KEEP) {	/* the SPE may still run: its slots and pages stay for the boot */
			vsx_hw_pin(x->hw, pc[i].ref);
			x->st.kept++;
		} else {
			vsx_hw_slot_free(x->hw, pc[i].slot);
		}
	}
	if (ret == SPE_RET_KEEP)
		vsx_hw_event(x->hw, VSX_EV_KEPT, 0, n);
	if (ret == SPE_RET_KEEP || ret == SPE_RET_NODEV)
		gone(x);
}

static void flush_writes(struct vsx *x)
{
	int ok[VSEAL_PAGES_MAX];
	unsigned int i;

	if (!x->nwr)
		return;
	seal_pieces(x, x->wr, x->nwr, ok);
	for (i = 0; i < x->nwr; i++)
		if (!ok[i])
			to_disk(x, &x->wr[i]);	/* the page as it is, at its place on the disk */
	x->nwr = 0;
}

/* a final tag failure: the entry becomes BAD (if it still points at that slot), the lockout counts it */
static void mark_bad(struct vsx *x, struct vsx_open *o)
{
	o->status = VSX_O_BAD;
	x->st.tag_fails++;
	if (x->map[o->p] == o->slot + 1) {
		x->map[o->p] = VSX_BAD;
		vsx_hw_slot_free(x->hw, o->slot);
		x->in_vram--;
		x->bad++;
		x->st.bad_marked++;
		vsx_hw_event(x->hw, VSX_EV_BAD, o->p, o->slot);
	}
	if (++x->fails >= x->lockout && !x->locked) {
		x->locked = 1;
		vsx_hw_event(x->hw, VSX_EV_LOCKOUT, x->fails, 0);
	}
}

/* the PPE opens one slot (the SPE gone): its copy into the private page, the tag checked over that copy,
 * decrypted in place, then copied out; from the bounce first, VRAM on a failure */
static void ppe_open(struct vsx *x, struct vsx_open *o)
{
	const vs_u8 *key = vsx_hw_key(x->hw);
	struct vsx_meta *m = vsx_hw_meta(x->hw, o->slot);
	vs_u8 nonce[12], ad[16];
	int from_bounce = o->bidx >= 0;

	if (!key || !x->scratch) {
		o->status = VSX_O_ERR;
		return;
	}
	v2_nonce_from_counter(m->ctr, nonce);
	vsx_ad(o->p, o->slot, VSX_FMT_RAW, ad);
	for (;;) {
		if (from_bounce)
			memcpy(x->scratch, vsx_hw_bounce_ptr(x->hw, o->bidx), VSX_PAGE);
		else
			vsx_hw_read_slot(x->hw, o->slot, x->scratch);
		if (!v2_aead_open(key, nonce, ad, 16, x->scratch, VSX_PAGE, x->scratch, m->tag)) {
			if (o->dst != x->scratch)
				memcpy(o->dst, x->scratch, VSX_PAGE);
			o->status = VSX_O_OK;
			x->st.opens_ppe++;
			return;
		}
#if !defined(VSX_FIXOFF_NO_RETRY) && !defined(VSX_FIXOFF_PPE_NO_RETRY)
		if (from_bounce) {
			from_bounce = 0;
			x->st.retries++;
			continue;
		}
#endif
		mark_bad(x, o);
		return;
	}
}

/* One SPE open job over the opens idx[0..k): from the bounce (R1) or from VRAM (R2). Sets each one's
 * status to OK, or leaves it 0 with *tagfail set for a tag mismatch. Returns the job's code. */
static int open_job(struct vsx *x, struct vsx_open *o, const unsigned int *idx, unsigned int k, int bounce,
		    int *tagfail)
{
	unsigned int i, attempt;
	int ret = 0;

	for (attempt = 0;; attempt++) {
		for (i = 0; i < k; i++) {
			struct vsx_open *t = &o[idx[i]];
			struct vsx_meta *m = vsx_hw_meta(x->hw, t->slot);
			struct vseal_page *jp = &x->jp[i];

			memset(jp, 0, sizeof(*jp));
			jp->src = bounce ? vsx_hw_bounce_ea(x->hw, t->bidx) : vsx_hw_slot_ea(x->hw, t->slot);
			jp->dst = EA(t->dst);
			jp->ctr = m->ctr;
			memcpy(jp->tag, m->tag, 16);
			vsx_ad(t->p, t->slot, VSX_FMT_RAW, jp->ad);
		}
		ret = vsx_hw_job(x->hw, VSEAL_OP_OPEN, k, x->jp, x->jr, bounce ? SPE_PATH_RAM : SPE_PATH_VRAM);
		if (ret == SPE_RET_TIMEDOUT && attempt == 0) {
			x->st.resubmits++;
			continue;
		}
		break;
	}
	if (ret != 0 && ret != SPE_RET_BADMSG)
		return ret;
	for (i = 0; i < k; i++) {
		struct vsx_open *t = &o[idx[i]];

#ifdef VSX_FIXOFF_IGNORE_TAG			/* (the fix-off: a refused page taken as opened) */
		x->jr[i].status = VSEAL_ST_OK;
#endif
		if (x->jr[i].status == VSEAL_ST_OK) {
			t->status = VSX_O_OK;
			if (bounce)
				x->st.opens_bounce++;
			else
				x->st.opens_vram++;
		} else {
			*tagfail = 1;
		}
	}
	return 0;
}

/* the job failed as a whole: the SPE gone (the PPE opens them), or kept (pinned, failed), or an error */
static void open_failed(struct vsx *x, struct vsx_open *o, const unsigned int *idx, unsigned int k, int ret)
{
	unsigned int i;

	if (ret == SPE_RET_NODEV)
		gone(x);
	for (i = 0; i < k; i++) {
		struct vsx_open *t = &o[idx[i]];

		if (ret == SPE_RET_NODEV) {
			ppe_open(x, t);
		} else {
			if (ret == SPE_RET_KEEP) {	/* the SPE may still write it: never handed back */
				vsx_hw_pin(x->hw, t->ref);
				x->st.kept++;
			}
			t->status = VSX_O_ERR;
		}
	}
	if (ret == SPE_RET_KEEP) {
		vsx_hw_event(x->hw, VSX_EV_KEPT, 0, k);
		gone(x);
	}
}

#ifdef VSX_PLAIN
/* test builds: the slots the SPE copied unsealed are copied back the same way (from the bounce or from VRAM);
 * without the SPE, by the PPE. Status 0 for every other slot. */
static void open_copies(struct vsx *x, struct vsx_open *o, unsigned int n)
{
	unsigned int i, k = 0, idx[VSEAL_PAGES_MAX];
	int ret = -1;

	for (i = 0; i < n; i++) {
		o[i].status = 0;
#ifdef VSX_FIXOFF_MODE_BY_SWITCH
		if (x->plain == 2)
#else
		if (vsx_hw_meta(x->hw, o[i].slot)->flags & VSX_M_COPY)
#endif
			idx[k++] = i;
	}
	if (!k)
		return;
	if (x->mode == VSX_SEAL) {
		for (i = 0; i < k; i++) {
			struct vsx_open *t = &o[idx[i]];

			memset(&x->jp[i], 0, sizeof(x->jp[i]));
			x->jp[i].src = t->bidx >= 0 ? vsx_hw_bounce_ea(x->hw, t->bidx) : vsx_hw_slot_ea(x->hw, t->slot);
			x->jp[i].dst = EA(t->dst);
		}
		ret = vsx_hw_job(x->hw, VSEAL_OP_TEST_COPY, k, x->jp, x->jr, SPE_PATH_RAM);
	}
	for (i = 0; i < k; i++) {
		struct vsx_open *t = &o[idx[i]];

		if (ret) {
			if (t->bidx >= 0)
				memcpy(t->dst, vsx_hw_bounce_ptr(x->hw, t->bidx), VSX_PAGE);
			else
				vsx_hw_read_slot(x->hw, t->slot, t->dst);
		}
		t->status = VSX_O_OK;
		x->st.plain_reads++;
	}
}
#endif

void vsx_open(struct vsx *x, struct vsx_open *o, unsigned int n)
{
	unsigned int i, k, idx[VSEAL_PAGES_MAX];
	int tagfail, ret;

#ifdef VSX_PLAIN
	open_copies(x, o, n);
#else
	for (i = 0; i < n; i++)
		o[i].status = 0;
#endif
	for (i = 0; i < n; i++) {
		struct vsx_meta *m = vsx_hw_meta(x->hw, o[i].slot);

		if (o[i].status)
			continue;
#ifdef VSX_PLAIN
#ifdef VSX_FIXOFF_MODE_BY_SWITCH		/* (the fix-off: read by the switch, not by how the slot was written) */
		if (x->plain == 1) {
#else
		if (m->flags & VSX_M_PLAIN) {
#endif
			if (o[i].bidx >= 0)
				memcpy(o[i].dst, vsx_hw_bounce_ptr(x->hw, o[i].bidx), VSX_PAGE);
			else
				vsx_hw_read_slot(x->hw, o[i].slot, o[i].dst);
			o[i].status = VSX_O_OK;
			x->st.plain_reads++;
			continue;
		}
#endif
		if (!(m->flags & VSX_M_SEALED))
			o[i].status = VSX_O_ERR;	/* (no seal recorded: never opened as if it were one) */
	}
	if (x->mode == VSX_DRAIN) {
		for (i = 0; i < n; i++)
			if (!o[i].status)
				ppe_open(x, &o[i]);
		return;
	}
	/* R1: from the bounce */
	for (i = k = 0; i < n; i++)
		if (!o[i].status && o[i].bidx >= 0)
			idx[k++] = i;
	if (k) {
		tagfail = 0;
		ret = open_job(x, o, idx, k, 1, &tagfail);
#ifndef VSX_FIXOFF_R1_FINAL			/* (the fix-off: a whole-job failure from the bounce is final) */
		if (ret && ret != SPE_RET_NODEV && ret != SPE_RET_KEEP) {
			/* failed as a whole (twice timed out, -EIO, -EINVAL), the slots untried: R2 opens them */
			for (i = 0; i < k; i++)
				if (!o[idx[i]].status) {
					o[idx[i]].bidx = -1;
					x->st.retries++;
				}
			ret = 0;
		}
#endif
		if (ret) {
			open_failed(x, o, idx, k, ret);
			if (x->mode == VSX_DRAIN) {	/* the rest by the PPE too */
				for (i = 0; i < n; i++)
					if (!o[i].status)
						ppe_open(x, &o[i]);
				return;
			}
		}
		if (tagfail) {
			for (i = 0; i < k; i++)
				if (!o[idx[i]].status) {
#ifdef VSX_FIXOFF_NO_RETRY			/* (the fix-off: a bounce failure is final) */
					mark_bad(x, &o[idx[i]]);
#else
					o[idx[i]].bidx = -1;	/* the retry from VRAM */
					x->st.retries++;
#endif
				}
		}
	}
	/* R2: from VRAM, the rest and the retries */
	for (i = k = 0; i < n; i++)
		if (!o[i].status && o[i].bidx < 0)
			idx[k++] = i;
	if (!k)
		return;
	tagfail = 0;
	ret = open_job(x, o, idx, k, 0, &tagfail);
	if (ret) {
		open_failed(x, o, idx, k, ret);
		return;
	}
	for (i = 0; i < k; i++)
		if (!o[idx[i]].status)
			mark_bad(x, &o[idx[i]]);
}

/* a partial piece on a page in VRAM: the slot opened into the private page, the piece merged, the page
 * sealed into a fresh slot from there (or written to the disk from a copy) */
static void merge(struct vsx *x, struct vsx_piece *pc, vs_u32 e)
{
	struct vsx_open o = { e - 1, pc->p, x->scratch, x->scratch_ref, pc->ctx, -1, 0 };
	void *buf = x->scratch;		/* (a pin during the seal replaces x->scratch; buf keeps the bytes) */
	int s, ok = 0;

	if (!x->scratch) {
		vsx_hw_fail(x->hw, pc->ctx);
		return;
	}
	vsx_open(x, &o, 1);
	if (o.status != VSX_O_OK) {
		vsx_hw_fail(x->hw, pc->ctx);
		return;
	}
	memcpy((vs_u8 *)buf + pc->in_page, pc->buf, pc->len);
	x->st.merges++;
	if (can_vram(x) && (s = vsx_hw_slot_alloc(x->hw)) >= 0) {
		struct vsx_piece m = *pc;

		m.buf = buf;
		m.ref = x->scratch_ref;
		m.in_page = 0;
		m.len = VSX_PAGE;
		m.slot = s;
		seal_pieces(x, &m, 1, &ok);
		if (ok)
			return;
	}
	if (vsx_hw_disk_copy(x->hw, pc->ctx, pc->p, buf, wb_busy(x, pc->p)))
		vsx_hw_fail(x->hw, pc->ctx);
	else
		vsx_set_disk(x, pc->p);
}

void vsx_begin(struct vsx *x)
{
	x->nwr = 0;
	x->nrd = 0;
}

/* whether page p has a write queued for the batch's seal */
static int queued(struct vsx *x, vs_u32 p)
{
	unsigned int i;

	for (i = 0; i < x->nwr; i++)
		if (x->wr[i].p == p)
			return 1;
	return 0;
}

void vsx_write(struct vsx *x, struct vsx_piece *pc)
{
	vs_u32 e;
	int s;

#ifndef VSX_FIXOFF_BATCH_REORDER		/* (the fix-off: a queued older write installed over this one) */
	if (queued(x, pc->p))
		flush_writes(x);		/* the older write first: the map then holds the newer one */
#endif
	e = x->map[pc->p];

	if (pc->len == VSX_PAGE) {
		if (e == VSX_BAD) {		/* a full write replaces it */
			x->map[pc->p] = 0;
			x->bad--;
		}
		if (can_vram(x) && (s = vsx_hw_slot_alloc(x->hw)) >= 0) {
			pc->slot = s;
			x->wr[x->nwr++] = *pc;
			if (x->nwr == VSEAL_PAGES_MAX)
				flush_writes(x);
			return;
		}
		if (x->mode != VSX_SEAL && !x->plain)
			x->st.to_disk_nospe++;
		to_disk(x, pc);
		return;
	}
	if (e == VSX_BAD) {
		vsx_hw_fail(x->hw, pc->ctx);
		return;
	}
	if (vsx_in_vram(e)) {
		flush_writes(x);		/* (in the order of the request) */
		merge(x, pc, e);
		return;
	}
#ifndef VSX_FIXOFF_PARTIAL_EMPTY
	if (!e) {			/* an empty page reads as zeros: written whole, never the piece alone */
		if (!x->scratch) {
			vsx_hw_fail(x->hw, pc->ctx);
			return;
		}
		memset(x->scratch, 0, VSX_PAGE);
		memcpy((vs_u8 *)x->scratch + pc->in_page, pc->buf, pc->len);
		if (vsx_hw_disk_copy(x->hw, pc->ctx, pc->p, x->scratch, wb_busy(x, pc->p)))
			vsx_hw_fail(x->hw, pc->ctx);
		else
			vsx_set_disk(x, pc->p);
		return;
	}
#endif
	to_disk(x, pc);
}

static void flush_reads(struct vsx *x)
{
	unsigned int i, n = x->nrd, base;

	for (base = 0; base < n; base += VSEAL_PAGES_MAX) {
		unsigned int k = n - base < VSEAL_PAGES_MAX ? n - base : VSEAL_PAGES_MAX;
		int gpu;

		for (i = 0; i < k; i++)
			x->bslots[i] = x->rd[base + i].slot;
		gpu = !vsx_hw_gpu_to_bounce(x->hw, x->bslots, k);
		for (i = 0; i < k; i++) {
			struct vsx_piece *pc = &x->rd[base + i];

			x->op[i] = (struct vsx_open){ pc->slot, pc->p, pc->buf, pc->ref, pc->ctx,
						      gpu ? (vs_s16)i : (vs_s16)-1, 0 };
		}
		vsx_open(x, x->op, k);
		for (i = 0; i < k; i++)
			if (x->op[i].status != VSX_O_OK)
				vsx_hw_fail(x->hw, x->rd[base + i].ctx);	/* nothing written to its page */
	}
	x->nrd = 0;
}

void vsx_read(struct vsx *x, struct vsx_piece *pc)
{
	vs_u32 e = x->map[pc->p];

	if (e == VSX_BAD) {
		vsx_hw_fail(x->hw, pc->ctx);
		return;
	}
	if (e == VSX_ON_DISK) {
		vsx_hw_disk(x->hw, pc, 0, wb_busy(x, pc->p));
		return;
	}
	if (!e) {
		vsx_hw_zero(x->hw, pc);
		return;
	}
	if (pc->len == VSX_PAGE && pc->in_page == 0) {
		pc->slot = e - 1;
		x->rd[x->nrd++] = *pc;
		return;
	}
	{	/* partial: the whole page opened into the private page, the piece copied */
		struct vsx_open o = { e - 1, pc->p, x->scratch, x->scratch_ref, pc->ctx, -1, 0 };

		if (x->scratch)
			vsx_open(x, &o, 1);
		if (!x->scratch || o.status != VSX_O_OK)
			vsx_hw_fail(x->hw, pc->ctx);
		else
			memcpy(pc->buf, (vs_u8 *)x->scratch + pc->in_page, pc->len);
	}
}

void vsx_discard(struct vsx *x, vs_u32 p)
{
	vs_u32 e = x->map[p];

	if (e == VSX_ON_DISK)
		x->on_disk--;
#ifdef VSX_FIXOFF_DISCARD_BAD			/* (the fix-off: BAD taken as a slot, as an earlier draft did) */
	else if (e) {
		vsx_hw_slot_free(x->hw, e - 1);
		x->in_vram--;
	}
#else
	else if (e == VSX_BAD)
		x->bad--;
	else
		leave_vram(x, e);
#endif
	x->map[p] = 0;
}

void vsx_writeback_open(struct vsx *x, const vs_u32 *slots, const vs_u32 *pages, unsigned int n, void **priv,
			void **priv_ref, int bounce_copied)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
#ifdef VSX_FIXOFF_WB_INTO_BOUNCE		/* (the fix-off: opened into the bounce, as the disk once read it) */
		void *dst = bounce_copied ? (void *)vsx_hw_bounce_ptr(x->hw, slots[i] % VSX_SLOTS) : priv[i];
#else
		void *dst = priv[i];		/* private pages: no plaintext in GART */
#endif
		x->op[i] = (struct vsx_open){ slots[i], pages[i], dst, priv_ref[i], NULL,
					      bounce_copied ? (vs_s16)(slots[i] % VSX_SLOTS) : (vs_s16)-1, 0 };
	}
	vsx_open(x, x->op, n);
}

void vsx_finish(struct vsx *x)
{
	flush_writes(x);
	if (x->nrd)
		flush_reads(x);
}
