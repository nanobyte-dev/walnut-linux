// SPDX-License-Identifier: GPL-2.0
/*
 * Freed memory is cleared before reuse (the rules: vclear.h).
 *
 * VRAM: a freed BO's range, a chunk the VRAM swap gives back, and at load every free range of
 * both heaps (whatever GameOS or an earlier load left) stay allocated in their heap as zombies
 * until a GPU fill of them has passed. The fill is an M2MF copy from the 64 KiB zero area at the
 * top of the ring reserve (ZERO_START), queued by this worker between client submits, in
 * batches: 2 MiB while a client is drawing (about 1 ms of GPU time in front of its next frame),
 * 8 MiB when idle or when an allocation waits. Where the GPU can't fill (display-only mode,
 * ctxsw=0, dry_run, or the GPU fills turned off after one of them trapped), the PPE clears the
 * CPU-visible zombies through a guarded mapping; GPU-only ones then stay until unload.
 *
 * GART: a freed GART BO's range only waits until the GPU passed the reference submitted before
 * the free (new GART BOs are cleared by the CPU when they are made, mem.c), so queued GPU work
 * of the old owner can't land in the new one's buffer.
 *
 * One ordered worker owns every zombie's fill state; the free paths only add zombies (vc.lock,
 * a spinlock), and GART zombies are also reaped inline by the allocator and the free path.
 * While it waits for exec_lock the worker holds no other lock; it releases exec_lock before
 * ps3rsx_console_unhold. Lock order (ps3rsx.h): own_lock -> exec_lock -> mm_lock -> vc.lock.
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/overflow.h>
#include <linux/sched/signal.h>
#include <linux/seq_file.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <drm/drm_print.h>

#include "ps3rsx.h"
#include "pushbuf.h"
#include "vclear.h"

#define BATCH_IDLE	(8 * SZ_1M)	/* bytes per batch when no client is drawing */
#define BATCH_BUSY	(2 * SZ_1M)	/* and while one is (smoothness first) */
#define BATCH_LAUNCHES	(BATCH_IDLE / VCLEAR_ZERO_SIZE)
#define BATCH_ZOMBIES	64		/* zombies in one batch */
#define BUSY_WINDOW	(HZ / 10)	/* a client submitted this recently: it is drawing */
#define FREE_DELAY	msecs_to_jiffies(4)	/* a free waits this long for more (one batch) */
#define RETRY		msecs_to_jiffies(20)	/* a fill that couldn't run looks again */
#define GART_RETRY	msecs_to_jiffies(100)	/* GART zombies nobody reaps inline */
#define AWAY_RETRY	msecs_to_jiffies(500)	/* X switched away (master_set kicks too) */
#define STUCK_RETRY	msecs_to_jiffies(5000)	/* a batch that didn't pass */
#define LOAD_PIECE	(64ULL * SZ_1M)	/* load-time zombies, so allocations start early */

#ifdef PS3RSX_TEST_CLEAR
/* boot 2's attribution arm only: frees as before, nothing cleared (never in a release build) */
static bool test_clear_off;
module_param(test_clear_off, bool, 0444);
MODULE_PARM_DESC(test_clear_off, "TEST BUILD: don't clear freed VRAM (as before P3)");
#else
#define test_clear_off false
#endif

static bool zombie_cpu(const struct ps3rsx_zombie *z)
{
	return z->start < CPU_VRAM_LIMIT;
}

static void count(struct ps3rsx_vclear *vc, const struct ps3rsx_zombie *z, s64 sign)
{
	u64 *b = z->kind == ZOMBIE_GART ? &vc->gart_bytes :
		 zombie_cpu(z) ? &vc->cpu_bytes : &vc->gpu_bytes;

	*b += sign * (s64)z->size;
	if (z->from_vswap)
		vc->vswap_bytes += sign * (s64)z->size;
	if (vc->cpu_bytes + vc->gpu_bytes > vc->peak_bytes)
		vc->peak_bytes = vc->cpu_bytes + vc->gpu_bytes;
}

void ps3rsx_vclear_kick(struct ps3rsx_device *rsx, unsigned long delay)
{
	struct ps3rsx_vclear *vc = &rsx->vc;

	if (!READ_ONCE(vc->started))
		return;
	if (delay)
		queue_delayed_work(vc->wq, &vc->work, delay);
	else
		mod_delayed_work(vc->wq, &vc->work, 0);
}

/* A zombie whose node is allocated in its heap joins the list (any context) */
static void zombie_add(struct ps3rsx_device *rsx, struct ps3rsx_zombie *z)
{
	struct ps3rsx_vclear *vc = &rsx->vc;
	unsigned long flags;

	z->free_seq = READ_ONCE(rsx->seqno);
	if (z->kind != ZOMBIE_GART)
		vclear_cursor_init(&z->cur, z->start, z->size);
	spin_lock_irqsave(&vc->lock, flags);
	list_add_tail(&z->link, z->kind == ZOMBIE_GART ? &vc->gart : &vc->vram);
	count(vc, z, 1);
	spin_unlock_irqrestore(&vc->lock, flags);
}

static void zombie_free(struct ps3rsx_zombie *z)
{
	if (z->bo)
		kfree(z->bo);		/* the zombie is the freed BO itself */
	else
		kfree(z);
}

enum fill_mode { FILL_GPU, FILL_PPE, FILL_LATER, FILL_STUCK };
static enum fill_mode fill_mode(struct ps3rsx_device *rsx);
static const char *ppe_why(struct ps3rsx_device *rsx);

/* Done zombies back to their heaps (process context, no lock held) */
static void zombies_release(struct ps3rsx_device *rsx, struct list_head *done)
{
	struct ps3rsx_vclear *vc = &rsx->vc;
	struct ps3rsx_zombie *z, *tmp;
	unsigned long flags;
	u64 bytes = 0;

	if (list_empty(done))
		return;
	mutex_lock(&rsx->mm_lock);
	list_for_each_entry(z, done, link) {
		if (z->kind == ZOMBIE_GART)
			ps3rsx_gart_put(rsx, z->start, z->size, z->free_seq);
		drm_mm_remove_node(z->node);
		bytes += z->size;
	}
	mutex_unlock(&rsx->mm_lock);
	list_for_each_entry_safe(z, tmp, done, link) {
		bool all = false, cpu = false;

		spin_lock_irqsave(&vc->lock, flags);
		count(vc, z, -1);
		if (z->at_load) {
			all = !--vc->load_pending;
			if (zombie_cpu(z))
				cpu = !--vc->load_cpu_pending;
		}
		spin_unlock_irqrestore(&vc->lock, flags);
		/* the boot log says when the free ranges of the load were cleared, and how: all of
		 * it by the GPU, or the CPU-visible part by the CPU (GPU-only VRAM then stays) */
		if (all && !vc->load_logged) {
			vc->load_logged = true;
			drm_info(&rsx->drm, "free VRAM cleared at load: %llu MiB in %lld ms (%s)\n",
				 vc->load_bytes >> 20, ktime_ms_delta(ktime_get(), vc->load_t0),
				 vc->ppe_bytes ? "GPU and CPU" : "GPU");
		} else if (cpu && !vc->load_logged && fill_mode(rsx) == FILL_PPE) {
			vc->load_logged = true;
			drm_info(&rsx->drm, "free VRAM cleared at load by the CPU (%s): %llu MiB in %lld ms, %llu MiB of GPU-only VRAM left unused\n",
				 ppe_why(rsx), vc->load_cpu_bytes >> 20,
				 ktime_ms_delta(ktime_get(), vc->load_t0),
				 (vc->load_bytes - vc->load_cpu_bytes) >> 20);
		}
		list_del(&z->link);
		zombie_free(z);
	}
	spin_lock_irqsave(&vc->lock, flags);
	vc->reaped += bytes;
	spin_unlock_irqrestore(&vc->lock, flags);
	wake_up_all(&vc->reaped_wq);
}

/* the GPU stopped for good: nothing queued will run (gart.c's reclaim uses the same test) */
static bool gpu_gone(struct ps3rsx_device *rsx)
{
	return ps3rsx_uw_display_only() && READ_ONCE(rsx->wedged);
}

/* GART zombies whose reference passed go back (any process context without ps3rsx locks) */
void ps3rsx_vclear_reap_gart(struct ps3rsx_device *rsx)
{
	struct ps3rsx_vclear *vc = &rsx->vc;
	struct ps3rsx_zombie *z, *tmp;
	unsigned long flags;
	LIST_HEAD(done);
	bool gone = gpu_gone(rsx), left;

	if (list_empty_careful(&vc->gart))
		return;
	spin_lock_irqsave(&vc->lock, flags);
	list_for_each_entry_safe(z, tmp, &vc->gart, link)
		if (gone || ps3rsx_seq_done(rsx, z->free_seq))
			list_move_tail(&z->link, &done);
	left = !list_empty(&vc->gart);
	spin_unlock_irqrestore(&vc->lock, flags);
	zombies_release(rsx, &done);
	if (left)
		ps3rsx_vclear_kick(rsx, GART_RETRY);
}

/*
 * bo_free: the BO's range becomes a zombie, the BO struct its carrier (no allocation here).
 * Returns false when the range may go back at once (no node, a reports BO, the test switch).
 */
bool ps3rsx_vclear_bo_free(struct ps3rsx_device *rsx, struct ps3rsx_bo *bo)
{
	struct ps3rsx_zombie *z = &bo->zombie;

	if (!drm_mm_node_allocated(&bo->node) || bo->kind == BO_REPORTS ||
	    (test_clear_off && bo->kind != BO_GART))
		return false;
	z->node = &bo->node;
	z->bo = bo;
	z->start = bo->node.start;
	z->size = bo->node.size;
	z->kind = bo->kind == BO_GART ? ZOMBIE_GART : ZOMBIE_VRAM;
	z->from_vswap = false;
	zombie_add(rsx, z);
	if (z->kind == ZOMBIE_GART)
		ps3rsx_vclear_reap_gart(rsx);
	else
		ps3rsx_vclear_kick(rsx, FREE_DELAY);
	return true;
}

/* CPU-visible VRAM [lo, hi) zeroed by the PPE: guarded stores, never dcbz (cache-inhibited
 * memory); false if it can't be mapped. may_sleep: cond_resched between megabytes. */
static bool zero_range(struct ps3rsx_device *rsx, u64 lo, u64 hi, bool may_sleep)
{
	u64 at;

	for (at = lo; at < hi; at += SZ_1M) {
		u64 n = min_t(u64, SZ_1M, hi - at), i;
		void __iomem *p = ioremap(rsx->info.ddr_lpar + at, n);

		if (!p)
			return false;
		for (i = 0; i < n; i += 8)
			__raw_writeq(0, p + i);
		iounmap(p);
		if (may_sleep)
			cond_resched();
	}
	mb();				/* the stores before the range goes back */
	return true;
}

/* vswap: a zombie for a chunk about to be given back, allocated before the eviction */
struct ps3rsx_zombie *ps3rsx_vclear_zombie_new(void)
{
	return kzalloc(sizeof(struct ps3rsx_zombie), GFP_KERNEL);
}

/*
 * vswap: the chunk's range moves into the zombie's own node, under the caller's mm_lock so no
 * allocation sees it free in between (the chunk's node is reused by the pool later; this kernel
 * has no drm_mm_replace_node). The reserve can't fail: the range was just freed and nothing else
 * ran meanwhile. If it ever did, the PPE zeroes the range before mm_lock is dropped, so it never
 * goes back uncleared, and the caller gets -EBUSY (no zombie); if the PPE can't map it either,
 * the chunk's own node takes the range back and it stays out of use until unload (-ENOMEM).
 */
int ps3rsx_vclear_take_node(struct ps3rsx_device *rsx, struct ps3rsx_zombie *z,
			    struct drm_mm_node *node)
{
	struct drm_mm *mm = node->mm;

	z->own.start = node->start;
	z->own.size = node->size;
	drm_mm_remove_node(node);
	if (WARN_ON(drm_mm_reserve_node(mm, &z->own))) {
		if (zero_range(rsx, z->own.start, z->own.start + z->own.size, false))
			return -EBUSY;		/* free and zeroed */
		/* not even that: the range stays the pool's, out of use until unload */
		if (!WARN_ON_ONCE(drm_mm_reserve_node(mm, node)))
			return -ENOMEM;
		return -EBUSY;
	}
	z->node = &z->own;
	z->start = z->own.start;
	z->size = z->own.size;
	z->kind = ZOMBIE_VRAM;
	z->from_vswap = true;
	return 0;
}

/* vswap: the zombie made by ps3rsx_vclear_take_node joins the list (no lock held) */
void ps3rsx_vclear_add(struct ps3rsx_device *rsx, struct ps3rsx_zombie *z)
{
	if (test_clear_off) {
		mutex_lock(&rsx->mm_lock);
		drm_mm_remove_node(z->node);
		mutex_unlock(&rsx->mm_lock);
		kfree(z);
		return;
	}
	zombie_add(rsx, z);
	ps3rsx_vclear_kick(rsx, 0);
}

/* --- the fill ------------------------------------------------------------------------------ */

/* Why the CPU path runs (the boot log) */
static const char *ppe_why(struct ps3rsx_device *rsx)
{
	if (ps3rsx_dry_run())
		return "dry_run";
	if (!ps3rsx_kcopy_allowed())
		return "ctxsw=0";
	if (READ_ONCE(rsx->vc.gpu_off))
		return "GPU clears off";
	return "display-only";
}

/* How the zombies can be cleared now (worker and allocator) */
static enum fill_mode fill_mode(struct ps3rsx_device *rsx)
{
	struct ps3rsx_vclear *vc = &rsx->vc;

	if (READ_ONCE(vc->gpu_off) || ps3rsx_uw_display_only() || !ps3rsx_kcopy_allowed())
		return FILL_PPE;
	if (READ_ONCE(rsx->recovering))
		return FILL_LATER;
	if (READ_ONCE(rsx->wedged))
		return FILL_STUCK;	/* a stall, or an exception whose recovery isn't coming */
	return FILL_GPU;
}

/* may the PPE clear the zombie now? (no GPU work left that writes it) */
static bool ppe_may(struct ps3rsx_device *rsx, const struct ps3rsx_zombie *z)
{
	if (gpu_gone(rsx) || ps3rsx_dry_run())
		return true;	/* nothing ran since load (dry_run is set at load only) */
	return ps3rsx_seq_done(rsx, z->free_seq) &&
	       (z->cur.next == z->cur.lo || ps3rsx_seq_done(rsx, z->cur.last_seq));
}

/* The PPE clears a CPU-visible zombie: guarded stores, never dcbz (cache-inhibited memory) */
static bool ppe_clear(struct ps3rsx_device *rsx, struct ps3rsx_zombie *z)
{
	if (!zero_range(rsx, z->cur.lo, z->cur.hi, true))
		return false;		/* stays a zombie */
	rsx->vc.ppe_bytes += z->cur.hi - z->cur.lo;
	return true;
}

static void ppe_pass(struct ps3rsx_device *rsx)
{
	struct ps3rsx_vclear *vc = &rsx->vc;
	struct ps3rsx_zombie *z, *tmp;
	unsigned long flags;
	LIST_HEAD(mine);
	LIST_HEAD(done);
	bool waiting = false;

	/* claim the CPU-visible zombies (only this worker takes them off the list) */
	spin_lock_irqsave(&vc->lock, flags);
	list_for_each_entry_safe(z, tmp, &vc->vram, link)
		if (zombie_cpu(z))
			list_move_tail(&z->link, &mine);
	spin_unlock_irqrestore(&vc->lock, flags);
	list_for_each_entry_safe(z, tmp, &mine, link) {
		if (!ppe_may(rsx, z)) {
			waiting = true;
			continue;
		}
		/* every claimed zombie is cleared here, whatever fills were queued for it */
		if (z->cur.lo == z->cur.hi || ppe_clear(rsx, z))
			list_move_tail(&z->link, &done);
	}
	spin_lock_irqsave(&vc->lock, flags);
	list_splice(&mine, &vc->vram);
	spin_unlock_irqrestore(&vc->lock, flags);
	zombies_release(rsx, &done);
	if (waiting)
		ps3rsx_vclear_kick(rsx, RETRY);
}

/*
 * VRAM zombies whose fill passed go back; those queued before a recovery are filled again.
 * Returns 1 while some still need pieces queued, else 0.
 */
static int reap_vram(struct ps3rsx_device *rsx)
{
	struct ps3rsx_vclear *vc = &rsx->vc;
	struct ps3rsx_zombie *z, *tmp;
	unsigned long flags;
	LIST_HEAD(done);
	bool more = false;

	spin_lock_irqsave(&vc->lock, flags);
	list_for_each_entry_safe(z, tmp, &vc->vram, link) {
		bool passed = z->cur.next != z->cur.lo && ps3rsx_seq_done(rsx, z->cur.last_seq);
		enum vclear_state st;

		smp_rmb();		/* the reference before the generation (vclear.h) */
		st = vclear_state(&z->cur, passed, READ_ONCE(rsx->recover_gen));
		if (st == VCLEAR_DONE)
			list_move_tail(&z->link, &done);
		else if (st == VCLEAR_REFILL)
			vc->refills++;
		if (st == VCLEAR_MORE || st == VCLEAR_REFILL)
			more = true;
	}
	spin_unlock_irqrestore(&vc->lock, flags);
	zombies_release(rsx, &done);
	return more;
}

struct batch {
	struct vclear_launch l[BATCH_LAUNCHES];
	unsigned int nl;
	struct ps3rsx_zombie *z[BATCH_ZOMBIES];
	u64 to[BATCH_ZOMBIES];
	unsigned int nz;
	u64 bytes;
};

/*
 * One batch: the next pieces of the VRAM zombies, queued with exec_lock held, waited for, then
 * the reap. The GPU is held (ps3rsx_console_hold_kernel). Returns 1 while pieces are left to
 * queue (call again), 0 when nothing more can be done now, -EAGAIN when it must wait a little
 * (a primitive open).
 */
static int gpu_batch(struct ps3rsx_device *rsx, struct batch *b)
{
	struct ps3rsx_vclear *vc = &rsx->vc;
	struct ps3rsx_zombie *z;
	unsigned long flags, budget;
	unsigned int i, waited;
	u32 seq = 0, gen;
	int ret = 0;

	b->nl = b->nz = 0;
	mutex_lock(&rsx->exec_lock);
	if (READ_ONCE(rsx->wedged) || READ_ONCE(rsx->recovering)) {
		ret = 0;		/* the end of the recovery kicks */
		goto unlock;
	}
	if (rsx->prim_owner) {
		/* nothing may go between BEGIN and END: wait for its owner; end it as a waiting
		 * submitter would (submit.c) only when an allocation waits (vclear.h) */
		if (!vclear_close_primitive(jiffies_to_msecs(jiffies - rsx->prim_since),
					    atomic_read(&vc->waiters) > 0) ||
		    ps3rsx_close_stale_primitive(rsx)) {
			WRITE_ONCE(vc->wait_prim, true);
			ret = -EAGAIN;
			goto unlock;
		}
	}
	WRITE_ONCE(vc->wait_prim, false);
	gen = READ_ONCE(rsx->recover_gen);
	budget = atomic_read(&vc->waiters) ||
		 time_after(jiffies, READ_ONCE(rsx->last_submit) + BUSY_WINDOW) ?
		 BATCH_IDLE : BATCH_BUSY;
	/* the pieces; zombies are added at the tail meanwhile, and only this worker removes any */
	b->bytes = 0;
	spin_lock_irqsave(&vc->lock, flags);
	list_for_each_entry(z, &vc->vram, link) {
		u64 to;
		u32 n;

		if (b->nz == BATCH_ZOMBIES || b->nl == BATCH_LAUNCHES || b->bytes >= budget)
			break;
		if (vclear_state(&z->cur, false, gen) != VCLEAR_MORE &&
		    vclear_state(&z->cur, false, gen) != VCLEAR_REFILL)
			continue;
		n = vclear_take(&z->cur, gen, budget - b->bytes, b->l + b->nl,
				BATCH_LAUNCHES - b->nl, &to);
		if (!n)
			continue;
		for (i = 0; i < n; i++)
			b->bytes += (u64)b->l[b->nl + i].lines * VCLEAR_LINE;
		b->nl += n;
		b->z[b->nz] = z;
		b->to[b->nz++] = to;
	}
	spin_unlock_irqrestore(&vc->lock, flags);
	if (!b->nl)
		goto unlock;
	/* the zero area is zero again before the GPU reads it (cacheable, coherent IOPTE; the
	 * commit's wmb() orders it before PUT, as push data) */
	memset(ps3rsx_gart_ptr(rsx, ZERO_START), 0, VCLEAR_ZERO_SIZE);
	ret = ps3rsx_kcopy_fill(rsx, b->l, b->nl, ZERO_START, &seq);
	if (!ret) {
		for (i = 0; i < b->nz; i++)
			vclear_queued(&b->z[i]->cur, b->to[i], gen, seq);
		vc->batch_seq = seq;
		vc->batch_gen = gen;
		vc->batches++;
		vc->filled_bytes += b->bytes;
	}
unlock:
	mutex_unlock(&rsx->exec_lock);
	if (ret == -EAGAIN)
		return ret;
	if (ret || !b->nl) {
		reap_vram(rsx);		/* zombies with nothing to fill, or a batch before this */
		return 0;
	}

	/* wait for it (sleeping; one batch in flight) */
	for (waited = 0; waited < 2000000; waited += 200) {
		if (ps3rsx_seq_done(rsx, seq) || READ_ONCE(rsx->wedged) ||
		    READ_ONCE(rsx->recover_gen) != gen)
			break;
		usleep_range(100, 200);
	}
	/* a batch not seen to pass (a stuck GPU, or a recovery under it): its zombies again from
	 * their start, and the run ends (the recovery's end kicks; a stuck GPU gets no more ring
	 * words behind it) */
	if (!ps3rsx_seq_done(rsx, seq)) {
		for (i = 0; i < b->nz; i++)
			vclear_cursor_restart(&b->z[i]->cur);
		if (!READ_ONCE(rsx->wedged) && READ_ONCE(rsx->recover_gen) == gen)
			drm_warn_once(&rsx->drm, "a VRAM clear batch didn't finish in 2 s: retrying later\n");
		return -ETIMEDOUT;
	}
	/* a stray write into the zero area since the memset: this batch's zombies again */
	if (memchr_inv(ps3rsx_gart_ptr(rsx, ZERO_START), 0, VCLEAR_ZERO_SIZE)) {
		vc->dirty_zero++;
		for (i = 0; i < b->nz; i++)
			vclear_cursor_restart(&b->z[i]->cur);
	}
	return reap_vram(rsx);
}

/*
 * Did the last batch trap the GPU? Known only once the recovery has found the trapping commit
 * (find_fault, under exec_lock, before it moves the generation on), so it is checked here, at the
 * worker's next run (the recovery's end kicks it), before anything is queued again: the same
 * words would trap again and spend the boot's GPU (a second exception means display-only).
 */
static void trapped_check(struct ps3rsx_device *rsx)
{
	struct ps3rsx_vclear *vc = &rsx->vc;

	if (!vc->batch_seq || READ_ONCE(rsx->recover_gen) == vc->batch_gen)
		return;
	mutex_lock(&rsx->exec_lock);
	if (rsx->fault_known && rsx->fault_chan < 0 && rsx->fault_seq == vc->batch_seq &&
	    !vc->gpu_off) {
		WRITE_ONCE(vc->gpu_off, true);
		drm_err(&rsx->drm, "a VRAM clear (commit %#x) trapped the GPU: GPU clears off until unload, the CPU clears what it can\n",
			vc->batch_seq);
	}
	vc->batch_seq = 0;		/* that batch is accounted for */
	mutex_unlock(&rsx->exec_lock);
}

static void vclear_work(struct work_struct *work)
{
	struct ps3rsx_vclear *vc = container_of(to_delayed_work(work), struct ps3rsx_vclear, work);
	struct ps3rsx_device *rsx = container_of(vc, struct ps3rsx_device, vc);
	int i, ret = 0;

	trapped_check(rsx);
	ps3rsx_vclear_reap_gart(rsx);
	if (list_empty_careful(&vc->vram))
		return;
	switch (fill_mode(rsx)) {
	case FILL_PPE:
		ppe_pass(rsx);
		return;
	case FILL_LATER:
	case FILL_STUCK:
		return;		/* the recovery's end kicks; a stall-wedge stays until unload */
	case FILL_GPU:
		break;
	}
	if (!vc->batch && !(vc->batch = kmalloc(sizeof(*vc->batch), GFP_KERNEL))) {
		ps3rsx_vclear_kick(rsx, RETRY);
		return;
	}
	/* the last batch never passed and no recovery moved on (a GPU stuck without a wedge): no
	 * run, so the console isn't held for another batch's wait */
	if (vc->batch_seq && READ_ONCE(rsx->recover_gen) == vc->batch_gen &&
	    !ps3rsx_seq_done(rsx, vc->batch_seq)) {
		ps3rsx_vclear_kick(rsx, STUCK_RETRY);
		return;
	}
	/* the GPU held across this run's batches: taken from the console once, not per batch */
	if (ps3rsx_console_hold_kernel(rsx)) {
		ps3rsx_vclear_kick(rsx, AWAY_RETRY);	/* X is switched away: master_set kicks */
		return;
	}
	for (i = 0; i < 64; i++) {	/* then let other work run; requeued below */
		ret = gpu_batch(rsx, vc->batch);
		if (ret <= 0 || ps3rsx_console_wanted(rsx))
			break;
	}
	ps3rsx_console_unhold(rsx);	/* exec_lock isn't held here */
	if (ret == -ETIMEDOUT)
		ps3rsx_vclear_kick(rsx, STUCK_RETRY);
	else if (ret > 0)
		ps3rsx_vclear_kick(rsx, 1);
	else if (!list_empty_careful(&vc->vram))
		ps3rsx_vclear_kick(rsx, RETRY);
}

/* --- the allocator's side -------------------------------------------------------------- */

/* What placing a VRAM BO may wait for (mem.c with vclear_alloc_next) */
void ps3rsx_vclear_pending(struct ps3rsx_device *rsx, u64 *cpu, bool *cpu_ok, u64 *gpu,
			   bool *gpu_ok, u64 *shrunk)
{
	struct ps3rsx_vclear *vc = &rsx->vc;
	enum fill_mode m = fill_mode(rsx);
	unsigned long flags;

	spin_lock_irqsave(&vc->lock, flags);
	*cpu = vc->cpu_bytes;
	*gpu = vc->gpu_bytes;
	*shrunk = vc->vswap_bytes;
	spin_unlock_irqrestore(&vc->lock, flags);
	*gpu_ok = READ_ONCE(vc->started) && (m == FILL_GPU || m == FILL_LATER);
	*cpu_ok = READ_ONCE(vc->started) && m != FILL_STUCK;
}

bool ps3rsx_vclear_gart_pending(struct ps3rsx_device *rsx)
{
	return !list_empty_careful(&rsx->vc.gart);
}

/* Wait up to ms for zombies to go back (kicks the worker now): 0, or -EINTR (killed) */
int ps3rsx_vclear_wait(struct ps3rsx_device *rsx, unsigned int ms)
{
	struct ps3rsx_vclear *vc = &rsx->vc;
	u64 seen = READ_ONCE(vc->reaped);
	ktime_t t0 = ktime_get();
	long t;
	u64 took;

	atomic_inc(&vc->waiters);		/* the worker takes big batches meanwhile */
	ps3rsx_vclear_kick(rsx, 0);
	t = wait_event_killable_timeout(vc->reaped_wq, READ_ONCE(vc->reaped) != seen,
					msecs_to_jiffies(ms));
	atomic_dec(&vc->waiters);
	took = ktime_ms_delta(ktime_get(), t0);
	vc->alloc_waits++;
	if (took > vc->alloc_wait_max_ms)
		vc->alloc_wait_max_ms = took;
	return t < 0 ? -EINTR : 0;
}

void ps3rsx_vclear_show(struct seq_file *m, struct ps3rsx_device *rsx)
{
	struct ps3rsx_vclear *vc = &rsx->vc;

	seq_printf(m, "clear: zombies %llu KiB CPU-visible, %llu KiB GPU-only (%llu KiB from the swap), %llu KiB GART, peak %llu KiB\n",
		   vc->cpu_bytes >> 10, vc->gpu_bytes >> 10, vc->vswap_bytes >> 10,
		   vc->gart_bytes >> 10, vc->peak_bytes >> 10);
	seq_printf(m, "clear: %llu KiB filled in %llu batches, %llu KiB by the PPE, %llu refills, %llu dirty zero areas%s%s\n",
		   vc->filled_bytes >> 10, vc->batches, vc->ppe_bytes >> 10, vc->refills,
		   vc->dirty_zero, vc->gpu_off ? ", GPU clears off" : "",
		   test_clear_off ? ", OFF (test switch)" : "");
	seq_printf(m, "clear: allocations waited %llu times (longest %llu ms), %llu swap shrinks for them\n",
		   vc->alloc_waits, vc->alloc_wait_max_ms, vc->alloc_shrinks);
}

/* --- load and unload ----------------------------------------------------------------------- */

/* A heap's whole range [start, end) becomes zombies, at load (the heap is empty, no user yet) */
static int heap_zombies(struct ps3rsx_device *rsx, struct drm_mm *mm, u64 start, u64 end)
{
	while (start < end) {
		struct ps3rsx_zombie *z = kzalloc(sizeof(*z), GFP_KERNEL);
		u64 n = min_t(u64, LOAD_PIECE, end - start);

		if (!z)
			return -ENOMEM;
		z->own.start = start;
		z->own.size = n;
		if (drm_mm_reserve_node(mm, &z->own)) {
			kfree(z);
			return -ENOSPC;
		}
		z->node = &z->own;
		z->start = start;
		z->size = n;
		z->kind = ZOMBIE_VRAM;
		z->at_load = true;
		rsx->vc.load_pending++;
		rsx->vc.load_bytes += n;
		if (zombie_cpu(z)) {
			rsx->vc.load_cpu_pending++;
			rsx->vc.load_cpu_bytes += n;
		}
		zombie_add(rsx, z);
		start += n;
	}
	return 0;
}

/* mem_init, after the heaps ([cpu_start, cpu_end) and [cpu_end, gpu_end)): the lists and the
 * load-time zombies; an error fails the load (ps3rsx_vclear_fini undoes it) */
int ps3rsx_vclear_init(struct ps3rsx_device *rsx, u64 cpu_start, u64 cpu_end, u64 gpu_end)
{
	struct ps3rsx_vclear *vc = &rsx->vc;
	int ret = 0;

	BUILD_BUG_ON(ZERO_END - ZERO_START != VCLEAR_ZERO_SIZE);
	BUILD_BUG_ON(ZERO_START != RING_END || ZERO_END != RING_RESERVE);
	BUILD_BUG_ON(RING_RESERVE % SZ_1M || (ZERO_START / SZ_1M) != ((ZERO_END - 1) / SZ_1M));
	BUILD_BUG_ON(RING_END - RING_START < 2 * MAX_SUBMIT);
	spin_lock_init(&vc->lock);
	INIT_LIST_HEAD(&vc->vram);
	INIT_LIST_HEAD(&vc->gart);
	init_waitqueue_head(&vc->reaped_wq);
	atomic_set(&vc->waiters, 0);
	INIT_DELAYED_WORK(&vc->work, vclear_work);
	vc->wq = alloc_ordered_workqueue("ps3rsx-clear", 0);
	if (!vc->wq)
		return -ENOMEM;
	if (!test_clear_off) {
		ret = heap_zombies(rsx, &rsx->vram_cpu, cpu_start, cpu_end);
		if (!ret)
			ret = heap_zombies(rsx, &rsx->vram_gpu, cpu_end, gpu_end);
	}
	if (!ps3rsx_kcopy_allowed())
		drm_warn(&rsx->drm, "ctxsw=0 or dry_run: freed VRAM is cleared by the CPU, and GPU-only VRAM stays unused\n");
	return ret;
}

/* Before the teardown (remove, probe errors): the worker stops; zombies stay until fini */
void ps3rsx_vclear_stop(struct ps3rsx_device *rsx)
{
	struct ps3rsx_vclear *vc = &rsx->vc;

	WRITE_ONCE(vc->started, false);
	if (vc->wq)
		cancel_delayed_work_sync(&vc->work);
}

/* After console_init (own_lock exists): the worker may run; the load-time fill starts */
void ps3rsx_vclear_start(struct ps3rsx_device *rsx)
{
	rsx->vc.load_t0 = ktime_get();
	WRITE_ONCE(rsx->vc.started, true);
	ps3rsx_vclear_kick(rsx, 0);
}

/* Before mem_fini: the worker stopped, every zombie's node back (the next load clears it) */
void ps3rsx_vclear_fini(struct ps3rsx_device *rsx)
{
	struct ps3rsx_vclear *vc = &rsx->vc;
	struct ps3rsx_zombie *z, *tmp;
	LIST_HEAD(all);

	if (!vc->wq)
		return;
	ps3rsx_vclear_stop(rsx);
	destroy_workqueue(vc->wq);
	vc->wq = NULL;
	list_splice_init(&vc->vram, &all);
	list_splice_init(&vc->gart, &all);
	mutex_lock(&rsx->mm_lock);
	list_for_each_entry_safe(z, tmp, &all, link) {
		if (z->kind == ZOMBIE_GART)
			ps3rsx_gart_put(rsx, z->start, z->size, z->free_seq);
		drm_mm_remove_node(z->node);
		list_del(&z->link);
		zombie_free(z);
	}
	mutex_unlock(&rsx->mm_lock);
	kfree(vc->batch);
	vc->batch = NULL;
}
