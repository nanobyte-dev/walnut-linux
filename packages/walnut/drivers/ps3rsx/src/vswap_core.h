/* SPDX-License-Identifier: GPL-2.0 */
/*
 * vswap_core.h: the sealed VRAM swap's page logic (vswap_core.c), shared by vswap.c and the host model
 * (test/vswap-test.c). It decides where each piece of a request goes and what a map entry means, and runs
 * the seals and opens through the vsx_hw_*() calls, which vswap.c implements on the PS3 and the model on a
 * fake VRAM, bounce, disk and SPE.
 *
 * What a VRAM slot holds: ChaCha20-Poly1305 ciphertext of one page (RFC 8439; the SPE's spu/vseal.c),
 * under the load's key, with a per-key 64-bit counter as the nonce (a fresh one for every submission,
 * retries included) and AD = (device page, slot, format). The counter and the 16-byte tag stay in kernel
 * RAM (struct vsx_meta, one per slot), never in VRAM or GART.
 *
 * Map entries: 0 empty (reads as zeros), slot + 1 in VRAM, VSX_ON_DISK on the backing disk, VSX_BAD (a page
 * whose slot failed its tag after the retry: reads fail, a full write or a discard clears it, a partial
 * write fails, the write-back skips it).
 *
 * Writes. A full page goes to a fresh slot, sealed by the SPE straight from the request's page into VRAM
 * (the PPE first stores one zero word into the slot, so the SPE's put finds its translation); only after
 * the job's done word does the map switch to it, and an old slot of that page is freed then. No slot a
 * map entry points at is ever written. A partial piece on a page in VRAM is merged in a private page (the
 * slot opened into it) and sealed into a fresh slot from there. With no fresh slot, no SPE, the lockout,
 * or a seal that failed, the page goes to the disk.
 *
 * Reads, by the order of the paths: R1, the GPU copies the slots into the GART bounce and the SPE opens
 * them from there into the request's pages; R2, the SPE gets the slot from VRAM itself, used when the GPU
 * can't be had and to retry any page whose tag failed on the bounce. A tag that fails from VRAM is final:
 * the piece fails (the reader gets an I/O error, nothing written to its page), the entry becomes BAD, and
 * it counts toward the lockout (after `lockout` final failures in a load, no new page goes to VRAM).
 *
 * Write-back opens into private pages (never into the bounce: no plaintext in GART), the same R1/R2 order.
 *
 * The SPE gone. -ENODEV (given up quiet) or -EHWPOISON (given up unquiet) turns the swap to VSX_DRAIN: no
 * new page into VRAM, and every slot is opened by the PPE with the same core (spu/v2_aead.c), from the
 * bounce or from VRAM, through a private page: the rescue drain moves them all to the disk, and a read of
 * one before the drain got to it is opened the same way. After -EHWPOISON, every page the failed job named
 * is pinned for the boot and its slots stay out of use; a read piece of such a job fails.
 *
 * Test builds (VSX_PLAIN): with `plain` 1, new pages are stored unsealed by PPE stores, as before the seal;
 * with 2, the SPE copies them through the same paths as the seal, without the cipher (the cipher's own cost).
 * Each slot records how it was written, and is read back that way whatever the switch says now.
 *
 * Locking: the caller holds vs->lock around every vsx_*() call; the SPE client takes its own lock inside.
 */
#ifndef PS3RSX_VSWAP_CORE_H
#define PS3RSX_VSWAP_CORE_H

#include "spe_core.h"

#define VSX_PAGE		4096u
#define VSX_SLOTS		256u		/* slots a 1 MiB chunk */
#define VSX_ON_DISK		0x80000000u
#define VSX_BAD			(VSX_ON_DISK | 1)
#define VSX_PIECES		64		/* pieces of one request (32 pages, each split at most once) */
#define VSX_LOCKOUT		8		/* final tag failures a load before the lockout */
#define VSX_FMT_RAW		0		/* the AD's format byte: a whole page, uncompressed */

enum vsx_mode { VSX_OFF, VSX_SEAL, VSX_DRAIN };

/* per slot, in kernel RAM */
struct vsx_meta {
	vs_u64 ctr;			/* the nonce's counter of the slot's last seal */
	vs_u8 tag[16];
	vs_u8 flags;			/* VSX_M_* */
	vs_u8 pad[7];
};
#define VSX_M_SEALED	1
#define VSX_M_PLAIN	2		/* test builds: written unsealed by the PPE */
#define VSX_M_COPY	4		/* test builds: copied unsealed by the SPE (the cipher's cost measured) */

/* one piece of a request: within one device page */
struct vsx_piece {
	void *buf;			/* its bytes in kernel memory */
	void *ref;			/* the caller's page (for the disk and for pinning) */
	vs_u32 p;			/* device page */
	vs_u32 slot;			/* the slot it goes to or comes from */
	vs_u32 old;			/* writes: the map entry before */
	vs_u16 in_page, len;
	vs_u16 off;			/* within ref */
	vs_s16 bidx;			/* reads: its page in the bounce (R1), or -1 */
	void *ctx;			/* the caller's request */
};

/* an open: one slot into one destination */
struct vsx_open {
	vs_u32 slot, p;
	void *dst;			/* VSX_PAGE bytes of kernel RAM, not mapped for the GPU */
	void *ref;			/* dst's page, for pinning */
	void *ctx;			/* the request it is for (failures), or NULL */
	vs_s16 bidx;			/* the slot's copy in the bounce, or -1 */
	vs_u8 status;			/* VSX_O_* out */
};
#define VSX_O_OK	1
#define VSX_O_BAD	2		/* a final tag failure: the entry is BAD now (if it still pointed there) */
#define VSX_O_ERR	3		/* not opened (no SPE answer, kept pages): no BAD, the piece fails */

struct vsx_stats {
	vs_u64 seals, seal_jobs, opens_bounce, opens_vram, opens_ppe, plain_writes, plain_reads;
	vs_u64 retries, resubmits, tag_fails, bad_marked, seal_fails, kept, merges, to_disk_nospe;
};

struct vsx {
	void *hw;
	vs_u32 *map;
	vs_u32 npages;
	vs_u64 next_ctr;		/* the next seal's counter: never reused under the key */
	enum vsx_mode mode;
	int plain;			/* test builds: new pages unsealed, 1 by PPE stores, 2 by the SPE's copy */
	int locked;			/* the lockout: nothing new into VRAM */
	vs_u32 fails, lockout;
	vs_u32 in_vram, on_disk, bad;
	struct vsx_piece wr[VSX_PIECES];
	unsigned int nwr;
	struct vsx_piece rd[VSX_PIECES];
	unsigned int nrd;
	struct vsx_open op[VSEAL_PAGES_MAX];
	struct vseal_page jp[VSEAL_PAGES_MAX];
	struct vseal_result jr[VSEAL_PAGES_MAX];
	vs_u32 bslots[VSX_PIECES];
	const vs_u32 *wb_pages;		/* the device pages a write-back is writing to the disk now */
	unsigned int wb_n;
	void *scratch;			/* a private page: merges, partial reads, the PPE's opens */
	void *scratch_ref;
	struct vsx_stats st;
};

/* --- what vswap.c (or the model) provides; vs->lock is held -------------------------------------- */
int vsx_hw_slot_alloc(void *hw);			/* a fresh slot, or -1 */
void vsx_hw_slot_free(void *hw, vs_u32 slot);		/* (and marks it changed for a write-back) */
void vsx_hw_slot_page(void *hw, vs_u32 slot, vs_u32 p);	/* the device page a slot now holds */
struct vsx_meta *vsx_hw_meta(void *hw, vs_u32 slot);
vs_u64 vsx_hw_slot_ea(void *hw, vs_u32 slot);		/* the slot's mapping, as the SPE names it */
void vsx_hw_touch(void *hw, vs_u32 slot);		/* one zero store into a fresh slot, then mb() */
void vsx_hw_read_slot(void *hw, vs_u32 slot, void *dst);	/* PPE reads of the slot */
void vsx_hw_write_slot(void *hw, vs_u32 slot, const void *src);	/* PPE stores (test builds' plain slots) */
int vsx_hw_gpu_to_bounce(void *hw, const vs_u32 *slots, unsigned int n);	/* 0: slot i in bounce page i */
vs_u64 vsx_hw_bounce_ea(void *hw, unsigned int i);
const void *vsx_hw_bounce_ptr(void *hw, unsigned int i);
int vsx_hw_job(void *hw, unsigned int op, unsigned int n, const struct vseal_page *pg,
	       struct vseal_result *res, enum spe_path path);	/* ps3rsx_spe_job() */
const vs_u8 *vsx_hw_key(void *hw);			/* the SPE client's key, or NULL */
void vsx_hw_pin(void *hw, void *ref);			/* keep a page for the boot */
/* a disk piece, submitted after the lock; defer: held back until the write-back's own disk writes of that page
 * have completed (vsx->wb_pages), so the two writes to one place can't pass each other */
void vsx_hw_disk(void *hw, struct vsx_piece *pc, int write, int defer);
int vsx_hw_disk_copy(void *hw, void *ctx, vs_u32 p, const void *page, int defer);	/* a whole page's copy */
void vsx_hw_fail(void *hw, void *ctx);			/* the request fails (BLK_STS_IOERR) */
void vsx_hw_zero(void *hw, struct vsx_piece *pc);	/* an empty page's piece reads zeros */
void vsx_hw_event(void *hw, int what, vs_u32 p, vs_u32 slot);	/* VSX_EV_*: logs */
void vsx_hw_drain(void *hw);				/* the SPE is gone: start the rescue drain */
#define VSX_EV_BAD	1
#define VSX_EV_LOCKOUT	2
#define VSX_EV_GONE	3
#define VSX_EV_KEPT	4
#define VSX_EV_SEAL_FAIL 5

/* --- the core -------------------------------------------------------------------------------- */
static inline int vsx_in_vram(vs_u32 e)
{
	return e && e < VSX_ON_DISK;
}

void vsx_begin(struct vsx *x);
void vsx_write(struct vsx *x, struct vsx_piece *pc);
void vsx_read(struct vsx *x, struct vsx_piece *pc);
void vsx_discard(struct vsx *x, vs_u32 p);
void vsx_finish(struct vsx *x);
/* n opens (at most VSEAL_PAGES_MAX): R1 for those with a bounce copy, R2 for the rest and for retries;
 * the PPE in VSX_DRAIN. Results in o[i].status; BAD entries made and counted. */
void vsx_open(struct vsx *x, struct vsx_open *o, unsigned int n);
/* the page is on the disk now (its write-back went through): the map says so, its slot goes */
void vsx_set_disk(struct vsx *x, vs_u32 p);
/* write-back: n slots of one chunk (at most VSEAL_PAGES_MAX) opened into the private pages priv[i]; from the
 * bounce (slot % VSX_SLOTS) when the chunk was copied there. Results in x->op[i].status. */
void vsx_writeback_open(struct vsx *x, const vs_u32 *slots, const vs_u32 *pages, unsigned int n, void **priv,
			void **priv_ref, int bounce_copied);
/* the AD and nonce layout (spu/v2_crypto.h's v2_ad_make, v2_nonce_from_counter) */
void vsx_ad(vs_u32 p, vs_u32 slot, vs_u8 fmt, vs_u8 ad[16]);

#endif
