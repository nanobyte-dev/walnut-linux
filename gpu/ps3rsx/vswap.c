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
 *    pages of the emptiest chunks back to the disk and hands the chunks over;
 *  - the VRAM holds the recently swapped: when the pool is (nearly) full, a background job
 *    moves the pages of its oldest chunk to the disk (pages that sat in swap longest without
 *    being read back are the cold ones; reads free their slots through discards).
 * Requests are served by our own work queue, on a fresh stack: blk-mq runs queue_rq inline from
 * a plug flush in direct reclaim, which was already deep in the stack (an IRQ found < 4 KiB left).
 * Writes are PPE stores (8-byte, ~124 MB/s). Reads (swap-ins) and write-backs are copied by the
 * GPU (M2MF, kcopy.c) into a pinned 1 MiB GART bounce, ~600 MB/s, when the GPU can be had
 * without waiting; otherwise the PPE reads VRAM itself (10 MB/s, docs/RSX.md). The GPU only
 * ever reads the VRAM slots, so a failed, late or wedged copy loses nothing: the PPE reads the
 * same slot. Both are far faster than the USB disk, which serves one command at a time.
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

#define CHUNK		SZ_1M
#define SLOTS		(CHUNK / PAGE_SIZE)	/* pages per chunk */
#define ON_DISK		0x80000000u		/* map entry: the page is on the backing disk */
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

struct vchunk {
	struct drm_mm_node node;	/* allocated while the chunk is in the pool */
	void __iomem *io;
	unsigned int nused;
	bool evicting;			/* being written back: no new pages into it */
	u64 born;			/* when it was (re)filled: the demotion takes the oldest */
	DECLARE_BITMAP(used, SLOTS);
	DECLARE_BITMAP(dirty, SLOTS);	/* rewritten or freed since the write-back copied it */
	u32 page[SLOTS];		/* device page in each used slot */
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
	u32 *map;			/* per device page: 0 empty, slot + 1 in VRAM, or ON_DISK */
	u32 npages;
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
	u64 demoted;
	/* GPU copies (kcopy.c) through a pinned GART bounce */
	struct drm_mm_node bounce_node;
	u32 bounce_io;			/* its GART IO address */
	void *bounce;			/* its kernel address (cacheable, IO-coherent) */
	bool bounce_ok;			/* pinned: GPU copies possible */
	bool bounce_evict;		/* a write-back owns it (until its disk writes complete) */
	bool bounce_late;		/* a timed-out copy may still write it, up to bounce_seq */
	u32 bounce_seq;
	struct vread {			/* VRAM pieces of the current read request */
		struct page *page;
		u32 slot;
		u16 off, in_page, len;
	} rd[2 * MAX_RQ_PAGES];
	unsigned int nrd;
	struct kcopy_run runs[2 * MAX_RQ_PAGES];
	u64 gpu_reads, gpu_evicts, gpu_fallbacks;
	/* statistics (pages) */
	u64 vram_writes, disk_writes, vram_reads, disk_reads, evicted;
	u32 in_vram, on_disk;
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
struct vswap_cmd {
	atomic_t pending;
	blk_status_t status;
	struct list_head node;		/* on vs->pending until io_work serves it */
};

static void cmd_put(struct request *rq)
{
	struct vswap_cmd *cmd = blk_mq_rq_to_pdu(rq);

	if (atomic_dec_and_test(&cmd->pending))
		blk_mq_end_request(rq, cmd->status);
}

static void disk_end_io(struct bio *bio)
{
	struct request *rq = bio->bi_private;
	struct vswap_cmd *cmd = blk_mq_rq_to_pdu(rq);

	if (bio->bi_status)
		cmd->status = bio->bi_status;
	bio_put(bio);
	cmd_put(rq);
}

/* A bio for one piece of rq at the same place on the disk, submitted once vs->lock is dropped */
static struct bio *disk_bio(struct ps3rsx_vswap *vs, struct request *rq, blk_opf_t op, u32 p,
			    unsigned int in_page, struct page *page, unsigned int len,
			    unsigned int off)
{
	struct vswap_cmd *cmd = blk_mq_rq_to_pdu(rq);
	struct bio *bio = bio_alloc_bioset(file_bdev(vs->backing), 1, op, GFP_NOIO, &vs->bios);

	bio->bi_iter.bi_sector = ((sector_t)p << (PAGE_SHIFT - 9)) + (in_page >> 9);
	__bio_add_page(bio, page, len, off);
	bio->bi_private = rq;
	bio->bi_end_io = disk_end_io;
	atomic_inc(&cmd->pending);
	return bio;
}

/* --- the VRAM pool ------------------------------------------------------------------------ */

/* A free slot, growing the pool if it may; -1 if none. vs->lock held. */
static int slot_alloc(struct ps3rsx_vswap *vs)
{
	struct ps3rsx_device *rsx = vs->rsx;
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
	if (i == vs->nchunks || !mutex_trylock(&rsx->mm_lock))
		return -1;
	c = &vs->chunks[i];
	memset(&c->node, 0, sizeof(c->node));
	ret = drm_mm_insert_node_generic(&rsx->vram_cpu, &c->node, CHUNK, CHUNK, 0,
					 DRM_MM_INSERT_HIGH);
	mutex_unlock(&rsx->mm_lock);
	if (ret)
		return -1;
	c->io = ioremap(rsx->info.ddr_lpar + c->node.start, CHUNK);
	if (!c->io) {
		mutex_lock(&rsx->mm_lock);	/* rare; nothing under mm_lock waits for us here */
		drm_mm_remove_node(&c->node);
		mutex_unlock(&rsx->mm_lock);
		return -1;
	}
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
	c->nused--;
	vs->in_vram--;
}

/* --- requests ----------------------------------------------------------------------------- */

/* A full (or no longer growing) pool: let the demotion make room for new pages. vs->lock held. */
static void maybe_demote(struct ps3rsx_vswap *vs, bool alloc_failed)
{
	if (alloc_failed || (vs->nalloc == vs->nchunks && vs->nalloc * SLOTS - vs->in_vram < HEADROOM))
		queue_work(vs->demote_wq, &vs->demote_work);
}

/* One piece of a write, within device page p; a disk piece is added to *bios. vs->lock held. */
static void do_write(struct ps3rsx_vswap *vs, struct request *rq, struct bio_list *bios, u32 p,
		     struct page *page, unsigned int len, unsigned int off, unsigned int in_page)
{
	u32 e = vs->map[p];
	void *buf;
	int slot;

	if (e && e != ON_DISK) {			/* already in VRAM: overwrite */
		struct vchunk *c = &vs->chunks[(e - 1) / SLOTS];

		buf = kmap_local_page(page);
		to_vram(slot_io(vs, e - 1) + in_page, buf + off, len);
		kunmap_local(buf);
		set_bit((e - 1) % SLOTS, c->dirty);	/* a write-back in flight is stale now */
		vs->vram_writes++;
		return;
	}
	slot = len == PAGE_SIZE ? slot_alloc(vs) : -1;
	maybe_demote(vs, len == PAGE_SIZE && slot < 0);
	if (slot >= 0) {
		buf = kmap_local_page(page);
		to_vram(slot_io(vs, slot), buf + off, len);
		kunmap_local(buf);
		vs->chunks[slot / SLOTS].page[slot % SLOTS] = p;
		if (e == ON_DISK)
			vs->on_disk--;
		vs->map[p] = slot + 1;
		vs->in_vram++;
		vs->vram_writes++;
		return;
	}
	bio_list_add(bios, disk_bio(vs, rq, REQ_OP_WRITE, p, in_page, page, len, off));
	if (e != ON_DISK)
		vs->on_disk++;
	vs->map[p] = ON_DISK;
	vs->disk_writes++;
}

static void do_read(struct ps3rsx_vswap *vs, struct request *rq, struct bio_list *bios, u32 p,
		    struct page *page, unsigned int len, unsigned int off, unsigned int in_page)
{
	u32 e = vs->map[p];
	void *buf;

	if (e == ON_DISK) {
		bio_list_add(bios, disk_bio(vs, rq, REQ_OP_READ, p, in_page, page, len, off));
		vs->disk_reads++;
		return;
	}
	if (e && vs->nrd < ARRAY_SIZE(vs->rd)) {	/* copied after the loop: reads_finish() */
		vs->rd[vs->nrd++] = (struct vread){ page, e - 1, off, in_page, len };
		vs->vram_reads++;
		return;
	}
	buf = kmap_local_page(page);
	if (e) {
		from_vram(buf + off, slot_io(vs, e - 1) + in_page, len);
		vs->vram_reads++;
	} else {
		memset(buf + off, 0, len);
	}
	kunmap_local(buf);
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

/*
 * Copy the request's VRAM pieces (vs->rd) into their pages: through the GPU into the bounce,
 * then a cacheable copy, or with PPE reads of VRAM. vs->lock held.
 */
static void reads_finish(struct ps3rsx_vswap *vs)
{
	struct ps3rsx_device *rsx = vs->rsx;
	unsigned int i, n = vs->nrd, nr = 0;
	int err;
	u32 seq;

	vs->nrd = 0;
	if (vswap_gpu && bounce_free(vs) && (n >= vswap_gpu_min || ps3rsx_exec_idle(rsx)) &&
	    !ps3rsx_kcopy_begin(rsx)) {
		for (i = 0; i < n; i++) {	/* piece i -> bounce page i; merge neighbours */
			struct vchunk *c = &vs->chunks[vs->rd[i].slot / SLOTS];
			u32 vram = c->node.start + (vs->rd[i].slot % SLOTS) * PAGE_SIZE;
			u32 gart = vs->bounce_io + i * PAGE_SIZE;
			struct kcopy_run *r = nr ? &vs->runs[nr - 1] : NULL;

			if (r && r->vram + r->npages * PAGE_SIZE == vram &&
			    r->gart + r->npages * PAGE_SIZE == gart)
				r->npages++;
			else
				vs->runs[nr++] = (struct kcopy_run){ vram, gart, 1 };
		}
		err = ps3rsx_kcopy_to_gart(rsx, vs->runs, nr, &seq);
		ps3rsx_kcopy_end(rsx);
		if (!err)
			err = ps3rsx_kcopy_wait(rsx, seq, 50000);
		if (!err) {
			for (i = 0; i < n; i++)
				memcpy_to_page(vs->rd[i].page, vs->rd[i].off,
					       vs->bounce + i * PAGE_SIZE + vs->rd[i].in_page,
					       vs->rd[i].len);
			vs->gpu_reads += n;
			return;
		}
		gpu_failed(vs, err, seq);
	}
	for (i = 0; i < n; i++) {
		void *buf = kmap_local_page(vs->rd[i].page);

		from_vram(buf + vs->rd[i].off, slot_io(vs, vs->rd[i].slot) + vs->rd[i].in_page,
			  vs->rd[i].len);
		kunmap_local(buf);
	}
}

static void do_discard(struct ps3rsx_vswap *vs, sector_t sector, unsigned int bytes)
{
	u32 p = DIV_ROUND_UP(sector, PAGE_SIZE >> 9);
	u32 end = (sector + (bytes >> 9)) >> (PAGE_SHIFT - 9);

	for (; p < end && p < vs->npages; p++) {
		u32 e = vs->map[p];

		if (e == ON_DISK) {
			vs->on_disk--;
		} else if (e) {
			set_bit((e - 1) % SLOTS, vs->chunks[(e - 1) / SLOTS].dirty);
			slot_free(vs, e - 1);
		}
		vs->map[p] = 0;
	}
}

/* Serve one request (io_work, on its own stack) */
static void vswap_do_rq(struct ps3rsx_vswap *vs, struct request *rq)
{
	struct vswap_cmd *cmd = blk_mq_rq_to_pdu(rq);
	u64 pos = (u64)blk_rq_pos(rq) << 9;
	struct bio_list bios;
	struct req_iterator iter;
	unsigned int noio;
	struct bio_vec bv;
	struct bio *bio;

	atomic_set(&cmd->pending, 1);
	cmd->status = BLK_STS_OK;
	bio_list_init(&bios);
	vs->nrd = 0;
	noio = memalloc_noio_save();
	mutex_lock(&vs->lock);
	if (!vs->map) {
		cmd->status = BLK_STS_IOERR;
		goto out;
	}
	switch (req_op(rq)) {
	case REQ_OP_FLUSH:
		break;
	case REQ_OP_DISCARD:
	case REQ_OP_WRITE_ZEROES:
		do_discard(vs, blk_rq_pos(rq), blk_rq_bytes(rq));
		break;
	case REQ_OP_READ:
	case REQ_OP_WRITE:
		rq_for_each_segment(bv, rq, iter) {
			unsigned int done = 0;

			/* in pieces that don't cross a device page */
			while (done < bv.bv_len) {
				u32 p = pos >> PAGE_SHIFT;
				unsigned int in_page = pos & (PAGE_SIZE - 1);
				unsigned int len = min(bv.bv_len - done, (unsigned int)PAGE_SIZE - in_page);

				if (p >= vs->npages) {
					cmd->status = BLK_STS_IOERR;
					goto out;
				}
				if (op_is_write(req_op(rq)))
					do_write(vs, rq, &bios, p, bv.bv_page, len, bv.bv_offset + done,
						 in_page);
				else
					do_read(vs, rq, &bios, p, bv.bv_page, len, bv.bv_offset + done,
						in_page);
				done += len;
				pos += len;
			}
		}
		break;
	default:
		cmd->status = BLK_STS_NOTSUPP;
	}
out:
	if (vs->nrd)
		reads_finish(vs);
	mutex_unlock(&vs->lock);
	while ((bio = bio_list_pop(&bios)))
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
 * chunk_evict()'s buffers: the page numbers, the bounce pages it hands to the disk (the GART
 * bounce's after a GPU copy), and its own pages for the PPE fallback, allocated only when needed.
 */
struct evict_buf {
	u32 *pages;
	struct page **bounce;
	struct page **own;
	gfp_t gfp;
};

static bool evict_buf_init(struct evict_buf *eb, gfp_t gfp)
{
	eb->gfp = gfp;
	eb->own = NULL;
	eb->pages = kcalloc(SLOTS, sizeof(*eb->pages), gfp);
	eb->bounce = kcalloc(SLOTS, sizeof(*eb->bounce), gfp);
	return eb->pages && eb->bounce;
}

static bool evict_bounce_alloc(struct evict_buf *eb)
{
	unsigned int s;

	if (eb->own)
		return true;
	eb->own = kcalloc(SLOTS, sizeof(*eb->own), eb->gfp);
	if (!eb->own)
		return false;
	for (s = 0; s < SLOTS; s++)
		if (!(eb->own[s] = alloc_page(eb->gfp)))
			return false;
	return true;
}

static void evict_buf_free(struct evict_buf *eb)
{
	unsigned int s;

	for (s = 0; eb->own && s < SLOTS; s++)
		if (eb->own[s])
			__free_page(eb->own[s]);
	kfree(eb->own);
	kfree(eb->bounce);
	kfree(eb->pages);
}

/*
 * Write chunk i's pages to the disk. They are copied out one page at a time under vs->lock
 * (reading VRAM is slow: holding the lock for the whole chunk stalled swap for 100 ms), written
 * without it, and switched to the disk only if nothing rewrote or discarded them meanwhile
 * (c->dirty). release: take the emptied chunk out of the pool (its node then goes back to the
 * heap, by the caller under mm_lock); else it stays, empty, as the pool's newest chunk.
 * Returns true if the chunk was emptied. Under vs->shrink_lock.
 */
static bool chunk_evict(struct ps3rsx_vswap *vs, unsigned int i, struct evict_buf *eb,
			bool release)
{
	struct ps3rsx_device *rsx = vs->rsx;
	struct vchunk *c = &vs->chunks[i];
	struct page **bounce = eb->bounce;
	u32 *pages = eb->pages;
	unsigned int s, noio;
	struct wb_batch b;
	bool empty, gpu = false;
	u32 seq;

	mutex_lock(&vs->lock);
	c->evicting = true;			/* no new pages into it */
	bitmap_zero(c->dirty, SLOTS);
	/* the whole chunk in one GPU copy into the bounce; its pages then go to the disk as is */
	if (vswap_gpu && bounce_free(vs) && !ps3rsx_kcopy_begin(rsx)) {
		struct kcopy_run r = { c->node.start, vs->bounce_io, SLOTS };
		int err = ps3rsx_kcopy_to_gart(rsx, &r, 1, &seq);

		ps3rsx_kcopy_end(rsx);
		if (!err)
			err = ps3rsx_kcopy_wait(rsx, seq, 100000);
		if (!err) {
			gpu = true;
			vs->bounce_evict = true;
			for (s = 0; s < SLOTS; s++)
				pages[s] = test_bit(s, c->used) ? c->page[s] : U32_MAX;
			vs->gpu_evicts++;
		} else {
			gpu_failed(vs, err, seq);
		}
	}
	mutex_unlock(&vs->lock);
	if (gpu) {
		for (s = 0; s < SLOTS; s++)
			bounce[s] = virt_to_page(vs->bounce + s * PAGE_SIZE);
	} else {
		if (!evict_bounce_alloc(eb)) {
			mutex_lock(&vs->lock);
			c->evicting = false;
			mutex_unlock(&vs->lock);
			return false;
		}
		bounce = eb->own;
		for (s = 0; s < SLOTS; s++) {
			pages[s] = U32_MAX;
			mutex_lock(&vs->lock);
			if (test_bit(s, c->used)) {
				from_vram(page_address(bounce[s]), c->io + s * PAGE_SIZE, PAGE_SIZE);
				pages[s] = c->page[s];
			}
			mutex_unlock(&vs->lock);
			cond_resched();
		}
	}

	atomic_set(&b.pending, 1);
	init_completion(&b.done);
	b.status = BLK_STS_OK;
	noio = memalloc_noio_save();
	for (s = 0; s < SLOTS; s++) {
		struct bio *bio;

		if (pages[s] == U32_MAX)
			continue;
		bio = bio_alloc(file_bdev(vs->backing), 1, REQ_OP_WRITE, GFP_NOIO);
		bio->bi_iter.bi_sector = (sector_t)pages[s] << (PAGE_SHIFT - 9);
		__bio_add_page(bio, bounce[s], PAGE_SIZE, 0);
		bio->bi_private = &b;
		bio->bi_end_io = wb_end_io;
		atomic_inc(&b.pending);
		submit_bio(bio);
	}
	memalloc_noio_restore(noio);
	if (!atomic_dec_and_test(&b.pending))
		wait_for_completion_io(&b.done);

	mutex_lock(&vs->lock);
	if (b.status == BLK_STS_OK) {
		for_each_set_bit(s, c->used, SLOTS) {
			u32 p = c->page[s];

			if (test_bit(s, c->dirty) || p != pages[s] || vs->map[p] != i * SLOTS + s + 1)
				continue;		/* changed while being written: stays */
			vs->map[p] = ON_DISK;
			vs->on_disk++;
			slot_free(vs, i * SLOTS + s);
			vs->evicted++;
		}
	} else {
		pr_err_ratelimited("ps3vswap: write-back failed (%d): chunk kept\n", b.status);
	}
	if (gpu)
		vs->bounce_evict = false;
	empty = !c->nused;
	if (empty && release) {
		iounmap(c->io);
		c->io = NULL;
		vs->nalloc--;
	} else if (empty) {
		c->born = ++vs->seq;
	}
	c->evicting = false;
	mutex_unlock(&vs->lock);
	return empty;
}

/*
 * Free at least bytes of the pool's VRAM for a buffer object: empty chunks first, then the
 * emptiest, their pages written back to the disk. Returns true if any chunk was given back.
 * Called without locks (from buffer allocation).
 */
bool ps3rsx_vswap_shrink(struct ps3rsx_device *rsx, u64 bytes)
{
	struct ps3rsx_vswap *vs = rsx->vswap;
	unsigned int i, freed = 0;
	struct evict_buf eb;
	u64 got = 0;
	bool ok;

	if (!vs || !vs->map)
		return false;
	mutex_lock(&vs->shrink_lock);
	ok = evict_buf_init(&eb, GFP_KERNEL);
	while (ok && got < bytes + CHUNK) {
		int best = -1;

		mutex_lock(&vs->lock);
		for (i = 0; i < vs->nchunks; i++)
			if (drm_mm_node_allocated(&vs->chunks[i].node) && vs->chunks[i].io &&
			    (best < 0 || vs->chunks[i].nused < vs->chunks[best].nused))
				best = i;
		mutex_unlock(&vs->lock);
		if (best < 0 || !chunk_evict(vs, best, &eb, true))
			break;
		mutex_lock(&rsx->mm_lock);
		drm_mm_remove_node(&vs->chunks[best].node);
		mutex_unlock(&rsx->mm_lock);
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
		if (vs->nalloc * SLOTS - vs->in_vram >= HEADROOM) {
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
	vs->map = map;
	vs->npages = sectors >> (PAGE_SHIFT - 9);
	strscpy(vs->backing_path, path, sizeof(vs->backing_path));
	mutex_unlock(&vs->lock);
	set_capacity_and_notify(vs->disk, sectors);
	pr_info("ps3vswap: %u MiB, up to %u MiB of it in VRAM, the rest on %s\n",
		(u32)(sectors >> 11), vswap_vram_mb, path);
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
		       "pages in VRAM %u on disk %u, VRAM chunks %u (MiB)\n"
		       "writes VRAM %llu disk %llu, reads VRAM %llu disk %llu, written back %llu "
		       "(demoted %llu)\n"
		       "GPU copies: reads %llu, chunk write-backs %llu, fallbacks to the PPE %llu\n",
		       vs->in_vram, vs->on_disk, chunks, vs->vram_writes, vs->disk_writes,
		       vs->vram_reads, vs->disk_reads, vs->evicted, vs->demoted, vs->gpu_reads,
		       vs->gpu_evicts, vs->gpu_fallbacks);
	mutex_unlock(&vs->lock);
	return n;
}
static DEVICE_ATTR_RO(stats);

static struct attribute *vswap_attrs[] = {
	&dev_attr_backing.attr,
	&dev_attr_stats.attr,
	NULL,
};
static const struct attribute_group vswap_group = { .attrs = vswap_attrs };
static const struct attribute_group *vswap_groups[] = { &vswap_group, NULL };

/* --- setup -------------------------------------------------------------------------------- */

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
	INIT_LIST_HEAD(&vs->pending);
	INIT_WORK(&vs->io_work, vswap_io_work);
	INIT_WORK(&vs->demote_work, vswap_demote_work);
	vs->nchunks = vswap_vram_mb;
	vs->chunks = kvcalloc(vs->nchunks, sizeof(*vs->chunks), GFP_KERNEL);
	ret = -ENOMEM;
	if (!vs->chunks || bioset_init(&vs->bios, BIO_RESERVE, 0, BIOSET_NEED_BVECS))
		goto err;
	/* both in the reclaim path (swap-out): they need their rescuer threads */
	vs->io_wq = alloc_ordered_workqueue("ps3vswap", WQ_MEM_RECLAIM | WQ_HIGHPRI);
	vs->demote_wq = alloc_ordered_workqueue("ps3vswap-demote", WQ_MEM_RECLAIM);
	if (!vs->io_wq || !vs->demote_wq)
		goto err;
	/* the GPU-copy bounce: 1 MiB of GART for good; without it, the PPE does all copies */
	if (!ps3rsx_gart_pin(rsx, &vs->bounce_node, &vs->bounce_io)) {
		vs->bounce = ps3rsx_gart_ptr(rsx, vs->bounce_io);
		vs->bounce_ok = true;
	} else {
		drm_warn(&rsx->drm, "ps3vswap: no GART for the copy bounce: PPE copies only\n");
	}

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
		iounmap(c->io);
		drm_mm_remove_node(&c->node);
	}
	mutex_unlock(&rsx->mm_lock);
	if (vs->backing)
		fput(vs->backing);
	kvfree(vs->map);
	bioset_exit(&vs->bios);
	kvfree(vs->chunks);
	kfree(vs);
	rsx->vswap = NULL;
}

/* debugfs ps3rsx_mm line */
void ps3rsx_vswap_show(struct seq_file *m, struct ps3rsx_device *rsx)
{
	struct ps3rsx_vswap *vs = rsx->vswap;

	if (!vs || !vs->map)
		return;
	seq_printf(m, "swap: %u pages in VRAM, %u on disk, %llu written back\n",
		   vs->in_vram, vs->on_disk, vs->evicted);
}
