// SPDX-License-Identifier: GPL-2.0
/*
 * ps3vswap0: swap in spare VRAM, with a disk partition behind it (docs/PLAN.md "Memory").
 *
 * The PS3 has 256 MB of RAM and 256 MB of VRAM; X and a game use well under half of the VRAM.
 * This block device keeps each 4 KiB page either in a VRAM slot or at the same place on a
 * backing partition (the USB disk's swap partition), so the swap layer sees one device:
 *  - writes go to VRAM while the pool has room (1 MiB chunks from the CPU-visible VRAM heap,
 *    taken from its top end, at most vswap_vram_mb), else to the disk;
 *  - discards (swapon --discard=pages) free slots;
 *  - games come first: when a buffer object finds no VRAM, ps3rsx_vswap_shrink() writes the
 *    pages of the emptiest chunks back to the disk and hands the chunks over, each cleared
 *    before a buffer object gets it (vclear.c: the pages written back are still in it);
 *  - the VRAM holds the recently swapped: when the pool is (nearly) full, a background job
 *    moves the pages of its oldest chunk to the disk (pages that sat in swap longest without
 *    being read back are the cold ones; reads free their slots through discards).
 * Requests are served by our own work queue, on a fresh stack: blk-mq runs queue_rq inline from
 * a plug flush in direct reclaim, which was already deep in the stack (an IRQ found < 4 KiB left).
 *
 * Pages in VRAM are sealed (vswap_core.h): ChaCha20-Poly1305 on an SPE reserved for ps3rsx (spe.c), the
 * ciphertext in the slot, the tag and the counter in kernel RAM. A write is sealed by the SPE straight
 * from the page into a fresh slot; a read is copied by the GPU (M2MF, kcopy.c) into a pinned 1 MiB GART
 * bounce, ~600 MB/s, when the GPU can be had without waiting, and opened by the SPE from there into the
 * page, else the SPE gets the slot from VRAM itself; a write-back opens into private pages before the
 * disk writes. Without the SPE (none reserved, or vswap_seal=0), nothing goes into VRAM: the device is
 * its disk. If the SPE is lost later, the PPE opens the slots with the same cipher and a drain moves them
 * all to the disk. Test builds keep the unsealed path behind test_vswap_seal=0, for measurements.
 *
 * Locking: vs->lock covers the map and the pool, never across disk I/O (disk pieces of a request
 * complete it asynchronously; write-back copies under the lock and writes without it). The I/O
 * path never waits for mm_lock (anything holding mm_lock may be in reclaim, which writes to
 * swap): it only trylocks it to grow the pool. Chunks go back to the heap under mm_lock with
 * vs->lock dropped. The request path runs in a memalloc_noio scope.
 *
 * Set up from userspace (rootfs ps3-vswap.service): write the backing partition's path to
 * /sys/block/ps3vswap0/backing, then mkswap and swapon at a priority above the disk's.
 */
#include <linux/bio.h>
#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/highmem.h>
#include <linux/sched/mm.h>
#include <linux/seq_file.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "ps3rsx.h"
#include "spe.h"
#include "vswap_core.h"

#define CHUNK		SZ_1M
#define SLOTS		(CHUNK / PAGE_SIZE)	/* pages per chunk */
#define ON_DISK		VSX_ON_DISK		/* map entry: the page is on the backing disk */
#define WB_RESERVE	VSEAL_PAGES_MAX		/* private pages kept for a write-back under pressure */
#define MAX_RQ_PAGES	32			/* request size limit: bounds the bios per request */
#define BIO_RESERVE	(2 * MAX_RQ_PAGES)	/* pieces of one request (a page split at most once) */
#define HEADROOM	(8 * SLOTS)		/* free VRAM slots the demotion keeps in a full pool */

static unsigned int vswap_vram_mb = 96;
module_param(vswap_vram_mb, uint, 0444);
MODULE_PARM_DESC(vswap_vram_mb, "VRAM the swap device may hold (MiB, 0: none)");
static bool vswap_gpu = true;
module_param(vswap_gpu, bool, 0644);
MODULE_PARM_DESC(vswap_gpu, "Copy swap-ins and write-backs with the GPU (M2MF) when it's free");
static unsigned int vswap_gpu_min = 8;
module_param(vswap_gpu_min, uint, 0644);
MODULE_PARM_DESC(vswap_gpu_min, "Swap-in pages per request from which the GPU copies even when "
		 "busy (a queued frame can delay a copy by 16-33 ms)");
static unsigned int vswap_size_mb = 1024;
module_param(vswap_size_mb, uint, 0444);
MODULE_PARM_DESC(vswap_size_mb, "Swap device size (MiB, at most the backing partition's)");
static unsigned int vswap_seal_lockout = VSX_LOCKOUT;
module_param(vswap_seal_lockout, uint, 0444);
MODULE_PARM_DESC(vswap_seal_lockout, "Pages failing their check in a load after which no new page goes to VRAM");
#ifdef VSX_PLAIN
static bool test_vswap_seal = true;
module_param(test_vswap_seal, bool, 0644);
MODULE_PARM_DESC(test_vswap_seal, "TEST BUILD: seal new pages in VRAM (0: store them unsealed, as before, for measurements)");
#endif

struct vchunk {
	struct drm_mm_node node;	/* allocated while the chunk is in the pool */
	void __iomem *io;
	unsigned int nused;
	bool evicting;			/* being written back: no new pages into it */
	u64 born;			/* when it was (re)filled: the demotion takes the oldest */
	DECLARE_BITMAP(used, SLOTS);
	DECLARE_BITMAP(dirty, SLOTS);	/* rewritten or freed since the write-back copied it */
	u32 page[SLOTS];		/* device page in each used slot */
	struct vsx_meta *meta;		/* each slot's counter and tag (kernel RAM), while in the pool */
};

struct ps3rsx_vswap {
	struct ps3rsx_device *rsx;
	struct gendisk *disk;
	struct blk_mq_tag_set tags;
	struct bio_set bios;		/* own reserve: a request's disk bios are allocated under
					 * vs->lock and submitted after it, so under memory pressure
					 * the shared reserve (2 bios) deadlocked on our own bios */
	struct mutex lock;
	struct mutex shrink_lock;	/* one write-back at a time */
	struct file *backing;		/* the partition behind the VRAM */
	char backing_path[64];
	struct vsx x;			/* the map, the seals (vswap_core.h) */
	struct vchunk *chunks;
	unsigned int nchunks;		/* chunks[] entries (allocated or not) */
	unsigned int hint;		/* chunk to look for a free slot in first */
	unsigned int nalloc;		/* chunks in the pool */
	u64 seq;
	struct workqueue_struct *io_wq;	/* serves requests (ordered) */
	struct work_struct io_work;
	spinlock_t pending_lock;
	struct list_head pending;	/* requests for io_work */
	struct workqueue_struct *demote_wq;
	struct work_struct demote_work;
	struct work_struct drain_work;	/* the SPE is gone: every slot to the disk */
	u64 demoted;
	struct page *scratch;		/* vswap_core's private page */
	struct page *wb_reserve[WB_RESERVE];	/* a write-back's pages when none can be allocated */
	unsigned int pins;		/* pages pinned for the boot (-EHWPOISON) */
	struct bio_list deferred;	/* requests' disk bios held back behind a write-back's writes */
	/* GPU copies (kcopy.c) through a pinned GART bounce */
	struct drm_mm_node bounce_node;
	u32 bounce_io;			/* its GART IO address */
	void *bounce;			/* its kernel address (cacheable, IO-coherent) */
	bool bounce_ok;			/* pinned: GPU copies possible */
	bool bounce_evict;		/* a write-back owns it (until its disk writes complete) */
	bool bounce_late;		/* a timed-out copy may still write it, up to bounce_seq */
	u32 bounce_seq;
	struct kcopy_run runs[2 * MAX_RQ_PAGES];
	u64 gpu_reads, gpu_evicts, gpu_fallbacks;
	/* statistics (pages) */
	u64 disk_writes, disk_reads, evicted;
};

/* one request being served: its disk bios go out once vs->lock is dropped */
struct vreq {
	struct ps3rsx_vswap *vs;
	struct request *rq;
	struct bio_list bios;
};

static int vswap_major;

/* --- copies ------------------------------------------------------------------------------- */

static void to_vram(void __iomem *dst, const void *src, unsigned int len)
{
	const u64 *s = src;
	unsigned int i;

	for (i = 0; i < len; i += 8)
		__raw_writeq(*s++, dst + i);
	mb();
}

static void from_vram(void *dst, const void __iomem *src, unsigned int len)
{
	u64 *d = dst;
	unsigned int i;

	for (i = 0; i < len; i += 8)
		*d++ = __raw_readq(src + i);
}

static void __iomem *slot_io(struct ps3rsx_vswap *vs, u32 slot)
{
	return vs->chunks[slot / SLOTS].io + (slot % SLOTS) * PAGE_SIZE;
}

/* --- the disk behind ---------------------------------------------------------------------- */

/*
 * Disk I/O never waits under vs->lock or in the request path: the block layer's workers that
 * run our queue also dispatch the disk's requests (waiting there deadlocked the first version).
 * A request completes when its last piece does.
 */
#define RQ_BIOS		(2 * MAX_RQ_PAGES)	/* bios of one request with their own status (4 KiB blocks: 32) */

/* one bio of a request: its pieces' context, so a failure ends only its own bio */
struct vbio {
	struct request *rq;
	struct vreq *r;			/* while io_work serves the request */
	unsigned int idx;
};

struct vswap_cmd {
	atomic_t pending;
	blk_status_t status;		/* the whole request (no map, out of range, an unknown op) */
	blk_status_t st[RQ_BIOS];	/* each bio's, in the request's order */
	struct vbio b[RQ_BIOS];
	struct list_head node;		/* on vs->pending until io_work serves it */
};

/* the request ends bio by bio: a page that failed its check fails only its own bio (its own swap entry) */
static void cmd_put(struct request *rq)
{
	struct vswap_cmd *cmd = blk_mq_rq_to_pdu(rq);
	unsigned int k = 0;

	if (!atomic_dec_and_test(&cmd->pending))
		return;
	if (cmd->status) {
		blk_mq_end_request(rq, cmd->status);
		return;
	}
	while (rq->bio) {
		blk_status_t st = cmd->st[min(k, (unsigned int)RQ_BIOS - 1)];

		k++;
		if (!blk_update_request(rq, st, rq->bio->bi_iter.bi_size))
			break;
	}
	__blk_mq_end_request(rq, BLK_STS_OK);
}

static void disk_end_io(struct bio *bio)
{
	struct vbio *b = bio->bi_private;
	struct request *rq = b->rq;
	struct vswap_cmd *cmd = blk_mq_rq_to_pdu(rq);

	if (bio->bi_status)
		cmd->st[b->idx] = bio->bi_status;
	bio_put(bio);
	cmd_put(rq);
}

/* A bio for one piece of rq at the same place on the disk, submitted once vs->lock is dropped */
static struct bio *disk_bio(struct ps3rsx_vswap *vs, struct vbio *b, blk_opf_t op, u32 p,
			    unsigned int in_page, struct page *page, unsigned int len,
			    unsigned int off)
{
	struct vswap_cmd *cmd = blk_mq_rq_to_pdu(b->rq);
	struct bio *bio = bio_alloc_bioset(file_bdev(vs->backing), 1, op, GFP_NOIO, &vs->bios);

	bio->bi_iter.bi_sector = ((sector_t)p << (PAGE_SHIFT - 9)) + (in_page >> 9);
	__bio_add_page(bio, page, len, off);
	bio->bi_private = b;
	bio->bi_end_io = disk_end_io;
	atomic_inc(&cmd->pending);
	return bio;
}

/* --- the VRAM pool ------------------------------------------------------------------------ */

/* A free slot, growing the pool if it may; -1 if none. vs->lock held. */
static int slot_alloc(struct ps3rsx_vswap *vs)
{
	struct ps3rsx_device *rsx = vs->rsx;
	struct vsx_meta *meta;
	unsigned int i, n;
	struct vchunk *c;
	int ret;

	for (n = 0; n < vs->nchunks; n++) {
		i = (vs->hint + n) % vs->nchunks;
		c = &vs->chunks[i];
		if (drm_mm_node_allocated(&c->node) && c->io && !c->evicting && c->nused < SLOTS)
			goto found;
	}
	/* grow: a chunk from the top of the CPU-visible VRAM, if nothing else holds the heap */
	for (i = 0; i < vs->nchunks && drm_mm_node_allocated(&vs->chunks[i].node); i++)
		;
	if (i == vs->nchunks)
		return -1;
	/* its counters and tags (kernel RAM, for as long as the chunk is in the pool) before the node, and
	 * mm_lock only trylocked: vs->lock is held, and whoever holds mm_lock may wait for this device */
	meta = kcalloc(SLOTS, sizeof(*meta), GFP_NOWAIT | __GFP_NOWARN);
	if (!meta || !mutex_trylock(&rsx->mm_lock)) {
		kfree(meta);
		return -1;
	}
	c = &vs->chunks[i];
	memset(&c->node, 0, sizeof(c->node));
	ret = drm_mm_insert_node_generic(&rsx->vram_cpu, &c->node, CHUNK, CHUNK, 0,
					 DRM_MM_INSERT_HIGH);
	if (!ret) {
		c->io = ioremap(rsx->info.ddr_lpar + c->node.start, CHUNK);
		if (!c->io)
			drm_mm_remove_node(&c->node);
	}
	mutex_unlock(&rsx->mm_lock);
	if (ret || !c->io) {
		kfree(meta);
		if (ret)
			ps3rsx_keep_pressure_async(rsx);	/* VRAM kept for reuse goes to the clear */
		return -1;
	}
	c->meta = meta;
	c->nused = 0;
	c->evicting = false;
	c->born = ++vs->seq;
	vs->nalloc++;
	bitmap_zero(c->used, SLOTS);
found:
	vs->hint = i;
	n = find_first_zero_bit(c->used, SLOTS);
	set_bit(n, c->used);
	c->nused++;
	return i * SLOTS + n;
}

static void slot_free(struct ps3rsx_vswap *vs, u32 slot)
{
	struct vchunk *c = &vs->chunks[slot / SLOTS];

	clear_bit(slot % SLOTS, c->used);
	set_bit(slot % SLOTS, c->dirty);	/* a write-back in flight is stale now */
	c->nused--;
}

/* a chunk leaves the pool (empty): its mapping and its metadata go */
static void chunk_unmap(struct vchunk *c)
{
	iounmap(c->io);
	c->io = NULL;
	kfree(c->meta);
	c->meta = NULL;
}

/* --- requests ----------------------------------------------------------------------------- */

/* A full (or no longer growing) pool: let the demotion make room for new pages. vs->lock held. */
static void maybe_demote(struct ps3rsx_vswap *vs, bool alloc_failed)
{
	if (alloc_failed || (vs->nalloc == vs->nchunks && vs->nalloc * SLOTS - vs->x.in_vram < HEADROOM))
		queue_work(vs->demote_wq, &vs->demote_work);
}

/* The bounce is free for a GPU copy. vs->lock held. */
static bool bounce_free(struct ps3rsx_vswap *vs)
{
	if (!vs->bounce_ok || vs->bounce_evict)
		return false;
	if (vs->bounce_late) {
		if (!ps3rsx_seq_done(vs->rsx, vs->bounce_seq))
			return false;
		vs->bounce_late = false;
	}
	return true;
}

/* A copy failed: count it, and if it may still land, keep the bounce until it has */
static void gpu_failed(struct ps3rsx_vswap *vs, int err, u32 seq)
{
	if (err == -ETIMEDOUT) {
		vs->bounce_late = true;
		vs->bounce_seq = seq;
	}
	vs->gpu_fallbacks++;
}

/* --- vswap_core's hardware calls (vs->lock held) ------------------------------------------- */

int vsx_hw_slot_alloc(void *hw)
{
	struct ps3rsx_vswap *vs = hw;
	int slot = slot_alloc(vs);

	maybe_demote(vs, slot < 0);
	return slot;
}

void vsx_hw_slot_free(void *hw, u32 slot)
{
	slot_free(hw, slot);
}

void vsx_hw_slot_page(void *hw, u32 slot, u32 p)
{
	struct ps3rsx_vswap *vs = hw;

	vs->chunks[slot / SLOTS].page[slot % SLOTS] = p;
}

struct vsx_meta *vsx_hw_meta(void *hw, u32 slot)
{
	struct ps3rsx_vswap *vs = hw;

	return &vs->chunks[slot / SLOTS].meta[slot % SLOTS];
}

u64 vsx_hw_slot_ea(void *hw, u32 slot)
{
	return (unsigned long)slot_io(hw, slot);
}

/* The SPE's put then finds the slot's translation: one store of the constant zero into the fresh slot
 * (overwritten by the put), ordered before the job */
void vsx_hw_touch(void *hw, u32 slot)
{
	__raw_writeq(0, slot_io(hw, slot));
	mb();
}

void vsx_hw_read_slot(void *hw, u32 slot, void *dst)
{
	from_vram(dst, slot_io(hw, slot), PAGE_SIZE);
}

void vsx_hw_write_slot(void *hw, u32 slot, const void *src)
{
	to_vram(slot_io(hw, slot), src, PAGE_SIZE);
}

/* The GPU copies the slots into the bounce (slot i into its page i), when it can be had without waiting */
int vsx_hw_gpu_to_bounce(void *hw, const u32 *slots, unsigned int n)
{
	struct ps3rsx_vswap *vs = hw;
	struct ps3rsx_device *rsx = vs->rsx;
	unsigned int i, nr = 0;
	int err;
	u32 seq;

	if (!vswap_gpu || !bounce_free(vs) || (n < vswap_gpu_min && !ps3rsx_exec_idle(rsx)) ||
	    ps3rsx_kcopy_begin(rsx))
		return -EBUSY;
	for (i = 0; i < n; i++) {	/* merge neighbours */
		struct vchunk *c = &vs->chunks[slots[i] / SLOTS];
		u32 vram = c->node.start + (slots[i] % SLOTS) * PAGE_SIZE;
		u32 gart = vs->bounce_io + i * PAGE_SIZE;
		struct kcopy_run *r = nr ? &vs->runs[nr - 1] : NULL;

		if (r && r->vram + r->npages * PAGE_SIZE == vram && r->gart + r->npages * PAGE_SIZE == gart)
			r->npages++;
		else
			vs->runs[nr++] = (struct kcopy_run){ vram, gart, 1 };
	}
	err = ps3rsx_kcopy_to_gart(rsx, vs->runs, nr, &seq);
	ps3rsx_kcopy_end(rsx);
	if (!err)
		err = ps3rsx_kcopy_wait(rsx, seq, 50000);
	if (err) {
		gpu_failed(vs, err, seq);
		return err;
	}
	vs->gpu_reads += n;
	return 0;
}

u64 vsx_hw_bounce_ea(void *hw, unsigned int i)
{
	struct ps3rsx_vswap *vs = hw;

	return (unsigned long)vs->bounce + i * PAGE_SIZE;
}

const void *vsx_hw_bounce_ptr(void *hw, unsigned int i)
{
	struct ps3rsx_vswap *vs = hw;

	return vs->bounce + i * PAGE_SIZE;
}

int vsx_hw_job(void *hw, unsigned int op, unsigned int n, const struct vseal_page *pg, struct vseal_result *res,
	       enum spe_path path)
{
	struct ps3rsx_vswap *vs = hw;

	return ps3rsx_spe_job(vs->rsx, op, n, pg, res, path);
}

const u8 *vsx_hw_key(void *hw)
{
	struct ps3rsx_vswap *vs = hw;

	return ps3rsx_spe_key(vs->rsx);
}

/* a page the SPE may still write: never given back this boot (an extra reference); the private page in
 * use is replaced */
void vsx_hw_pin(void *hw, void *ref)
{
	struct ps3rsx_vswap *vs = hw;

	unsigned int k;

	get_page(ref);
	vs->pins++;			/* a write-back's own pages are replaced after its chunk (chunk_evict) */
	if (ref == vs->scratch) {
		vs->scratch = alloc_page(GFP_NOWAIT | __GFP_NOWARN);
		vs->x.scratch = vs->scratch ? page_address(vs->scratch) : NULL;
		vs->x.scratch_ref = vs->scratch;
	}
	for (k = 0; k < WB_RESERVE; k++)
		if (ref == vs->wb_reserve[k])
			vs->wb_reserve[k] = alloc_page(GFP_NOWAIT | __GFP_NOWARN);
}

void vsx_hw_disk(void *hw, struct vsx_piece *pc, int write, int defer)
{
	struct ps3rsx_vswap *vs = hw;
	struct vbio *b = pc->ctx;
	struct bio *bio = disk_bio(vs, b, write ? REQ_OP_WRITE : REQ_OP_READ, pc->p, pc->in_page, pc->ref,
				   pc->len, pc->off);

	/* a write-back is writing that page now: after it (chunk_evict submits these) */
	bio_list_add(defer ? &vs->deferred : &b->r->bios, bio);
	if (write)
		vs->disk_writes++;
	else
		vs->disk_reads++;
}

static void disk_copy_end_io(struct bio *bio)
{
	__free_page(bio_first_page_all(bio));
	disk_end_io(bio);
}

/* a whole page's copy to its place on the disk (a merged page with no slot for it) */
int vsx_hw_disk_copy(void *hw, void *ctx, u32 p, const void *data, int defer)
{
	struct ps3rsx_vswap *vs = hw;
	struct vbio *b = ctx;
	struct page *page = alloc_page(GFP_NOIO | __GFP_NOWARN);
	struct bio *bio;

	if (!page)
		return -ENOMEM;
	memcpy(page_address(page), data, PAGE_SIZE);
	bio = disk_bio(vs, b, REQ_OP_WRITE, p, 0, page, PAGE_SIZE, 0);
	bio->bi_end_io = disk_copy_end_io;
	bio_list_add(defer ? &vs->deferred : &b->r->bios, bio);
	vs->disk_writes++;
	return 0;
}

void vsx_hw_fail(void *hw, void *ctx)
{
	struct vbio *b = ctx;

	if (b)			/* that bio only */
		((struct vswap_cmd *)blk_mq_rq_to_pdu(b->rq))->st[b->idx] = BLK_STS_IOERR;
}

void vsx_hw_zero(void *hw, struct vsx_piece *pc)
{
	memset(pc->buf, 0, pc->len);
}

void vsx_hw_event(void *hw, int what, u32 a, u32 b)
{
	switch (what) {
	case VSX_EV_BAD:
		pr_err_ratelimited("ps3vswap: page %u (slot %u) failed its check: it reads as an I/O error now\n",
				   a, b);
		break;
	case VSX_EV_LOCKOUT:
		pr_err("ps3vswap: %u pages failed their check in this load: no new page goes to VRAM until the next load\n",
		       a);
		break;
	case VSX_EV_GONE:
		pr_err("ps3vswap: the SPE is gone: the pages in VRAM are opened on the PPE and moved to the disk\n");
		break;
	case VSX_EV_KEPT:
		pr_err("ps3vswap: the SPE wasn't seen stopped: %u pages of its last job are kept for the boot\n", b);
		break;
	case VSX_EV_SEAL_FAIL:
		pr_err_ratelimited("ps3vswap: a seal of %u pages failed (-%u): they went to the disk\n", b, a);
		break;
	}
}

void vsx_hw_drain(void *hw)
{
	struct ps3rsx_vswap *vs = hw;

	queue_work(vs->demote_wq, &vs->drain_work);
}

/* --- discards (vs->lock held) --------------------------------------------------------------- */

static void do_discard(struct ps3rsx_vswap *vs, sector_t sector, unsigned int bytes)
{
	u32 p = DIV_ROUND_UP(sector, PAGE_SIZE >> 9);
	u32 end = (sector + (bytes >> 9)) >> (PAGE_SHIFT - 9);

	for (; p < end && p < vs->x.npages; p++)
		vsx_discard(&vs->x, p);
}

/* Serve one request (io_work, on its own stack) */
static void vswap_do_rq(struct ps3rsx_vswap *vs, struct request *rq)
{
	struct vswap_cmd *cmd = blk_mq_rq_to_pdu(rq);
	u64 pos = (u64)blk_rq_pos(rq) << 9;
	struct vreq r = { vs, rq };
	struct bvec_iter iter;
	unsigned int noio, k = 0;
	struct bio_vec bv;
	struct bio *bio, *rb;

	atomic_set(&cmd->pending, 1);
	cmd->status = BLK_STS_OK;
	memset(cmd->st, 0, sizeof(cmd->st));
	bio_list_init(&r.bios);
	noio = memalloc_noio_save();
	mutex_lock(&vs->lock);
	if (!vs->x.map) {
		cmd->status = BLK_STS_IOERR;
		goto out;
	}
#ifdef VSX_PLAIN
	vs->x.plain = !READ_ONCE(test_vswap_seal);
#endif
	vsx_begin(&vs->x);
	switch (req_op(rq)) {
	case REQ_OP_FLUSH:
		break;
	case REQ_OP_DISCARD:
	case REQ_OP_WRITE_ZEROES:
		do_discard(vs, blk_rq_pos(rq), blk_rq_bytes(rq));
		break;
	case REQ_OP_READ:
	case REQ_OP_WRITE:
		__rq_for_each_bio(rb, rq) {
			struct vbio *b = &cmd->b[min(k, (unsigned int)RQ_BIOS - 1)];

			*b = (struct vbio){ rq, &r, min(k, (unsigned int)RQ_BIOS - 1) };
			k++;
			bio_for_each_segment(bv, rb, iter) {
				unsigned int done = 0;

				/* in pieces that don't cross a device page */
				while (done < bv.bv_len) {
					struct vsx_piece pc = {
						.ref = bv.bv_page,
						.p = pos >> PAGE_SHIFT,
						.in_page = pos & (PAGE_SIZE - 1),
						.off = bv.bv_offset + done,
						.bidx = -1,
						.ctx = b,
					};

					pc.len = min(bv.bv_len - done, (unsigned int)PAGE_SIZE - pc.in_page);
					pc.buf = page_address(bv.bv_page) + pc.off;	/* (no highmem on ppc64) */
					if (pc.p >= vs->x.npages) {
						cmd->status = BLK_STS_IOERR;
						goto out;
					}
					if (op_is_write(req_op(rq)))
						vsx_write(&vs->x, &pc);
					else
						vsx_read(&vs->x, &pc);
					done += pc.len;
					pos += pc.len;
				}
			}
		}
		break;
	default:
		cmd->status = BLK_STS_NOTSUPP;
	}
out:
	vsx_finish(&vs->x);		/* the seals and the opens of the request */
	mutex_unlock(&vs->lock);
	while ((bio = bio_list_pop(&r.bios)))
		submit_bio(bio);
	memalloc_noio_restore(noio);
	cmd_put(rq);
}

static void vswap_io_work(struct work_struct *work)
{
	struct ps3rsx_vswap *vs = container_of(work, struct ps3rsx_vswap, io_work);
	struct vswap_cmd *cmd;

	for (;;) {
		spin_lock_irq(&vs->pending_lock);
		cmd = list_first_entry_or_null(&vs->pending, struct vswap_cmd, node);
		if (cmd)
			list_del(&cmd->node);
		spin_unlock_irq(&vs->pending_lock);
		if (!cmd)
			break;
		vswap_do_rq(vs, blk_mq_rq_from_pdu(cmd));
	}
}

/* Hand the request to io_work: the caller may be deep in reclaim */
static blk_status_t vswap_queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bd)
{
	struct ps3rsx_vswap *vs = hctx->queue->queuedata;
	struct vswap_cmd *cmd = blk_mq_rq_to_pdu(bd->rq);
	unsigned long flags;

	blk_mq_start_request(bd->rq);
	spin_lock_irqsave(&vs->pending_lock, flags);
	list_add_tail(&cmd->node, &vs->pending);
	spin_unlock_irqrestore(&vs->pending_lock, flags);
	queue_work(vs->io_wq, &vs->io_work);
	return BLK_STS_OK;
}

static const struct blk_mq_ops vswap_mq_ops = {
	.queue_rq = vswap_queue_rq,
};

static const struct block_device_operations vswap_fops = {
	.owner = THIS_MODULE,
};

/* --- giving VRAM back to games ------------------------------------------------------------ */

struct wb_batch {
	atomic_t pending;
	struct completion done;
	blk_status_t status;
};

static void wb_end_io(struct bio *bio)
{
	struct wb_batch *b = bio->bi_private;

	if (bio->bi_status)
		b->status = bio->bi_status;
	bio_put(bio);
	if (atomic_dec_and_test(&b->pending))
		complete(&b->done);
}

/*
 * chunk_evict()'s buffers: the page numbers and the private pages its slots are opened into before the
 * disk writes (never the bounce: no plaintext in GART). A chunk's worth (256 pages) when they can be had,
 * else the swap's own reserve of WB_RESERVE pages, used batch by batch.
 */
struct evict_buf {
	u32 *pages;			/* the device pages written, in the order of w */
	struct page **w;		/* the opened pages to write */
	struct page **own;
	unsigned int nown;		/* 256 own pages, or 0: the reserve */
	gfp_t gfp;
};

static bool evict_buf_init(struct evict_buf *eb, gfp_t gfp)
{
	eb->gfp = gfp;
	eb->own = NULL;
	eb->nown = 0;
	eb->pages = kcalloc(SLOTS, sizeof(*eb->pages), gfp);
	eb->w = kcalloc(SLOTS, sizeof(*eb->w), gfp);
	return eb->pages && eb->w;
}

/* the private pages: before vs->lock is taken, never under it */
static void evict_own_alloc(struct evict_buf *eb)
{
	unsigned int s;

	if (eb->nown)
		return;
	eb->own = kcalloc(SLOTS, sizeof(*eb->own), eb->gfp | __GFP_NOWARN);
	for (s = 0; eb->own && s < SLOTS; s++)
		if (!(eb->own[s] = alloc_page(eb->gfp | __GFP_NOWARN)))
			break;
	if (eb->own && s == SLOTS) {
		eb->nown = SLOTS;
		return;
	}
	while (eb->own && s--)
		__free_page(eb->own[s]);
	kfree(eb->own);
	eb->own = NULL;
}

static void evict_buf_free_own(struct evict_buf *eb)
{
	unsigned int s;

	for (s = 0; s < eb->nown; s++)
		__free_page(eb->own[s]);	/* (a pinned one keeps its extra reference) */
	kfree(eb->own);
	eb->own = NULL;
	eb->nown = 0;
}

static void evict_buf_free(struct evict_buf *eb)
{
	evict_buf_free_own(eb);
	kfree(eb->w);
	kfree(eb->pages);
}

/* the disk writes of the opened pages w[0..n), waited for */
static blk_status_t evict_write(struct ps3rsx_vswap *vs, struct page **w, const u32 *pages, unsigned int n)
{
	struct wb_batch b;
	unsigned int k, noio;

	atomic_set(&b.pending, 1);
	init_completion(&b.done);
	b.status = BLK_STS_OK;
	noio = memalloc_noio_save();
	for (k = 0; k < n; k++) {
		struct bio *bio = bio_alloc(file_bdev(vs->backing), 1, REQ_OP_WRITE, GFP_NOIO);

		bio->bi_iter.bi_sector = (sector_t)pages[k] << (PAGE_SHIFT - 9);
		__bio_add_page(bio, w[k], PAGE_SIZE, 0);
		bio->bi_private = &b;
		bio->bi_end_io = wb_end_io;
		atomic_inc(&b.pending);
		submit_bio(bio);
	}
	memalloc_noio_restore(noio);
	if (!atomic_dec_and_test(&b.pending))
		wait_for_completion_io(&b.done);
	return b.status;
}

/*
 * Write chunk i's pages to the disk. With the GPU, the whole chunk is copied into the bounce first; then,
 * a batch of up to VSEAL_PAGES_MAX at a time under vs->lock, the slots are opened into private pages
 * (vsx_writeback_open: from the bounce, a failure retried from VRAM; or from VRAM; or by the PPE once the
 * SPE is gone). A page whose slot fails its check becomes BAD and isn't written. The disk is written from
 * the private pages without the lock, and a page switches to the disk only if nothing rewrote or discarded
 * it meanwhile (c->dirty). release: take the emptied chunk out of the pool (its node then goes back to the
 * heap, by the caller under mm_lock); else it stays, empty, as the pool's newest chunk.
 * Returns true if the chunk was emptied. Under vs->shrink_lock.
 */
static bool chunk_evict(struct ps3rsx_vswap *vs, unsigned int i, struct evict_buf *eb,
			bool release)
{
	struct ps3rsx_device *rsx = vs->rsx;
	struct vchunk *c = &vs->chunks[i];
	struct page **w = eb->w;
	u32 slots[VSEAL_PAGES_MAX], pg[VSEAL_PAGES_MAX], *wpages = eb->pages;
	void *priv[VSEAL_PAGES_MAX], *ref[VSEAL_PAGES_MAX];
	unsigned int s = 0, nw = 0, k, n, batch, pins0;
	blk_status_t st = BLK_STS_OK;
	struct bio_list held;
	bool empty, gpu = false;
	struct bio *bio;
	u32 seq;

	evict_own_alloc(eb);
	if (!eb->nown)
		for (k = 0; k < WB_RESERVE; k++)
			if (!vs->wb_reserve[k])
				return false;	/* (a reserve page lost to a pin, not replaced) */
	bio_list_init(&held);
	mutex_lock(&vs->lock);
	pins0 = vs->pins;
	c->evicting = true;			/* no new pages into it */
	bitmap_zero(c->dirty, SLOTS);
	/* the whole chunk in one GPU copy into the bounce, opened from there */
	if (vswap_gpu && bounce_free(vs) && !ps3rsx_kcopy_begin(rsx)) {
		struct kcopy_run r = { c->node.start, vs->bounce_io, SLOTS };
		int err = ps3rsx_kcopy_to_gart(rsx, &r, 1, &seq);

		ps3rsx_kcopy_end(rsx);
		if (!err)
			err = ps3rsx_kcopy_wait(rsx, seq, 100000);
		if (!err) {
			gpu = true;
			vs->bounce_evict = true;
			vs->gpu_evicts++;
		} else {
			gpu_failed(vs, err, seq);
		}
	}
	/* from the bounce a whole batch under the lock; from VRAM (about 250 us a page) smaller ones */
	batch = gpu ? VSEAL_PAGES_MAX : 8;
	while (s < SLOTS) {
		if (!eb->nown)
			for (k = 0; k < WB_RESERVE; k++)
				if (!vs->wb_reserve[k])
					goto out;	/* (a reserve page lost to a pin) */
		for (n = 0; s < SLOTS && n < batch; s++) {
			u32 p = c->page[s];

			if (!test_bit(s, c->used) || vs->x.map[p] != i * SLOTS + s + 1)
				continue;	/* free, or kept out of use */
			ref[n] = eb->nown ? eb->own[s] : vs->wb_reserve[n];
			priv[n] = page_address(ref[n]);
			slots[n] = i * SLOTS + s;
			pg[n++] = p;
		}
		if (n) {
			vsx_writeback_open(&vs->x, slots, pg, n, priv, ref, gpu);
			for (k = 0; k < n; k++)
				if (vs->x.op[k].status == VSX_O_OK) {
					w[nw] = ref[k];
					wpages[nw++] = pg[k];
				}
		}
		if (eb->nown && s < SLOTS) {		/* own pages: all opened first, written at the end */
			mutex_unlock(&vs->lock);
			cond_resched();
			mutex_lock(&vs->lock);
			continue;
		}
		if (!nw)
			continue;
		/* written now (the reserve's pages are reused by the next batch) */
		if (gpu && s == SLOTS)
			vs->bounce_evict = false;	/* everything opened: the bounce is free */
		vs->x.wb_pages = wpages;		/* a request's disk I/O of these pages waits */
		vs->x.wb_n = nw;
		mutex_unlock(&vs->lock);
		while ((bio = bio_list_pop(&held)))
			submit_bio(bio);
		if (evict_write(vs, w, wpages, nw) != BLK_STS_OK)
			st = BLK_STS_IOERR;
		mutex_lock(&vs->lock);
		for (k = 0; k < nw && st == BLK_STS_OK; k++) {
			u32 p = wpages[k], e = vs->x.map[p];

			if (!vsx_in_vram(e) || (e - 1) / SLOTS != i || test_bit((e - 1) % SLOTS, c->dirty))
				continue;		/* changed while being written: stays */
			vsx_set_disk(&vs->x, p);
			vs->evicted++;
		}
		vs->x.wb_n = 0;			/* written: what was held back goes out next */
		bio_list_merge(&held, &vs->deferred);
		bio_list_init(&vs->deferred);
		nw = 0;
		if (st != BLK_STS_OK)
			break;
	}
out:
	if (gpu)
		vs->bounce_evict = false;
	if (st != BLK_STS_OK)
		pr_err_ratelimited("ps3vswap: write-back failed: chunk kept\n");
	empty = !c->nused;
	if (empty && release) {
		chunk_unmap(c);
		vs->nalloc--;
	} else if (empty) {
		c->born = ++vs->seq;
	}
	c->evicting = false;
	vs->x.wb_n = 0;
	bio_list_merge(&held, &vs->deferred);
	bio_list_init(&vs->deferred);
	if (vs->pins != pins0 && eb->nown) {	/* a buried SPE may still write some: none of them reused */
		evict_buf_free_own(eb);
	}
	mutex_unlock(&vs->lock);
	while ((bio = bio_list_pop(&held)))
		submit_bio(bio);
	return empty;
}

/*
 * Free bytes plus one chunk of the pool's VRAM for a buffer object (bytes 0: one chunk): empty
 * chunks first, then the emptiest, their pages written back to the disk. Returns true if any
 * chunk was given back.
 * Called without locks (from buffer allocation).
 */
bool ps3rsx_vswap_shrink(struct ps3rsx_device *rsx, u64 bytes)
{
	struct ps3rsx_vswap *vs = rsx->vswap;
	unsigned int i, freed = 0;
	struct evict_buf eb;
	u64 got = 0;
	bool ok;
	int ret;

	if (!vs || !vs->x.map)
		return false;
	mutex_lock(&vs->shrink_lock);
	ok = evict_buf_init(&eb, GFP_KERNEL);
	while (ok && got < bytes + CHUNK) {
		struct ps3rsx_zombie *z;
		int best = -1;

		/* the chunk goes back as a zombie, cleared before any BO lands on it (vclear.c);
		 * its carrier first, so a failed allocation leaves the chunk in the pool */
		z = ps3rsx_vclear_zombie_new();
		if (!z)
			break;
		mutex_lock(&vs->lock);
		for (i = 0; i < vs->nchunks; i++)
			if (drm_mm_node_allocated(&vs->chunks[i].node) && vs->chunks[i].io &&
			    (best < 0 || vs->chunks[i].nused < vs->chunks[best].nused))
				best = i;
		mutex_unlock(&vs->lock);
		if (best < 0 || !chunk_evict(vs, best, &eb, true)) {
			kfree(z);
			break;
		}
		mutex_lock(&rsx->mm_lock);
		ret = ps3rsx_vclear_take_node(rsx, z, &vs->chunks[best].node);
		mutex_unlock(&rsx->mm_lock);
		if (ret) {
			if (ret == -ENOMEM)	/* its node holds the range: out of use until unload */
				pr_err_once("ps3vswap: a chunk couldn't be cleared: kept out of use\n");
			else
				pr_err("ps3vswap: a chunk went back to the heap without its zombie, zeroed by the CPU\n");
			kfree(z);
		} else {
			ps3rsx_vclear_add(rsx, z);
		}
		got += CHUNK;
		freed++;
	}
	mutex_unlock(&vs->shrink_lock);
	evict_buf_free(&eb);
	return freed > 0;
}

/* Keep HEADROOM slots free in a full pool: the oldest chunk's pages go to the disk */
static void vswap_demote_work(struct work_struct *work)
{
	struct ps3rsx_vswap *vs = container_of(work, struct ps3rsx_vswap, demote_work);
	struct evict_buf eb;
	unsigned int n;

	mutex_lock(&vs->shrink_lock);
	/* under heavy pressure it gives up: new pages then go to the disk for a while */
	if (!evict_buf_init(&eb, GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN))
		goto out;
	for (n = 0; n < vs->nchunks; n++) {	/* at most one pass over the pool */
		unsigned int i, before;
		int oldest = -1;

		mutex_lock(&vs->lock);
		if (vs->nalloc * SLOTS - vs->x.in_vram >= HEADROOM) {
			mutex_unlock(&vs->lock);
			break;
		}
		for (i = 0; i < vs->nchunks; i++)
			if (vs->chunks[i].io && !vs->chunks[i].evicting && vs->chunks[i].nused &&
			    (oldest < 0 || vs->chunks[i].born < vs->chunks[oldest].born))
				oldest = i;
		before = oldest >= 0 ? vs->chunks[oldest].nused : 0;
		mutex_unlock(&vs->lock);
		if (oldest < 0)
			break;
		chunk_evict(vs, oldest, &eb, false);
		mutex_lock(&vs->lock);
		i = before - vs->chunks[oldest].nused;
		vs->demoted += i;
		mutex_unlock(&vs->lock);
		if (!i)
			break;			/* no progress (no memory for the PPE bounce) */
	}
out:
	mutex_unlock(&vs->shrink_lock);
	evict_buf_free(&eb);
}

/* The SPE is gone: every chunk's pages opened by the PPE (vswap_core) and written to the disk */
static void vswap_drain_work(struct work_struct *work)
{
	struct ps3rsx_vswap *vs = container_of(work, struct ps3rsx_vswap, drain_work);
	struct evict_buf eb;
	unsigned int i;
	bool has;

	mutex_lock(&vs->shrink_lock);
	if (evict_buf_init(&eb, GFP_KERNEL)) {
		for (i = 0; i < vs->nchunks; i++) {
			mutex_lock(&vs->lock);
			has = vs->chunks[i].io && vs->chunks[i].nused;
			mutex_unlock(&vs->lock);
			if (has)
				chunk_evict(vs, i, &eb, false);
			cond_resched();
		}
	}
	evict_buf_free(&eb);
	mutex_unlock(&vs->shrink_lock);
	pr_info("ps3vswap: the drain is done: %u pages left in VRAM (kept or changed while it ran)\n",
		READ_ONCE(vs->x.in_vram));
}

/* --- sysfs -------------------------------------------------------------------------------- */

static struct ps3rsx_vswap *dev_vswap(struct device *dev)
{
	return dev_to_disk(dev)->private_data;
}

static ssize_t backing_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", dev_vswap(dev)->backing_path);
}

/* Set the backing partition (once): it sizes the device */
static ssize_t backing_store(struct device *dev, struct device_attribute *attr, const char *buf,
			     size_t len)
{
	struct ps3rsx_vswap *vs = dev_vswap(dev);
	char path[sizeof(vs->backing_path)];
	struct file *f;
	sector_t sectors;
	u32 *map;
	int ret = 0;

	if (strscpy(path, buf, sizeof(path)) < 0)
		return -ENAMETOOLONG;
	strim(path);
	f = bdev_file_open_by_path(path, BLK_OPEN_READ | BLK_OPEN_WRITE | BLK_OPEN_EXCL, vs, NULL);
	if (IS_ERR(f))
		return PTR_ERR(f);
	sectors = min_t(sector_t, bdev_nr_sectors(file_bdev(f)), (sector_t)vswap_size_mb << 11);
	sectors &= ~(sector_t)((PAGE_SIZE >> 9) - 1);
	map = kvcalloc(sectors >> (PAGE_SHIFT - 9), sizeof(*map), GFP_KERNEL);
	mutex_lock(&vs->lock);
	if (vs->backing || !map || !sectors) {
		ret = vs->backing ? -EBUSY : -ENOMEM;
		mutex_unlock(&vs->lock);
		kvfree(map);
		fput(f);
		return ret;
	}
	vs->backing = f;
	vs->x.map = map;
	vs->x.npages = sectors >> (PAGE_SHIFT - 9);
	strscpy(vs->backing_path, path, sizeof(vs->backing_path));
	mutex_unlock(&vs->lock);
	set_capacity_and_notify(vs->disk, sectors);
	pr_info("ps3vswap: %u MiB, up to %u MiB of it in VRAM (%s), the rest on %s\n",
		(u32)(sectors >> 11), vs->x.mode == VSX_SEAL ? vswap_vram_mb : 0,
		vs->x.mode == VSX_SEAL ? "sealed on an SPE" : "none: no SPE for the seals", path);
	return len;
}
static DEVICE_ATTR_RW(backing);

static ssize_t stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ps3rsx_vswap *vs = dev_vswap(dev);
	unsigned int i, chunks = 0;
	ssize_t n;

	mutex_lock(&vs->lock);
	for (i = 0; i < vs->nchunks; i++)
		chunks += drm_mm_node_allocated(&vs->chunks[i].node);
	n = sysfs_emit(buf,
		       "pages in VRAM %u on disk %u failed %u, VRAM chunks %u (MiB)\n"
		       "writes VRAM %llu disk %llu, reads VRAM %llu disk %llu, written back %llu "
		       "(demoted %llu)\n"
		       "GPU copies: reads %llu, chunk write-backs %llu, fallbacks %llu\n"
		       "seals %s: pages %llu (jobs %llu, failed %llu), opens from the bounce %llu, from VRAM %llu, "
		       "by the PPE %llu, retries %llu, resubmits %llu, check failures %llu (BAD %llu)%s, kept %llu, "
		       "merges %llu, unsealed writes %llu reads %llu, to disk without an SPE %llu\n",
		       vs->x.in_vram, vs->x.on_disk, vs->x.bad, chunks, vs->x.st.seals + vs->x.st.plain_writes,
		       vs->disk_writes, vs->x.st.opens_bounce + vs->x.st.opens_vram + vs->x.st.opens_ppe +
		       vs->x.st.plain_reads, vs->disk_reads, vs->evicted, vs->demoted, vs->gpu_reads,
		       vs->gpu_evicts, vs->gpu_fallbacks,
		       vs->x.mode == VSX_SEAL ? "on" : vs->x.mode == VSX_DRAIN ? "GONE (drain)" : "off (no SPE)",
		       vs->x.st.seals, vs->x.st.seal_jobs, vs->x.st.seal_fails, vs->x.st.opens_bounce,
		       vs->x.st.opens_vram, vs->x.st.opens_ppe, vs->x.st.retries, vs->x.st.resubmits,
		       vs->x.st.tag_fails, vs->x.st.bad_marked, vs->x.locked ? ", LOCKED OUT" : "", vs->x.st.kept,
		       vs->x.st.merges, vs->x.st.plain_writes, vs->x.st.plain_reads, vs->x.st.to_disk_nospe);
	mutex_unlock(&vs->lock);
	return n;
}
static DEVICE_ATTR_RO(stats);

#ifdef VSX_PLAIN
/*
 * Test builds: /sys/block/ps3vswap0/test_seal (root, write only), for the PS3 correctness test of the seals.
 * "corrupt P": one bit of device page P's slot flipped by a PPE store (a changed slot);
 * "save P": P's slot's ciphertext copied aside; "replay P": the saved ciphertext written into P's slot now
 * (an older seal, or another page's). The next read of P must fail. Results in the kernel log. Only slots
 * of the pool, through its own mapping.
 */
static ssize_t test_seal_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	static u8 saved[PAGE_SIZE] __aligned(8);
	static bool have_saved;
	struct ps3rsx_vswap *vs = dev_vswap(dev);
	char cmd[16];
	u32 p, e;

	if (sscanf(buf, "%15s %u", cmd, &p) != 2)
		return -EINVAL;
	mutex_lock(&vs->lock);
	e = vs->x.map && p < vs->x.npages ? vs->x.map[p] : 0;
	if (!vsx_in_vram(e)) {
		mutex_unlock(&vs->lock);
		pr_info("ps3vswap: test_seal %s %u: not in VRAM (%#x)\n", cmd, p, e);
		return -ENOENT;
	}
	if (!strcmp(cmd, "corrupt")) {
		void __iomem *a = slot_io(vs, e - 1) + 64;

		__raw_writeq(__raw_readq(a) ^ 1, a);
		mb();
	} else if (!strcmp(cmd, "save")) {
		from_vram(saved, slot_io(vs, e - 1), PAGE_SIZE);
		have_saved = true;
	} else if (!strcmp(cmd, "replay") && have_saved) {
		to_vram(slot_io(vs, e - 1), saved, PAGE_SIZE);
	} else {
		mutex_unlock(&vs->lock);
		return -EINVAL;
	}
	mutex_unlock(&vs->lock);
	pr_info("ps3vswap: test_seal %s %u: slot %u\n", cmd, p, e - 1);
	return len;
}
static DEVICE_ATTR_WO(test_seal);
#endif

static struct attribute *vswap_attrs[] = {
	&dev_attr_backing.attr,
	&dev_attr_stats.attr,
#ifdef VSX_PLAIN
	&dev_attr_test_seal.attr,
#endif
	NULL,
};
static const struct attribute_group vswap_group = { .attrs = vswap_attrs };
static const struct attribute_group *vswap_groups[] = { &vswap_group, NULL };

/* --- setup -------------------------------------------------------------------------------- */

/* Whether the pool may hold pages at all (the SPE client reserves an SPE only then) */
bool ps3rsx_vswap_pool_enabled(void)
{
	return vswap_vram_mb != 0;
}

/* the SPE may name: the bounce (ciphertext only), a slot of the pool (VRAM), kernel RAM that no GPU mapping
 * holds; the bounce's window first, so its pages are never taken as RAM */
static int vswap_page_ok(void *arg, u64 ea, unsigned int kind)
{
	struct ps3rsx_vswap *vs = arg;
	unsigned int i;

	if (kind == SPE_WIN_VRAM) {
		for (i = 0; i < vs->nchunks; i++) {
			struct vchunk *c = &vs->chunks[i];

			if (c->io && ea >= (unsigned long)c->io && ea - (unsigned long)c->io < CHUNK)
				return 1;
		}
		return 0;
	}
	return kind == SPE_WIN_RAM && virt_addr_valid((void *)(unsigned long)ea) &&
	       !ps3rsx_gart_holds(vs->rsx, ea);
}

/* the seals' state: the private pages, the mode, the SPE's bounds */
static int vswap_seal_init(struct ps3rsx_vswap *vs)
{
	struct spe_window w[3];
	unsigned int k, nw = 0;

	vs->scratch = alloc_page(GFP_KERNEL);
	if (!vs->scratch)
		return -ENOMEM;
	for (k = 0; k < WB_RESERVE; k++)
		if (!(vs->wb_reserve[k] = alloc_page(GFP_KERNEL)))
			return -ENOMEM;
	vs->x.hw = vs;
	vs->x.next_ctr = 1;
	vs->x.lockout = vswap_seal_lockout ?: 1;
	vs->x.scratch = page_address(vs->scratch);
	vs->x.scratch_ref = vs->scratch;
	vs->x.mode = VSX_OFF;
	if (vs->bounce_ok)
		w[nw++] = (struct spe_window){ (unsigned long)vs->bounce, CHUNK, SPE_WIN_BOUNCE };
	w[nw++] = (struct spe_window){ H_KERN_IO_START, H_KERN_IO_END - H_KERN_IO_START, SPE_WIN_VRAM };
	w[nw++] = (struct spe_window){ PAGE_OFFSET, 1ull << 40, SPE_WIN_RAM };
	if (ps3rsx_spe_ready(vs->rsx) && !ps3rsx_spe_set_bounds(vs->rsx, w, nw, NULL, 0, vswap_page_ok, vs))
		vs->x.mode = VSX_SEAL;
	else
		drm_warn(&vs->rsx->drm, "ps3vswap: no SPE for the seals: the swap keeps nothing in VRAM\n");
	return 0;
}

static void vswap_seal_fini(struct ps3rsx_vswap *vs)
{
	unsigned int k;

	if (vs->scratch)
		__free_page(vs->scratch);	/* (a pinned one keeps its extra reference) */
	for (k = 0; k < WB_RESERVE; k++)
		if (vs->wb_reserve[k])
			__free_page(vs->wb_reserve[k]);
}

int ps3rsx_vswap_init(struct ps3rsx_device *rsx)
{
	struct queue_limits lim = {
		.logical_block_size = PAGE_SIZE,
		.physical_block_size = PAGE_SIZE,
		.io_min = PAGE_SIZE,
		.max_hw_sectors = MAX_RQ_PAGES << (PAGE_SHIFT - 9),
		.max_hw_discard_sectors = UINT_MAX,
		.max_write_zeroes_sectors = UINT_MAX,
		.discard_granularity = PAGE_SIZE,
		.dma_alignment = PAGE_SIZE - 1,	/* whole, aligned pages only, O_DIRECT too */
	};
	struct ps3rsx_vswap *vs;
	int ret;

	if (!vswap_vram_mb)
		return 0;
	vs = kzalloc(sizeof(*vs), GFP_KERNEL);
	if (!vs)
		return -ENOMEM;
	vs->rsx = rsx;
	mutex_init(&vs->lock);
	mutex_init(&vs->shrink_lock);
	spin_lock_init(&vs->pending_lock);
	bio_list_init(&vs->deferred);
	INIT_LIST_HEAD(&vs->pending);
	INIT_WORK(&vs->io_work, vswap_io_work);
	INIT_WORK(&vs->demote_work, vswap_demote_work);
	INIT_WORK(&vs->drain_work, vswap_drain_work);
	vs->nchunks = min_t(unsigned int, vswap_vram_mb, (VSX_ON_DISK - 2) / SLOTS);	/* slot + 1 stays below ON_DISK */
	vs->chunks = kvcalloc(vs->nchunks, sizeof(*vs->chunks), GFP_KERNEL);
	ret = -ENOMEM;
	if (!vs->chunks || bioset_init(&vs->bios, BIO_RESERVE, 0, BIOSET_NEED_BVECS))
		goto err;
	/* both in the reclaim path (swap-out): they need their rescuer threads */
	vs->io_wq = alloc_ordered_workqueue("ps3vswap", WQ_MEM_RECLAIM | WQ_HIGHPRI);
	vs->demote_wq = alloc_ordered_workqueue("ps3vswap-demote", WQ_MEM_RECLAIM);
	if (!vs->io_wq || !vs->demote_wq)
		goto err;
	/* the GPU-copy bounce: 1 MiB of GART for good; without it, the SPE gets every slot from VRAM */
	if (!ps3rsx_gart_pin(rsx, &vs->bounce_node, &vs->bounce_io)) {
		vs->bounce = ps3rsx_gart_ptr(rsx, vs->bounce_io);
		vs->bounce_ok = true;
	} else {
		drm_warn(&rsx->drm, "ps3vswap: no GART for the copy bounce: no GPU copies\n");
	}
	ret = vswap_seal_init(vs);
	if (ret)
		goto err;

	ret = register_blkdev(0, "ps3vswap");
	if (ret < 0)
		goto err;
	vswap_major = ret;
	vs->tags.ops = &vswap_mq_ops;
	vs->tags.nr_hw_queues = 1;
	vs->tags.queue_depth = 64;	/* deep: a full queue made swap-outs in reclaim wait for tags, nested deep in the stack */
	vs->tags.numa_node = NUMA_NO_NODE;
	vs->tags.cmd_size = sizeof(struct vswap_cmd);
	ret = blk_mq_alloc_tag_set(&vs->tags);
	if (ret)
		goto err_blkdev;
	vs->disk = blk_mq_alloc_disk(&vs->tags, &lim, vs);
	if (IS_ERR(vs->disk)) {
		ret = PTR_ERR(vs->disk);
		goto err_tags;
	}
	vs->disk->major = vswap_major;
	vs->disk->first_minor = 0;
	vs->disk->minors = 1;
	vs->disk->fops = &vswap_fops;
	vs->disk->private_data = vs;
	strscpy(vs->disk->disk_name, "ps3vswap0", DISK_NAME_LEN);
	set_capacity(vs->disk, 0);		/* sized when the backing partition is set */
	ret = device_add_disk(rsx->drm.dev, vs->disk, vswap_groups);
	if (ret)
		goto err_disk;
	rsx->vswap = vs;
	return 0;

err_disk:
	put_disk(vs->disk);
err_tags:
	blk_mq_free_tag_set(&vs->tags);
err_blkdev:
	unregister_blkdev(vswap_major, "ps3vswap");
err:
	if (vs->io_wq)
		destroy_workqueue(vs->io_wq);
	if (vs->demote_wq)
		destroy_workqueue(vs->demote_wq);
	if (vs->bounce_ok)
		ps3rsx_gart_unpin(rsx, &vs->bounce_node);
	vswap_seal_fini(vs);
	bioset_exit(&vs->bios);
	kvfree(vs->chunks);
	kfree(vs);
	return ret;
}

void ps3rsx_vswap_fini(struct ps3rsx_device *rsx)
{
	struct ps3rsx_vswap *vs = rsx->vswap;
	unsigned int i;

	if (!vs)
		return;
	del_gendisk(vs->disk);			/* the module can't unload while swap is on it */
	put_disk(vs->disk);
	cancel_work_sync(&vs->demote_work);
	cancel_work_sync(&vs->drain_work);
	destroy_workqueue(vs->demote_wq);
	destroy_workqueue(vs->io_wq);
	if (vs->bounce_ok) {
		if (vs->bounce_late)		/* a timed-out copy may still write it */
			ps3rsx_kcopy_wait(rsx, vs->bounce_seq, 2000000);
		ps3rsx_gart_unpin(rsx, &vs->bounce_node);
	}
	blk_mq_free_tag_set(&vs->tags);
	unregister_blkdev(vswap_major, "ps3vswap");
	mutex_lock(&rsx->mm_lock);
	for (i = 0; i < vs->nchunks; i++) {
		struct vchunk *c = &vs->chunks[i];

		if (!drm_mm_node_allocated(&c->node))
			continue;
		if (c->io)		/* none: a chunk kept out of use (ps3rsx_vclear_take_node) */
			iounmap(c->io);
		kfree(c->meta);
		drm_mm_remove_node(&c->node);
	}
	mutex_unlock(&rsx->mm_lock);
	if (vs->backing)
		fput(vs->backing);
	vswap_seal_fini(vs);
	kvfree(vs->x.map);
	bioset_exit(&vs->bios);
	kvfree(vs->chunks);
	kfree(vs);
	rsx->vswap = NULL;
}

/* debugfs ps3rsx_mm line */
void ps3rsx_vswap_show(struct seq_file *m, struct ps3rsx_device *rsx)
{
	struct ps3rsx_vswap *vs = rsx->vswap;

	if (!vs || !vs->x.map)
		return;
	seq_printf(m, "swap: %u pages in VRAM, %u on disk, %u failed, %llu written back, seals %s\n",
		   vs->x.in_vram, vs->x.on_disk, vs->x.bad, vs->evicted,
		   vs->x.mode == VSX_SEAL ? "on" : vs->x.mode == VSX_DRAIN ? "GONE" : "off");
}
