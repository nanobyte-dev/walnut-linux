// SPDX-License-Identifier: GPL-2.0
/*
 * The GART: main memory the GPU reaches through our context's IO window, in 1 MiB IO pages.
 *
 * Two sources (kernel patch local/0004, local/0007):
 *  - ps3rsx_cma=<size>: a CMA area. IO pages are backed on demand, one 1 MiB chunk each, when a
 *    buffer object first needs them, and given back once they've been unused for a while and the
 *    GPU has passed every fence that could still touch them. In between, the area holds ordinary
 *    movable pages, so the GART costs RAM only while the GPU uses it.
 *  - ps3rsx_gart=<size>: a fixed boot reservation, all backed for good (the earlier scheme).
 *
 * The first RING_RESERVE bytes (reset program and ring, exec.c) are one contiguous allocation,
 * backed for good: the ring is written as linear memory. IO pages without a chunk are mapped to
 * a scratch chunk, so a stray GPU fetch never faults (a fault wedges the GPU until a reboot).
 * Chunks aren't contiguous in kernel memory: CPU access goes through ps3rsx_gart_ptr() one chunk
 * at a time.
 */
#include <linux/mm.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <asm/cell-regs.h>
#include <asm/ps3.h>

#include "ps3rsx.h"

#define CHUNK		SZ_1M
#define IOPTE_RW	(CBE_IOPTE_PP_W | CBE_IOPTE_PP_R | CBE_IOPTE_M)
#define RECLAIM_IDLE	(2 * HZ)	/* an empty chunk stays this long before it goes back */

static int map_chunk(struct ps3rsx_device *rsx, unsigned int i, struct page *page)
{
	int ret = ps3fb_gpu_iomap((u64)i * CHUNK, ps3_mm_phys_to_lpar(page_to_phys(page)), CHUNK,
				  IOPTE_RW);

	if (ret)
		drm_err(&rsx->drm, "GART iomap at IO %#llx failed: %d\n", (u64)i * CHUNK, ret);
	return ret;
}

/* Back chunk i with fresh CMA memory (cleared: buffers start zeroed); mm_lock held */
static int chunk_back(struct ps3rsx_device *rsx, unsigned int i)
{
	struct ps3rsx_gart_chunk *c = &rsx->gart_chunks[i];
	struct page *page = ps3rsx_cma_alloc(1);
	int ret;

	if (!page)
		return -ENOMEM;
	memset(page_address(page), 0, CHUNK);
	ret = map_chunk(rsx, i, page);
	if (ret) {
		ps3rsx_cma_free(page, 1);
		return ret;
	}
	c->page = page;
	rsx->gart_backed += CHUNK;
	return 0;
}

/* Give chunk i back: scratch over its IO page first, so the GPU never sees freed memory */
static void chunk_release(struct ps3rsx_device *rsx, unsigned int i)
{
	struct ps3rsx_gart_chunk *c = &rsx->gart_chunks[i];

	if (map_chunk(rsx, i, rsx->gart_scratch))
		return;		/* still mapped: keep the memory */
	ps3rsx_cma_free(c->page, 1);
	c->page = NULL;
	rsx->gart_backed -= CHUNK;
}

static void reclaim_work(struct work_struct *work)
{
	struct ps3rsx_device *rsx = container_of(work, struct ps3rsx_device, gart_reclaim.work);
	bool pending = false;
	unsigned int i;

	mutex_lock(&rsx->mm_lock);
	for (i = 0; i < rsx->gart_nchunks; i++) {
		struct ps3rsx_gart_chunk *c = &rsx->gart_chunks[i];

		if (c->fixed || !c->page || c->users)
			continue;
		if (time_before(jiffies, c->idle_since + RECLAIM_IDLE) ||
		    !ps3rsx_seq_done(rsx, c->free_seq)) {
			pending = true;
			continue;
		}
		chunk_release(rsx, i);
	}
	mutex_unlock(&rsx->mm_lock);
	if (pending)
		schedule_delayed_work(&rsx->gart_reclaim, RECLAIM_IDLE);
}

/* A buffer object takes [io, io + size): back the chunks it touches; mm_lock held */
int ps3rsx_gart_get(struct ps3rsx_device *rsx, u64 io, u64 size)
{
	unsigned int first = io / CHUNK, last = (io + size - 1) / CHUNK, i;
	int ret = 0;

	for (i = first; i <= last; i++) {
		struct ps3rsx_gart_chunk *c = &rsx->gart_chunks[i];

		if (!c->page) {
			ret = chunk_back(rsx, i);
			if (ret)
				break;
		}
		c->users++;
	}
	if (ret) {
		/* undo; the chunks backed so far go back through the reclaim */
		while (i-- > first)
			ps3rsx_gart_put(rsx, (u64)i * CHUNK, 1, rsx->seqno);
		pr_warn_ratelimited("ps3rsx: no memory to back the GART (%llu of %llu MiB in use)\n",
				    rsx->gart_backed >> 20, rsx->gart_size >> 20);
	}
	return ret;
}

/* A buffer object frees [io, io + size); seq: the last fence that may have used it */
void ps3rsx_gart_put(struct ps3rsx_device *rsx, u64 io, u64 size, u32 seq)
{
	unsigned int first = io / CHUNK, last = (io + size - 1) / CHUNK, i;

	for (i = first; i <= last; i++) {
		struct ps3rsx_gart_chunk *c = &rsx->gart_chunks[i];

		if (WARN_ON(!c->users))
			continue;
		if (--c->users || c->fixed)
			continue;
		c->free_seq = seq;
		c->idle_since = jiffies;
		schedule_delayed_work(&rsx->gart_reclaim, RECLAIM_IDLE);
	}
}

/*
 * A GART area for the kernel's own use (the VRAM swap's copy bounce): one 1 MiB chunk, backed
 * and kept for good (fixed: the idle reclaim never takes it). Returns its IO address.
 */
int ps3rsx_gart_pin(struct ps3rsx_device *rsx, struct drm_mm_node *node, u32 *io)
{
	int ret;

	mutex_lock(&rsx->mm_lock);
	ret = drm_mm_insert_node_generic(&rsx->gart, node, CHUNK, CHUNK, 0, DRM_MM_INSERT_HIGH);
	if (!ret) {
		ret = ps3rsx_gart_get(rsx, node->start, CHUNK);
		if (ret)
			drm_mm_remove_node(node);
		else
			rsx->gart_chunks[node->start / CHUNK].fixed = true;
	}
	mutex_unlock(&rsx->mm_lock);
	if (!ret)
		*io = node->start;
	return ret;
}

/* Give a pinned area back (the GPU no longer uses it) */
void ps3rsx_gart_unpin(struct ps3rsx_device *rsx, struct drm_mm_node *node)
{
	mutex_lock(&rsx->mm_lock);
	if (rsx->gart_dynamic)
		rsx->gart_chunks[node->start / CHUNK].fixed = false;
	ps3rsx_gart_put(rsx, node->start, CHUNK, READ_ONCE(rsx->seqno));
	drm_mm_remove_node(node);
	mutex_unlock(&rsx->mm_lock);
}

/* Kernel address of GART IO address io; valid up to the end of its 1 MiB chunk */
void *ps3rsx_gart_ptr(struct ps3rsx_device *rsx, u64 io)
{
	return page_address(rsx->gart_chunks[io / CHUNK].page) + io % CHUNK;
}

/* Page frame of GART IO address io (for user mappings) */
unsigned long ps3rsx_gart_pfn(struct ps3rsx_device *rsx, u64 io)
{
	return page_to_pfn(rsx->gart_chunks[io / CHUNK].page) + (io % CHUNK >> PAGE_SHIFT);
}

/* Copy len bytes from GART IO address io, across chunks */
void ps3rsx_gart_read(struct ps3rsx_device *rsx, void *dst, u64 io, size_t len)
{
	while (len) {
		size_t n = min_t(size_t, len, CHUNK - io % CHUNK);

		memcpy(dst, ps3rsx_gart_ptr(rsx, io), n);
		dst += n;
		io += n;
		len -= n;
	}
}

/* Zero len bytes at GART IO address io, across chunks */
void ps3rsx_gart_clear(struct ps3rsx_device *rsx, u64 io, size_t len)
{
	while (len) {
		size_t n = min_t(size_t, len, CHUNK - io % CHUNK);

		memset(ps3rsx_gart_ptr(rsx, io), 0, n);
		io += n;
		len -= n;
	}
}

int ps3rsx_gart_init(struct ps3rsx_device *rsx)
{
	unsigned int i, ring = RING_RESERVE / CHUNK;
	struct page *ring_page;
	int ret;

	rsx->gart_dynamic = !ps3rsx_gart.size;
	rsx->gart_size = rsx->gart_dynamic ? ps3rsx_cma_size() : ps3rsx_gart.size;
	if (rsx->gart_size < RING_RESERVE + 2 * CHUNK)
		return dev_err_probe(rsx->drm.dev, -ENOMEM,
				     "no GART (ps3rsx_cma=<size> or ps3rsx_gart=<size>)\n");
	rsx->gart_nchunks = rsx->gart_size / CHUNK;
	rsx->gart_chunks = kcalloc(rsx->gart_nchunks, sizeof(*rsx->gart_chunks), GFP_KERNEL);
	if (!rsx->gart_chunks)
		return -ENOMEM;
	INIT_DELAYED_WORK(&rsx->gart_reclaim, reclaim_work);

	if (!rsx->gart_dynamic) {
		/* the boot reservation: every chunk backed for good, at IO = its offset */
		for (i = 0; i < rsx->gart_nchunks; i++) {
			struct ps3rsx_gart_chunk *c = &rsx->gart_chunks[i];

			c->page = virt_to_page(ps3rsx_gart.address + (u64)i * CHUNK);
			c->fixed = true;
			ret = map_chunk(rsx, i, c->page);
			if (ret)
				goto err;
		}
		rsx->gart_backed = rsx->gart_size;
		return 0;
	}

	/* CMA: the ring region contiguous and for good, one scratch chunk, the rest on demand.
	 * The scratch chunk takes one MiB of the area: the IO range is one MiB smaller. */
	ring_page = ps3rsx_cma_alloc(ring);
	rsx->gart_scratch = ps3rsx_cma_alloc(1);
	if (!ring_page || !rsx->gart_scratch) {
		ret = -ENOMEM;
		drm_err(&rsx->drm, "no CMA memory for the GPU ring\n");
		if (ring_page)
			ps3rsx_cma_free(ring_page, ring);
		goto err;
	}
	memset(page_address(rsx->gart_scratch), 0, CHUNK);
	rsx->gart_nchunks--;
	rsx->gart_size -= CHUNK;
	for (i = 0; i < rsx->gart_nchunks; i++) {
		struct ps3rsx_gart_chunk *c = &rsx->gart_chunks[i];

		if (i < ring) {
			c->page = nth_page(ring_page, i * (CHUNK >> PAGE_SHIFT));
			c->fixed = true;
			rsx->gart_backed += CHUNK;
		}
		ret = map_chunk(rsx, i, c->page ?: rsx->gart_scratch);
		if (ret)
			goto err;
	}
	return 0;
err:
	ps3rsx_gart_fini(rsx);
	return ret;
}

void ps3rsx_gart_fini(struct ps3rsx_device *rsx)
{
	unsigned int i, ring = RING_RESERVE / CHUNK;

	if (!rsx->gart_chunks)
		return;
	cancel_delayed_work_sync(&rsx->gart_reclaim);
	/* unmap everything (the GPU is idle), then give the CMA memory back */
	for (i = 0; i < rsx->gart_nchunks; i++) {
		struct page *page = rsx->gart_chunks[i].page ?: rsx->gart_scratch;

		if (page)
			ps3fb_gpu_iomap((u64)i * CHUNK, ps3_mm_phys_to_lpar(page_to_phys(page)),
					CHUNK, CBE_IOPTE_M);
	}
	if (rsx->gart_dynamic) {
		for (i = ring; i < rsx->gart_nchunks; i++)
			if (rsx->gart_chunks[i].page)
				ps3rsx_cma_free(rsx->gart_chunks[i].page, 1);
		if (rsx->gart_chunks[0].page)
			ps3rsx_cma_free(rsx->gart_chunks[0].page, ring);
		if (rsx->gart_scratch)
			ps3rsx_cma_free(rsx->gart_scratch, 1);
	}
	kfree(rsx->gart_chunks);
	rsx->gart_chunks = NULL;
	rsx->gart_scratch = NULL;
	rsx->gart_backed = 0;
}
