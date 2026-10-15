// SPDX-License-Identifier: GPL-2.0
/*
 * Command execution on our lv1 context: one ring for every channel (3D state is shared between
 * hardware channels anyway, docs/RSX.md E5), per-submit fences from SET_REFERENCE (a real
 * completion fence, E11), and wedge detection (any graph exception stops the whole GPU).
 *
 * GART IO layout: [0, RING_START) reset program, [RING_START, RING_END) ring, then BOs.
 */
#include <linux/delay.h>
#include <linux/dma-fence.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <asm/lv1call.h>
#include <asm/ps3gpu.h>

#include "ps3rsx.h"

#define RESET_SIZE	0x9b8		/* ps3fb's reset program, ends with RETURN */
#define CMD_JMP(a)	(0x20000000u | (a))
#define CMD_CALL(a)	((a) | 2)
#define SET_REFERENCE	0x00040050	/* host method 0x50, 1 word, subchannel 0 */

#define CTRL_PUT	0x40
#define CTRL_GET	0x44
#define CTRL_REF	0x48

/*
 * Fence completion is polled (no completion interrupt yet). Fast only while someone waits on a
 * fence (enable_signaling): polling every 250 us for as long as any work was pending meant up to
 * 4000 timer interrupts a second during games, each a costly trip through lv1, and it showed as
 * stutters (2026-10-15). Otherwise a slow poll retires fences and watches for a stalled GPU, and
 * .signaled answers "is it done?" from the reference register directly.
 */
#define POLL_NS		(250 * NSEC_PER_USEC)
#define SLOW_POLL_NS	(100 * NSEC_PER_MSEC)
#define WEDGE_NS	(2 * NSEC_PER_SEC)

void ps3fb_gpu_handles(u64 *memory_handle, u64 *context_handle, u64 *ddr_lpar,
		       void **xdr, unsigned long *xdr_size);

static u32 ref_read(struct ps3rsx_device *rsx)
{
	return in_be32(rsx->ctrl + CTRL_REF);
}

static u32 *ring_cpu(struct ps3rsx_device *rsx, u32 io)
{
	return ps3rsx_gart_ptr(rsx, io);	/* the ring region is one contiguous chunk */
}

/* --- fences ---------------------------------------------------------------------------- */

static const char *fence_driver_name(struct dma_fence *f)
{
	return "ps3rsx";
}

static const char *fence_timeline_name(struct dma_fence *f)
{
	return "rsx-ring";
}

static bool seq_passed(u32 ref, u32 seq);

static struct ps3rsx_device *fence_rsx(struct dma_fence *f)
{
	return container_of(f->lock, struct ps3rsx_device, fence_lock);
}

static bool fence_signaled(struct dma_fence *f)
{
	return seq_passed(ref_read(fence_rsx(f)), f->seqno);
}

/* Someone waits for this fence: poll fast until no waited-for fence is left. fence_lock held. */
static bool fence_enable_signaling(struct dma_fence *f)
{
	struct ps3rsx_device *rsx = fence_rsx(f);

	if (!rsx->fast_poll) {
		rsx->fast_poll = true;
		hrtimer_start(&rsx->fence_timer, ns_to_ktime(POLL_NS), HRTIMER_MODE_REL);
	}
	return true;
}

static const struct dma_fence_ops ps3rsx_fence_ops = {
	.get_driver_name = fence_driver_name,
	.get_timeline_name = fence_timeline_name,
	.enable_signaling = fence_enable_signaling,
	.signaled = fence_signaled,
};

/* Has the GPU passed fence seq? */
bool ps3rsx_seq_done(struct ps3rsx_device *rsx, u32 seq)
{
	return seq_passed(ref_read(rsx), seq);
}

static bool seq_passed(u32 ref, u32 seq)
{
	return (s32)(ref - seq) >= 0;
}

/* Signal what has completed; returns true while fences are still pending. fence_lock held. */
static bool fences_update(struct ps3rsx_device *rsx, u32 ref, int error)
{
	struct ps3rsx_fence *f, *tmp;

	list_for_each_entry_safe(f, tmp, &rsx->fences, head) {
		if (!error && !seq_passed(ref, f->base.seqno))
			break;
		if (error)
			dma_fence_set_error(&f->base, error);
		list_del(&f->head);
		dma_fence_signal_locked(&f->base);
		dma_fence_put(&f->base);
	}
	return !list_empty(&rsx->fences);
}

static enum hrtimer_restart fence_poll(struct hrtimer *t)
{
	struct ps3rsx_device *rsx = container_of(t, struct ps3rsx_device, fence_timer);
	unsigned long flags;
	bool pending;
	u64 interval;
	u32 ref;

	spin_lock_irqsave(&rsx->fence_lock, flags);
	ref = ref_read(rsx);
	if (ref != rsx->last_ref) {
		rsx->last_ref = ref;
		rsx->last_progress = ktime_get();
	} else if (!list_empty(&rsx->fences) &&
		   ktime_to_ns(ktime_sub(ktime_get(), rsx->last_progress)) > WEDGE_NS &&
		   !rsx->wedged) {
		rsx->wedged = true;
		pr_err("ps3rsx: GPU stopped (ref %#x, waiting for %#llx): wedged until reboot\n",
		       ref, list_first_entry(&rsx->fences, struct ps3rsx_fence, head)->base.seqno);
	}
	pending = fences_update(rsx, ref, rsx->wedged ? -EIO : 0);
	/* fast while a remaining fence has a waiter */
	rsx->fast_poll = false;
	if (pending) {
		struct ps3rsx_fence *f;

		list_for_each_entry(f, &rsx->fences, head)
			if (test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &f->base.flags)) {
				rsx->fast_poll = true;
				break;
			}
	}
	interval = rsx->fast_poll ? POLL_NS : SLOW_POLL_NS;
	spin_unlock_irqrestore(&rsx->fence_lock, flags);
	if (!pending)
		return HRTIMER_NORESTART;
	hrtimer_forward_now(t, ns_to_ktime(interval));
	return HRTIMER_RESTART;
}

static void exception_hook(void *data, u64 status, const void __iomem *info)
{
	struct ps3rsx_device *rsx = data;

	/* hard IRQ: record only; the poll timer errors the fences */
	pr_err("ps3rsx: graph exception; last submit from channel %d, %s (pid %d)\n",
	       READ_ONCE(rsx->last_chan), rsx->last_comm, READ_ONCE(rsx->last_pid));
	WRITE_ONCE(rsx->wedged, true);
	hrtimer_start(&rsx->fence_timer, 0, HRTIMER_MODE_REL);
}

/* --- ring -------------------------------------------------------------------------------- */

/*
 * After a wrap, wait until the GPU has fetched the old commands in [ring_put, end), which the
 * next ones overwrite. Before it takes the JMP, GET is past everything written in the new lap
 * (we only write below it); once it has, GET is at or below ring_put and the ring ahead is free.
 * Waiting only for the part being overwritten, not for the whole old lap to be fetched, keeps
 * the CPU from spinning for most of a frame at every wrap.
 */
static int ring_wait_room(struct ps3rsx_device *rsx, u32 end)
{
	int i;

	for (i = 0; i < 200000; i++) {
		u32 get = in_be32(rsx->ctrl + CTRL_GET);

		if (get <= rsx->ring_put) {
			rsx->ring_lapping = false;
			return 0;
		}
		if (get >= end)
			return 0;
		if (READ_ONCE(rsx->wedged))
			return -EIO;
		udelay(10);
	}
	return -ETIMEDOUT;
}

/*
 * Make room for len bytes at the ring's write position. Wrapping writes a JMP back to the
 * start; the start is only rewritten once the GPU has fetched up to the JMP. Called with
 * exec_lock held. Returns the CPU pointer to write at.
 */
u32 *ps3rsx_ring_begin(struct ps3rsx_device *rsx, u32 len, int *err)
{
	*err = 0;
	if (len > RING_END - RING_START - 16) {
		*err = -E2BIG;
		return NULL;
	}
	/* len + SET_REFERENCE (8) + room for the next wrap's JMP (4) */
	if (rsx->ring_put + len + 12 > RING_END) {
		*ring_cpu(rsx, rsx->ring_put) = CMD_JMP(RING_START);
		wmb();
		out_be32(rsx->ctrl + CTRL_PUT, RING_START);
		rsx->ring_put = RING_START;
		rsx->ring_lapping = true;
	}
	if (rsx->ring_lapping) {
		*err = ring_wait_room(rsx, rsx->ring_put + len + 12);
		if (*err)
			return NULL;
	}
	return ring_cpu(rsx, rsx->ring_put);
}

/* Commit len bytes written at ring_begin plus a SET_REFERENCE; returns the fence (or ERR) */
struct dma_fence *ps3rsx_ring_commit(struct ps3rsx_device *rsx, u32 len, bool kick)
{
	struct ps3rsx_fence *f = kzalloc(sizeof(*f), GFP_KERNEL);
	unsigned long flags;
	u32 *p;

	if (!f)
		return ERR_PTR(-ENOMEM);
	p = ring_cpu(rsx, rsx->ring_put + len);
	*p++ = SET_REFERENCE;
	*p++ = ++rsx->seqno;
	dma_fence_init(&f->base, &ps3rsx_fence_ops, &rsx->fence_lock, rsx->fence_context,
		       rsx->seqno);
	dma_fence_get(&f->base);	/* the pending list's reference */

	spin_lock_irqsave(&rsx->fence_lock, flags);
	list_add_tail(&f->head, &rsx->fences);
	if (!kick) {
		/* dry run: nothing goes to the GPU */
		list_del(&f->head);
		dma_fence_signal_locked(&f->base);
		dma_fence_put(&f->base);
		spin_unlock_irqrestore(&rsx->fence_lock, flags);
		return &f->base;
	}
	if (list_is_singular(&rsx->fences))
		rsx->last_progress = ktime_get();
	spin_unlock_irqrestore(&rsx->fence_lock, flags);

	rsx->ring_put += len + 8;
	wmb();
	out_be32(rsx->ctrl + CTRL_PUT, rsx->ring_put);
	if (!hrtimer_active(&rsx->fence_timer))
		hrtimer_start(&rsx->fence_timer, ns_to_ktime(SLOW_POLL_NS), HRTIMER_MODE_REL);
	return &f->base;
}

/* Nothing in flight and no primitive left open: ps3fb may use the GPU */
/*
 * Commit len bytes written after ps3rsx_ring_begin() with a bare SET_REFERENCE, no dma_fence:
 * nothing is allocated (kernel copies on the swap path, kcopy.c). Returns the reference to
 * poll with ps3rsx_seq_done(). exec_lock held.
 */
u32 ps3rsx_ring_commit_seq(struct ps3rsx_device *rsx, u32 len)
{
	u32 *p = ring_cpu(rsx, rsx->ring_put + len);

	*p++ = SET_REFERENCE;
	*p++ = ++rsx->seqno;
	rsx->ring_put += len + 8;
	wmb();
	out_be32(rsx->ctrl + CTRL_PUT, rsx->ring_put);
	return rsx->seqno;
}

bool ps3rsx_exec_idle(struct ps3rsx_device *rsx)
{
	return !READ_ONCE(rsx->prim_owner) && seq_passed(ref_read(rsx), rsx->seqno);
}

/* ps3fb had the GPU: what PGRAPH holds is unknown (no submit is running) */
void ps3rsx_exec_forget_hw_state(struct ps3rsx_device *rsx)
{
	mutex_lock(&rsx->exec_lock);
	ps3rsx_shadow_clear(rsx->hw_shadow);
	rsx->hw_behind = NULL;
	rsx->last_chan = -1;
	mutex_unlock(&rsx->exec_lock);
}

/* Wait for everything submitted so far (unload, ring drain) */
static int exec_drain(struct ps3rsx_device *rsx)
{
	int i;

	for (i = 0; i < 2000; i++) {
		if (seq_passed(ref_read(rsx), rsx->seqno))
			return 0;
		if (READ_ONCE(rsx->wedged))
			return -EIO;
		usleep_range(500, 1000);
	}
	return -ETIMEDOUT;
}

/* --- bring-up ---------------------------------------------------------------------------- */

int ps3rsx_exec_init(struct ps3rsx_device *rsx)
{
	struct ps3fb_gpu_hook *hook = &rsx->hook;
	u64 mem, fbctx, ddr;
	unsigned long xdr_size;
	void *fb_xdr;
	u32 *p;
	int ret, i;

	rsx->hw_shadow = ps3rsx_shadow_new();
	rsx->restore_buf = kvmalloc(RESTORE_MAX_WORDS * 4, GFP_KERNEL);
	rsx->last_chan = -1;
	rsx->prim_owner = NULL;
	init_waitqueue_head(&rsx->prim_wq);
	if (!rsx->hw_shadow || !rsx->restore_buf) {
		ps3rsx_shadow_free(rsx->hw_shadow);
		kvfree(rsx->restore_buf);
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&rsx->fences);
	spin_lock_init(&rsx->fence_lock);
	mutex_init(&rsx->exec_lock);
	hrtimer_init(&rsx->fence_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	rsx->fence_timer.function = fence_poll;
	rsx->fence_context = dma_fence_context_alloc(1);
	ps3rsx_submit_debugfs_init(rsx);

	if (ps3fb_gpu_exception_seen()) {
		drm_err(&rsx->drm, "the GPU has had a graphics exception since boot: reboot first\n");
		return -EIO;
	}
	rsx->ctrl = ioremap(rsx->ctx.ctrl_lpar, PAGE_SIZE);
	rsx->dinfo = ioremap(rsx->ctx.dinfo_lpar, 128 * 1024);
	if (!rsx->ctrl || !rsx->dinfo) {
		if (rsx->ctrl)
			iounmap(rsx->ctrl);
		rsx->ctrl = NULL;
		return -ENOMEM;
	}

	ret = ps3fb_gpu_takeover(true);
	if (ret) {
		drm_err(&rsx->drm, "ps3fb won't hand over the GPU: %d\n", ret);
		goto err_unmap;
	}
	hook->exception = exception_hook;
	hook->data = rsx;
	ps3fb_gpu_set_exception_hook(hook);

	/* ps3fb's reset program (binds the objects and DMA contexts on subchannels 0-6) at IO 0,
	 * CALLed from the ring like ps3fb does on its own context */
	ps3fb_gpu_handles(&mem, &fbctx, &ddr, &fb_xdr, &xdr_size);
	memcpy(ring_cpu(rsx, 0), fb_xdr, RESET_SIZE);
	p = ring_cpu(rsx, RING_START);
	p[0] = CMD_CALL(0);
	p[1] = SET_REFERENCE;
	p[2] = rsx->seqno = 0x5e000000;	/* fence sequence numbers continue from here */
	wmb();
	ret = lv1_gpu_context_attribute(rsx->ctx.handle, L1GPU_CONTEXT_ATTRIBUTE_FIFO_SETUP,
					RING_START, RING_START, 0, 0);
	if (ret) {
		drm_err(&rsx->drm, "FIFO_SETUP failed: %d\n", ret);
		ret = -EIO;
		goto err_release;
	}
	rsx->ring_put = RING_START + 12;
	rsx->ring_lapping = false;
	out_be32(rsx->ctrl + CTRL_PUT, rsx->ring_put);
	for (i = 0; i < 2000 && ref_read(rsx) != rsx->seqno; i++)
		usleep_range(100, 200);
	if (ref_read(rsx) != rsx->seqno) {
		drm_err(&rsx->drm, "GPU didn't run the reset program (get %#x ref %#x): wedged?\n",
			in_be32(rsx->ctrl + CTRL_GET), ref_read(rsx));
		ret = -EIO;
		goto err_release;
	}
	rsx->last_ref = rsx->seqno;
	return 0;

err_release:
	ps3fb_gpu_set_exception_hook(NULL);
	ps3fb_gpu_takeover(false);
err_unmap:
	iounmap(rsx->ctrl);
	iounmap(rsx->dinfo);
	rsx->ctrl = NULL;
	rsx->dinfo = NULL;
	return ret;
}

void ps3rsx_exec_fini(struct ps3rsx_device *rsx)
{
	unsigned long flags;

	if (!rsx->ctrl)
		return;
	if (exec_drain(rsx))
		drm_err(&rsx->drm, "ring didn't drain (ref %#x, last %#x)\n", ref_read(rsx),
			rsx->seqno);
	hrtimer_cancel(&rsx->fence_timer);
	spin_lock_irqsave(&rsx->fence_lock, flags);
	fences_update(rsx, ref_read(rsx), rsx->wedged ? -EIO : -ECANCELED);
	spin_unlock_irqrestore(&rsx->fence_lock, flags);
	ps3fb_gpu_set_exception_hook(NULL);
	if (!rsx->wedged)
		ps3fb_gpu_takeover(false);
	else
		drm_err(&rsx->drm, "GPU wedged: keeping it from ps3fb until reboot\n");
	iounmap(rsx->ctrl);
	rsx->ctrl = NULL;
	if (rsx->dinfo)
		iounmap(rsx->dinfo);
	rsx->dinfo = NULL;
	ps3rsx_shadow_free(rsx->hw_shadow);
	kvfree(rsx->restore_buf);
}
