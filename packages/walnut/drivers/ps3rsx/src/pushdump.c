// SPDX-License-Identifier: GPL-2.0
/*
 * The push dump: a test build's record of what clients submit, for the address checks' host
 * tests (a corpus of real streams). Built only with DEB_BUILD_OPTIONS=ps3rsx-pushdump and a
 * version that says pushdump (debian/rules); it checks nothing and changes no verdict.
 *
 * debugfs dri/N/ps3rsx_push_dump (root, read only, one reader at a time) streams records
 * (pushdump_scan.h) from a ring in kernel memory while test_push_dump is set: every GEM handle
 * made and deleted (the files' buffer sets), every submit with its buffer list, the state
 * restore, its pushes as checked and its verdict, ps3rsx's own ENDs, and what the accepted
 * pushes make the GPU fetch that they don't carry: each fragment program's first bytes and each
 * index batch's range of the index buffer. A full ring drops records and says how many (PD_GAP).
 *
 * The contents are read once the submit's own fence has passed (test_push_dump_contents, on by
 * default), so a program or index buffer that a copy earlier in the same submit wrote is read as
 * the GPU used it; the submit waits for that, so dump runs are slow and never timed. GART is
 * read through its kernel mapping, CPU-visible VRAM through a temporary mapping with 8-byte
 * loads (the VRAM swap's PPE path), GPU-only VRAM not at all (PD_UNREADABLE).
 */
#include <linux/debugfs.h>
#include <linux/dma-fence.h>
#include <linux/io.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_print.h>
#include <drm/nouveau_drm.h>

#include "ps3rsx.h"
#include "pushdump.h"
#include "pushdump_scan.h"

#ifndef __BIG_ENDIAN
#error "the dump copies GPU words as they are in memory: big-endian only"
#endif

static bool test_push_dump;
module_param(test_push_dump, bool, 0600);
MODULE_PARM_DESC(test_push_dump, "TEST BUILD: record submits to debugfs ps3rsx_push_dump");

static bool test_push_dump_contents = true;
module_param(test_push_dump_contents, bool, 0600);
MODULE_PARM_DESC(test_push_dump_contents, "TEST BUILD: wait for each submit and record its "
		 "fragment programs and index ranges");

static unsigned int test_push_dump_kb = 16384;
module_param(test_push_dump_kb, uint, 0444);
MODULE_PARM_DESC(test_push_dump_kb, "TEST BUILD: the dump's ring in KiB (at load)");

static unsigned int test_push_dump_fp_kb = 16;
module_param(test_push_dump_fp_kb, uint, 0600);
MODULE_PARM_DESC(test_push_dump_fp_kb, "TEST BUILD: KiB recorded from a fragment program's start");

static unsigned int test_push_dump_content_kb = 1024;
module_param(test_push_dump_content_kb, uint, 0600);
MODULE_PARM_DESC(test_push_dump_content_kb, "TEST BUILD: KiB of contents recorded per submit");

#define BOUNCE		(64 * 1024)

static struct {
	struct mutex lock;		/* the ring; taken under exec_lock, never around it */
	u8 *ring;
	u64 size, head, tail;		/* head: next write, tail: next read (bytes ever) */
	u64 dropped;			/* records not written since the last PD_GAP */
	bool started;			/* PD_START written since recording went on */
	bool closing;			/* unloading: readers return */
	atomic_t readers;
	wait_queue_head_t wq;
	struct dentry *file;
	struct ps3rsx_device *rsx;
} pd;

struct pd_submit {
	u32 chan, seq, n;
	struct pd_range r[PD_MAX_RANGES];
};

struct pd_piece {
	const void *p;
	size_t n;
};

static void ring_put(const void *src, size_t n)
{
	while (n) {
		u64 at = pd.head % pd.size;
		size_t k = min_t(u64, n, pd.size - at);

		memcpy(pd.ring + at, src, k);
		pd.head += k;
		src += k;
		n -= k;
	}
}

/* Room for len more bytes, after a PD_GAP for what was dropped before: false counts a drop */
static bool reserve_locked(size_t len)
{
	u8 gap[PD_GAP_BYTES];
	u64 room = pd.size - (pd.head - pd.tail);

	if (room < len + (pd.dropped ? sizeof(gap) : 0)) {
		pd.dropped++;
		return false;
	}
	if (pd.dropped) {
		pd_rec_gap(gap, READ_ONCE(pd.rsx->seqno), pd.dropped);
		ring_put(gap, sizeof(gap));
		pd.dropped = 0;
	}
	return true;
}

static bool emit_locked(const struct pd_piece *pc, int npc)
{
	size_t len = 0;
	int i;

	for (i = 0; i < npc; i++)
		len += pc[i].n;
	if (!reserve_locked(len))
		return false;
	for (i = 0; i < npc; i++)
		ring_put(pc[i].p, pc[i].n);
	return true;
}

/* The start record, once each time recording comes on: the address space's kernel ranges */
static void start_locked(void)
{
	struct ps3rsx_device *rsx = pd.rsx;
	u8 r[PD_START_BYTES];
	struct pd_piece pc = { r, sizeof(r) };

	pd_rec_start(r, READ_ONCE(rsx->seqno), rsx->info.reserved, rsx->info.memory_size,
		     rsx->gart_size, CPU_VRAM_LIMIT, RING_RESERVE);
	if (emit_locked(&pc, 1))
		pd.started = true;
}

/* Is recording on? With pd.lock held: writes PD_START first when it just came on */
static bool recording_locked(void)
{
	if (!READ_ONCE(test_push_dump) || !pd.ring) {
		pd.started = false;
		return false;
	}
	if (!pd.started)
		start_locked();
	return pd.started;
}

static void emit(const struct pd_piece *pc, int npc)
{
	mutex_lock(&pd.lock);
	if (recording_locked() && emit_locked(pc, npc))
		wake_up_interruptible(&pd.wq);
	mutex_unlock(&pd.lock);
}

static u32 dom_of(u32 gem_domain)
{
	return gem_domain == NOUVEAU_GEM_DOMAIN_VRAM ? PD_DOM_VRAM : PD_DOM_GART;
}

void ps3rsx_pd_bo_event(struct ps3rsx_bo *bo, struct drm_file *file, bool open)
{
	struct ps3rsx_file *pf = file ? file->driver_priv : NULL;
	u8 r[PD_BO_EVENT_BYTES];
	struct pd_piece pc = { r, sizeof(r) };

	if (!READ_ONCE(test_push_dump))
		return;
	/* the device from the BO, not pd.rsx: an event racing the unload's ps3rsx_pd_fini() then
	 * finds recording off under pd.lock (emit()) instead of a NULL device */
	pd_rec_bo_event(r, open, READ_ONCE(to_rsx(bo->base.dev)->seqno),
			pf && pf->owner ? lower_32_bits(pf->owner->id) : 0, task_tgid_nr(current),
			bo->kind, dom_of(bo->domain), bo->offset, bo->base.size);
	emit(&pc, 1);
}

static u32 push_words(const struct drm_nouveau_gem_pushbuf_push *push)
{
	return (push->length & ~(u64)NOUVEAU_GEM_PUSHBUF_NO_PREFETCH) / 4;
}

/* The records of a submit; pd.lock held, recording on */
static void submit_records(struct ps3rsx_chan *ch, u32 seq, bool accepted, u32 bad_push,
			   const struct drm_nouveau_gem_pushbuf_push *push, u32 nr_push,
			   const struct drm_nouveau_gem_pushbuf_bo *bos,
			   struct drm_gem_object **objs, u32 nr_buffers,
			   const u32 *restore, u32 restore_words, const u32 *pushes)
{
	u32 i, sent = accepted ? nr_push : bad_push + 1;
	u8 hdr[PD_SUBMIT_BYTES], bo[PD_BO_BYTES];
	char comm[TASK_COMM_LEN] = {};
	const u32 *w = pushes;

	BUILD_BUG_ON(TASK_COMM_LEN != 16);
	get_task_comm(comm, current);
	pd_rec_submit(hdr, seq, ch->id, lower_32_bits(ch->serial),
		      ch->pf->owner ? lower_32_bits(ch->pf->owner->id) : 0, task_tgid_nr(current),
		      accepted ? PD_ACCEPTED : PD_REJECTED, accepted ? ~0u : bad_push, nr_push,
		      nr_buffers, comm);
	/* the buffers one by one after the header: no allocation */
	if (!reserve_locked(sizeof(hdr) + (u64)nr_buffers * sizeof(bo)))
		return;
	ring_put(hdr, sizeof(hdr));
	for (i = 0; i < nr_buffers; i++) {
		struct ps3rsx_bo *b = to_bo(objs[i]);

		pd_rec_bo(bo, bos[i].handle, b->kind, dom_of(b->domain), lower_32_bits(b->offset),
			  b->base.size, bos[i].read_domains, bos[i].write_domains);
		ring_put(bo, sizeof(bo));
	}
	if (restore_words) {
		u8 r[PD_RESTORE_BYTES];
		struct pd_piece pc[2] = { { r, sizeof(r) }, { restore, restore_words * 4 } };

		pd_rec_restore(r, seq, ch->id, restore_words);
		emit_locked(pc, 2);
	}
	for (i = 0; i < sent; i++) {
		u32 n = push_words(&push[i]);
		u8 r[PD_PUSH_BYTES];
		struct pd_piece pc[2] = { { r, sizeof(r) }, { w, n * 4 } };

		pd_rec_push(r, seq, ch->id, i, n);
		emit_locked(pc, 2);
		w += n;
	}
	wake_up_interruptible(&pd.wq);
}

/*
 * A submit, accepted (after its commit) or rejected (before ps3rsx's END can reuse its ring
 * space): its records, with the restore and the pushes read from the ring where they were
 * checked (exec_lock held). An accepted one's pushes are also scanned, always, so the channel's
 * binds and index buffer are known when recording starts later. Returns what to read once its
 * fence has passed, or NULL.
 */
struct pd_submit *ps3rsx_pd_submit(struct ps3rsx_device *rsx, struct ps3rsx_chan *ch,
				   bool accepted, u32 bad_push,
				   const struct drm_nouveau_gem_pushbuf_push *push, u32 nr_push,
				   const struct drm_nouveau_gem_pushbuf_bo *bos,
				   struct drm_gem_object **objs, u32 nr_buffers,
				   const u32 *restore, u32 restore_words, const u32 *pushes)
{
	u32 seq = READ_ONCE(rsx->seqno), i;
	struct pd_submit *ps = NULL;
	struct pd_scan scan;
	bool rec;

	if (!pd.ring)
		return NULL;
	mutex_lock(&pd.lock);
	rec = recording_locked();
	if (rec)
		submit_records(ch, seq, accepted, bad_push, push, nr_push, bos, objs, nr_buffers,
			       restore, restore_words, pushes);
	if (!accepted)
		goto out;
	if (rec && READ_ONCE(test_push_dump_contents))
		ps = kmalloc(sizeof(*ps), GFP_KERNEL);
	pd_scan_begin(&scan, &ch->pd, ps ? ps->r : NULL, ps ? PD_MAX_RANGES : 0,
		      READ_ONCE(test_push_dump_fp_kb) * 1024);
	for (i = 0; i < nr_push; i++) {
		u32 n = push_words(&push[i]);

		pd_scan_push(&scan, pushes, n);
		pushes += n;
	}
	ch->pd = scan.st;		/* the channel's binds and index buffer from now on */
	if (ps && scan.n) {
		ps->chan = ch->id;
		ps->seq = seq;
		ps->n = scan.n;
	} else {
		kfree(ps);
		ps = NULL;
	}
out:
	mutex_unlock(&pd.lock);
	return ps;
}

/* ps3rsx ended the channel's open primitive: header is its BEGIN_END word's (the data is 0) */
void ps3rsx_pd_end(struct ps3rsx_device *rsx, struct ps3rsx_chan *ch, u32 header)
{
	u8 r[PD_END_BYTES];
	struct pd_piece pc = { r, sizeof(r) };

	if (!READ_ONCE(test_push_dump) || !pd.ring)
		return;
	pd_rec_end(r, READ_ONCE(rsx->seqno), ch->id, header);
	emit(&pc, 1);
}

/* CPU-visible VRAM [at, at + len) into dst: 8-byte loads from a temporary mapping */
static int vram_read(struct ps3rsx_device *rsx, void *dst, u64 at, u32 len)
{
	u64 a = at & ~7ULL, b = ALIGN(at + len, 8), i;
	void __iomem *io;
	u64 *tmp;

	tmp = kvmalloc(b - a, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;
	io = ioremap(rsx->info.ddr_lpar + a, b - a);
	if (!io) {
		kvfree(tmp);
		return -ENOMEM;
	}
	for (i = 0; i < b - a; i += 8)
		tmp[i / 8] = __raw_readq(io + i);
	iounmap(io);
	memcpy(dst, (u8 *)tmp + (at - a), len);
	kvfree(tmp);
	return 0;
}

/* The submit's buffer that holds [lo, lo + 1) in that domain, or NULL */
static struct ps3rsx_bo *find_bo(struct drm_gem_object **objs, u32 nr, u8 dom, u64 lo)
{
	u32 i;

	for (i = 0; i < nr; i++) {
		struct ps3rsx_bo *bo = to_bo(objs[i]);

		if (bo->kind == BO_REPORTS || !drm_mm_node_allocated(&bo->node) ||
		    dom_of(bo->domain) != dom)
			continue;
		if (lo >= bo->offset && lo < bo->offset + bo->base.size)
			return bo;
	}
	return NULL;
}

void ps3rsx_pd_contents(struct ps3rsx_device *rsx, struct pd_submit *ps, struct dma_fence *fence,
			struct drm_gem_object **objs, u32 nr_buffers)
{
	u64 budget = (u64)READ_ONCE(test_push_dump_content_kb) * 1024;
	bool passed = false;
	u32 i;

	if (!ps)
		return;
	if (fence)
		passed = dma_fence_wait_timeout(fence, false, HZ) > 0 &&
			 dma_fence_get_status(fence) == 1;
	for (i = 0; i < ps->n; i++) {
		const struct pd_range *r = &ps->r[i];
		struct ps3rsx_bo *bo = find_bo(objs, nr_buffers, r->dom, r->lo);
		u32 status = passed ? PD_READ_CPU : PD_READ_EARLY, got = 0;
		u8 hdr[PD_CONTENT_BYTES];
		u64 want = r->hi - r->lo;
		void *buf = NULL;

		if (!bo) {
			status = PD_NO_BO;
		} else if (bo->kind == BO_VRAM_GPU_ONLY ||
			   (bo->kind == BO_VRAM && r->lo + 1 > CPU_VRAM_LIMIT)) {
			status = PD_UNREADABLE;
		} else {
			u64 n = min(r->hi, bo->offset + bo->base.size) - r->lo;

			if (bo->kind == BO_VRAM)
				n = min_t(u64, n, CPU_VRAM_LIMIT - r->lo);
			if (n > budget) {
				n = budget;
				status = PD_CUT;
			}
			buf = n ? kvzalloc(ALIGN(n, 4), GFP_KERNEL) : NULL;
			if (buf) {
				if (bo->kind == BO_GART)
					ps3rsx_gart_read(rsx, buf, r->lo, n);
				else if (vram_read(rsx, buf, r->lo, n))
					n = 0;
				got = n;
				budget -= n;
			}
		}
		pd_rec_content(hdr, ps->seq, ps->chan, r->what, r->dom, lower_32_bits(r->lo),
			       lower_32_bits(want), status, got);
		{
			struct pd_piece pc[2] = { { hdr, sizeof(hdr) }, { buf, ALIGN(got, 4) } };

			emit(pc, got ? 2 : 1);
		}
		kvfree(buf);
	}
	kfree(ps);
}

/* --- the debugfs file ------------------------------------------------------------------- */

static int dump_open(struct inode *inode, struct file *f)
{
	if (atomic_cmpxchg(&pd.readers, 0, 1))
		return -EBUSY;
	return 0;
}

static int dump_release(struct inode *inode, struct file *f)
{
	atomic_set(&pd.readers, 0);
	return 0;
}

static ssize_t dump_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
	size_t n = 0, k;
	u8 *bounce;
	int ret;

	ret = wait_event_interruptible(pd.wq, READ_ONCE(pd.head) != READ_ONCE(pd.tail) ||
				       READ_ONCE(pd.closing));
	if (ret)
		return ret;
	if (READ_ONCE(pd.closing))
		return 0;
	bounce = kvmalloc(BOUNCE, GFP_KERNEL);
	if (!bounce)
		return -ENOMEM;
	mutex_lock(&pd.lock);
	count = min_t(u64, min_t(size_t, count, BOUNCE), pd.head - pd.tail);
	while (n < count) {
		u64 at = pd.tail % pd.size;

		k = min_t(u64, count - n, pd.size - at);
		memcpy(bounce + n, pd.ring + at, k);
		pd.tail += k;
		n += k;
	}
	mutex_unlock(&pd.lock);
	/* no lock held over the user copy: it may fault */
	ret = copy_to_user(ubuf, bounce, n) ? -EFAULT : 0;
	kvfree(bounce);
	if (ret)
		return ret;
	*ppos += n;
	return n;
}

static const struct file_operations dump_fops = {
	.owner = THIS_MODULE,
	.open = dump_open,
	.release = dump_release,
	.read = dump_read,
	.llseek = noop_llseek,
};

/* After drm_dev_register (the minor's debugfs directory exists then) */
void ps3rsx_pd_init(struct ps3rsx_device *rsx)
{
	mutex_init(&pd.lock);
	init_waitqueue_head(&pd.wq);
	atomic_set(&pd.readers, 0);
	pd.size = (u64)test_push_dump_kb * 1024;
	pd.ring = pd.size ? vmalloc(pd.size) : NULL;
	if (!pd.ring) {
		drm_warn(&rsx->drm, "push dump: no ring (%u KiB)\n", test_push_dump_kb);
		return;
	}
	pd.head = pd.tail = pd.dropped = 0;
	pd.closing = false;
	pd.rsx = rsx;
	pd.file = debugfs_create_file("ps3rsx_push_dump", 0400, rsx->drm.primary->debugfs_root,
				      NULL, &dump_fops);
	drm_info(&rsx->drm, "TEST BUILD: push dump ready (%u KiB ring, test_push_dump=1 to record)\n",
		 test_push_dump_kb);
}

/* Before drm_dev_unregister */
void ps3rsx_pd_fini(struct ps3rsx_device *rsx)
{
	WRITE_ONCE(pd.closing, true);	/* a blocked reader returns: the removal waits for it */
	wake_up_interruptible(&pd.wq);
	debugfs_remove(pd.file);
	pd.file = NULL;
	mutex_lock(&pd.lock);
	vfree(pd.ring);
	pd.ring = NULL;
	pd.rsx = NULL;
	mutex_unlock(&pd.lock);
}
