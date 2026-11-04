// SPDX-License-Identifier: GPL-2.0
/*
 * Channels and submission (nouveau ABI): CHANNEL_ALLOC/FREE, NOTIFIEROBJ_ALLOC, GROBJ_ALLOC,
 * GPUOBJ_FREE, GEM_PUSHBUF, GEM_CPU_PREP/FINI. After drivers/gpu/drm/nouveau/nouveau_abi16.c
 * and nouveau_gem.c, but pushes are copied into the kernel's ring and validated there instead
 * of being CALLed in user memory.
 */
#include <linux/dma-fence.h>
#include <linux/dma-resv.h>
#include <linux/overflow.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_gem.h>
#include <drm/nouveau_drm.h>

#include "ps3rsx.h"
#include "pushbuf.h"
#include "pushdump.h"
#include "wait.h"

/* At load only: turned off under running clients, the GPU's binds would stop matching each
 * channel's validator state */
static bool ctxsw = true;
module_param(ctxsw, bool, 0444);
MODULE_PARM_DESC(ctxsw, "Restore a channel's GPU state when switching between channels "
		 "(required: the address checks rely on it)");

/*
 * At load: the address checks (validate.c) take a field the channel set as what the GPU holds
 * when its words run, which only the state restore at a channel switch makes true. Without it
 * the module doesn't load.
 */
int ps3rsx_submit_params_ok(void)
{
	if (!READ_ONCE(ctxsw)) {
		pr_err("ps3rsx: ctxsw=0 is not supported with the address checks\n");
		return -EINVAL;
	}
	return 0;
}

static bool trace;
module_param(trace, bool, 0644);
MODULE_PARM_DESC(trace, "Dump every submitted push to the kernel log (small tests only: "
		 "a GL program's worth through netconsole has hung the console)");

/* at load only: the VRAM clear relies on nothing having run when it is set (vclear.c) */
static bool dry_run;
module_param_named(dry_run, dry_run, bool, 0444);
MODULE_PARM_DESC(dry_run, "Validate submissions but send nothing to the GPU");

bool ps3rsx_dry_run(void)
{
	return READ_ONCE(dry_run);
}

static bool refuse_rejected;
module_param(refuse_rejected, bool, 0644);
MODULE_PARM_DESC(refuse_rejected, "A channel with a rejected push (or an open primitive ps3rsx had "
		 "to close) is refused from then on (-EIO), even when its client recovers "
		 "(PS3_RECOVER: it gets -ECANCELED and re-sends its state)");

/* All channels share one reports area; each gets its own fence label and query slice */
#define FENCE_LABEL(id)		(0x20 + 32 * (id))	/* Mesa asks for 32 bytes */
#define QUERY_SLICE(id)		(0x1400 + 0x1000 * (id))
#define NTFY_OFFSET		0x1000

/* pf->lock held; the channel stays valid while it is held */
struct ps3rsx_chan *ps3rsx_chan_find(struct ps3rsx_file *pf, u32 id)
{
	struct ps3rsx_chan *ch;

	list_for_each_entry(ch, &pf->chans, head)
		if (ch->id == id)
			return ch;
	return NULL;
}

/* A reference for a submit, which can outlive CHANNEL_FREE (another thread of the client) */
static struct ps3rsx_chan *chan_get(struct ps3rsx_file *pf, u32 id)
{
	struct ps3rsx_chan *ch;

	mutex_lock(&pf->lock);
	ch = ps3rsx_chan_find(pf, id);
	if (ch)
		kref_get(&ch->ref);
	mutex_unlock(&pf->lock);
	return ch;
}

/* Does the channel's client re-send its state after a cancelled submit (PS3_RECOVER)? */
static bool chan_recovers(struct ps3rsx_chan *ch)
{
	return READ_ONCE(ch->pf->recover) && !READ_ONCE(refuse_rejected);
}

/*
 * End the primitive the GPU holds open for prim_owner (its last accepted submit left it open)
 * with a BEGIN_END stop on its 3D subchannel. The channel's validator state then has no
 * primitive open, so its later vertex data and END are rejected (validate.c) rather than sent
 * outside a primitive. exec_lock held.
 */
static int end_primitive(struct ps3rsx_device *rsx)
{
	struct ps3rsx_chan *owner = rsx->prim_owner;
	struct dma_fence *f;
	u32 *p, sc;
	int ret;

	for (sc = 0; sc < 8 && owner->vstate.bound[sc] != 0x31337000; sc++)
		;
	p = ps3rsx_ring_begin(rsx, 8, &ret);
	if (!p)
		return ret;
	p[0] = (1 << 18) | ((sc & 7) << 13) | 0x1808;	/* BEGIN_END = stop */
	p[1] = 0;
	f = ps3rsx_ring_commit(rsx, 8, !dry_run);
	if (IS_ERR(f))
		return PTR_ERR(f);
	dma_fence_put(f);
	ps3rsx_pd_end(rsx, owner, p[0]);	/* the push dump (test builds) */
	owner->vstate.prim_open = false;
	rsx->prim_owner = NULL;
	wake_up_all(&rsx->prim_wq);
	if (READ_ONCE(rsx->vc.wait_prim))
		ps3rsx_vclear_kick(rsx, 0);	/* a VRAM clear waited for it */
	return 0;
}

/* The owner of an open primitive didn't continue it: end it. Unless its client recovers, mark
 * that channel broken (its next push would continue a primitive that no longer exists; a
 * recovering client's is rejected for it and cancelled). exec_lock held. */
static int close_stale_primitive(struct ps3rsx_device *rsx)
{
	struct ps3rsx_chan *owner = rsx->prim_owner;
	int ret = end_primitive(rsx);

	if (ret)
		return ret;
	pr_warn("ps3rsx: channel %u left a primitive open for 1 s: closed it\n", owner->id);
	if (!chan_recovers(owner))
		owner->broken = true;
	return 0;
}

/* The VRAM clear's worker applies the submitters' rule (vclear.c): exec_lock held */
int ps3rsx_close_stale_primitive(struct ps3rsx_device *rsx)
{
	return rsx->prim_owner ? close_stale_primitive(rsx) : 0;
}

/*
 * A push of the submit was rejected: nothing of the submit reached the GPU, and the channel's
 * validator state and shadow are still what its last accepted submit left. Its client counted
 * on what the submit would have set (Mesa sends state once), so its next draws would run on
 * older state. A client that recovers (PS3_RECOVER) gets -ECANCELED, takes nothing of its own
 * state as set any more and re-sends it all before it draws again; its later pushes are
 * validated as always. Any other is refused from now on (-EIO), as all were before.
 * exec_lock held.
 */
static int submit_rejected(struct ps3rsx_device *rsx, struct ps3rsx_chan *ch)
{
	if (!chan_recovers(ch)) {
		if (!ch->broken)
			pr_warn("ps3rsx: channel %u refused from now on (pid %d, %s)\n",
				ch->id, task_pid_nr(current), current->comm);
		ch->broken = true;
		return -EINVAL;
	}
	/* its last accepted submit left a primitive open, which this one was to continue or end:
	 * end it now (nothing else may reach the GPU inside it) */
	if (ch->vstate.prim_open && rsx->prim_owner == ch && end_primitive(rsx)) {
		ch->broken = true;		/* the ring didn't take it: the GPU is gone */
		return -EIO;
	}
	ch->cancelled++;
	pr_warn_ratelimited("ps3rsx: channel %u: submit cancelled (%u so far; pid %d, %s)\n",
			    ch->id, ch->cancelled, task_pid_nr(current), current->comm);
	return -ECANCELED;
}

/* Bring hw_shadow up to date with the last channel's pushes. exec_lock held. */
static void hw_shadow_sync(struct ps3rsx_device *rsx)
{
	if (rsx->hw_behind) {
		ps3rsx_shadow_take(rsx->hw_behind, rsx->hw_shadow);
		rsx->hw_behind = NULL;
	}
}

/* debugfs dri/N/ps3rsx_hw_state: the state the GPU holds as far as we know (state a test
 * inherits from the clients before it) */
static int hw_state_show(struct seq_file *m, void *unused)
{
	struct drm_debugfs_entry *entry = m->private;
	struct ps3rsx_device *rsx = to_rsx(entry->dev);

	mutex_lock(&rsx->exec_lock);
	hw_shadow_sync(rsx);
	ps3rsx_shadow_show(m, rsx->hw_shadow);
	mutex_unlock(&rsx->exec_lock);
	return 0;
}

#ifdef PS3RSX_P4_COUNT
/* debugfs dri/N/ps3rsx_p4 (test builds): what the address checks would have refused */
static int p4_show(struct seq_file *m, void *unused)
{
	ps3rsx_p4_show(m);
	return 0;
}
#endif

void ps3rsx_submit_debugfs_init(struct ps3rsx_device *rsx)
{
	drm_debugfs_add_file(&rsx->drm, "ps3rsx_hw_state", hw_state_show, NULL);
#ifdef PS3RSX_P4_COUNT
	drm_debugfs_add_file(&rsx->drm, "ps3rsx_p4", p4_show, NULL);
#endif
}

/* For kernel copies (kcopy.c): hw_shadow brought up to date; exec_lock held */
void ps3rsx_hw_shadow_sync(struct ps3rsx_device *rsx)
{
	hw_shadow_sync(rsx);
}

/* Kernel copies rely on the software context switch to hide their state from clients */
bool ps3rsx_kcopy_allowed(void)
{
	return READ_ONCE(ctxsw) && !READ_ONCE(dry_run);
}

/*
 * The last reference is gone (kref_put_mutex: exec_lock held, unlocked here). Nothing of the
 * channel may stay on the GPU side: its shadow, its last-channel mark, an open primitive. Its id
 * (and so its fence label and query slice) is free only from here, so a new channel can't share
 * them with a submit of this one still in the kernel.
 */
static void chan_release(struct kref *ref)
{
	struct ps3rsx_chan *ch = container_of(ref, struct ps3rsx_chan, ref);
	struct ps3rsx_device *rsx = ch->rsx;

	if (rsx->hw_behind == ch->shadow)
		hw_shadow_sync(rsx);
	if (rsx->last_chan == ch->id)
		rsx->last_chan = -1;
	if (rsx->prim_owner == ch) {
		/* display-only or wedged: no GPU writes, just let the others go */
		if (ps3rsx_uw_display_only() || READ_ONCE(rsx->wedged) ||
		    close_stale_primitive(rsx)) {
			rsx->prim_owner = NULL;
			wake_up_all(&rsx->prim_wq);
		}
	}
	mutex_lock(&rsx->chan_lock);
	clear_bit(ch->id, rsx->chan_ids);
	mutex_unlock(&rsx->chan_lock);
	mutex_unlock(&rsx->exec_lock);
	ps3rsx_shadow_free(ch->shadow);
	kfree(ch);
}

/* Never with exec_lock held */
static void chan_put(struct ps3rsx_chan *ch)
{
	kref_put_mutex(&ch->ref, chan_release, &ch->rsx->exec_lock);
}

/*
 * CHANNEL_FREE and the file's close: no lookup finds the channel any more, a submit of it still
 * in the kernel refuses to reach the ring (dead, checked under exec_lock), and the last
 * reference cleans up. pf->lock held.
 */
static void chan_free(struct ps3rsx_device *rsx, struct ps3rsx_chan *ch)
{
	list_del(&ch->head);
	mutex_lock(&rsx->exec_lock);
	ch->dead = true;
	mutex_unlock(&rsx->exec_lock);
	chan_put(ch);
}

void ps3rsx_file_chans_free(struct ps3rsx_device *rsx, struct ps3rsx_file *pf)
{
	struct ps3rsx_chan *ch, *tmp;

	mutex_lock(&pf->lock);
	list_for_each_entry_safe(ch, tmp, &pf->chans, head)
		chan_free(rsx, ch);
	mutex_unlock(&pf->lock);
}

int ps3rsx_ioctl_channel_alloc(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ps3rsx_device *rsx = to_rsx(dev);
	struct ps3rsx_file *pf = file->driver_priv;
	struct drm_nouveau_channel_alloc *req = data;
	struct drm_nouveau_gem_info info = {};
	struct ps3rsx_chan *ch;
	struct ps3rsx_bo *bo;
	int id, ret;

	if (!pf->abi_ok)
		return -EPERM;		/* not a PS3-patched Mesa: its streams would wedge the GPU */
	if (ps3rsx_uw_display_only())
		return -ENODEV;		/* display-only: no 3D (Mesa falls back to software) */
	if (READ_ONCE(pf->refused))
		return -ENODEV;		/* a refused client gets no new channel */
	ch = kzalloc(sizeof(*ch), GFP_KERNEL);
	if (!ch)
		return -ENOMEM;
	ch->shadow = ps3rsx_shadow_new();
	if (!ch->shadow) {
		kfree(ch);
		return -ENOMEM;
	}
	mutex_lock(&rsx->chan_lock);
	id = find_first_zero_bit(rsx->chan_ids, PS3RSX_MAX_CHANNELS);
	if (id < PS3RSX_MAX_CHANNELS) {
		set_bit(id, rsx->chan_ids);
		ch->serial = ++rsx->chan_serial;
	}
	mutex_unlock(&rsx->chan_lock);
	if (id >= PS3RSX_MAX_CHANNELS) {
		ps3rsx_shadow_free(ch->shadow);
		kfree(ch);
		return -ENOSPC;
	}
	ch->id = id;
	ch->pf = pf;
	ch->rsx = rsx;
	ch->vstate.chan_id = id;	/* its fence label and query slice (validate.c) */
	ch->vstate.set = &pf->set;	/* its file's buffers, under pf->set_lock */
	kref_init(&ch->ref);

	/* the notifier memory is the context's reports area (labels at 0, reports at 0x1400) */
	bo = ps3rsx_bo_new_reports(rsx);
	if (IS_ERR(bo)) {
		ret = PTR_ERR(bo);
		goto err;
	}
	ret = ps3rsx_bo_handle(file, bo, &info);
	drm_gem_object_put(&bo->base);
	if (ret)
		goto err;

	mutex_lock(&pf->lock);
	list_add(&ch->head, &pf->chans);
	mutex_unlock(&pf->lock);
	req->channel = id;
	req->pushbuf_domains = NOUVEAU_GEM_DOMAIN_GART;
	req->notifier_handle = info.handle;
	req->nr_subchan = 0;
	return 0;
err:
	mutex_lock(&rsx->chan_lock);
	clear_bit(id, rsx->chan_ids);
	mutex_unlock(&rsx->chan_lock);
	ps3rsx_shadow_free(ch->shadow);
	kfree(ch);
	return ret;
}

int ps3rsx_ioctl_channel_free(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ps3rsx_file *pf = file->driver_priv;
	struct drm_nouveau_channel_free *req = data;
	struct ps3rsx_chan *ch;
	int ret = -ENOENT;

	mutex_lock(&pf->lock);
	ch = ps3rsx_chan_find(pf, req->channel);
	if (ch) {
		chan_free(to_rsx(dev), ch);	/* pf->lock -> exec_lock, as the recovery takes them */
		ret = 0;
	}
	mutex_unlock(&pf->lock);
	return ret;
}

int ps3rsx_ioctl_notifierobj_alloc(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ps3rsx_file *pf = file->driver_priv;
	struct drm_nouveau_notifierobj_alloc *req = data;
	struct ps3rsx_chan *ch;
	int ret = 0;

	mutex_lock(&pf->lock);
	ch = ps3rsx_chan_find(pf, req->channel);
	if (!ch) {
		ret = -ENOENT;
	} else if (req->handle == 0x66606660 && req->size <= 32) {
		req->offset = FENCE_LABEL(ch->id);	/* fence: 3D back-end semaphore label */
	} else if (req->handle == 0x66626660 && req->size <= 0x1000) {
		req->offset = QUERY_SLICE(ch->id);	/* queries: GET_REPORT records */
	} else if (req->handle == 0x66604200 && req->size <= 0x400) {
		req->offset = NTFY_OFFSET;		/* DMA_NOTIFY: CPU side only */
	} else {
		ret = -EINVAL;
	}
	mutex_unlock(&pf->lock);
	return ret;
}

/* Engine objects exist in the hypervisor already: accepting one only checks the handle */
int ps3rsx_ioctl_grobj_alloc(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_nouveau_grobj_alloc *req = data;

	return ps3rsx_known_object(req->handle) || req->handle == 0 ? 0 : -EINVAL;
}

int ps3rsx_ioctl_gpuobj_free(struct drm_device *dev, void *data, struct drm_file *file)
{
	return 0;
}

/* --- GEM_PUSHBUF ------------------------------------------------------------------------- */


static int apply_relocs(struct drm_nouveau_gem_pushbuf_reloc *r, u32 nr_relocs,
			struct drm_nouveau_gem_pushbuf_bo *bos, struct drm_gem_object **objs,
			u32 nr_buffers, u32 push_bo, u64 push_off, u32 *copy, u64 len)
{
	u32 i;

	for (i = 0; i < nr_relocs; i++, r++) {
		struct ps3rsx_bo *bo;
		u64 at;
		u32 v;

		if (r->reloc_bo_index != push_bo || r->bo_index >= nr_buffers)
			continue;
		if (bos[r->bo_index].presumed.valid)
			continue;
		if (!ps3rsx_reloc_word(r->reloc_bo_offset, push_off, len, &at))
			continue;	/* not in this push */
		bo = to_bo(objs[r->bo_index]);
		if (r->flags & NOUVEAU_GEM_RELOC_LOW)
			v = lower_32_bits(bo->offset + r->data);
		else if (r->flags & NOUVEAU_GEM_RELOC_HIGH)
			v = upper_32_bits(bo->offset + r->data);
		else
			v = r->data;
		if (r->flags & NOUVEAU_GEM_RELOC_OR)
			v |= bo->domain == NOUVEAU_GEM_DOMAIN_GART ? r->tor : r->vor;
		copy[at] = v;
	}
	return 0;
}

int ps3rsx_ioctl_gem_pushbuf(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ps3rsx_device *rsx = to_rsx(dev);
	struct ps3rsx_file *pf = file->driver_priv;
	struct drm_nouveau_gem_pushbuf *req = data;
	struct drm_nouveau_gem_pushbuf_bo *bos = NULL;
	struct drm_nouveau_gem_pushbuf_reloc *relocs = NULL;
	struct drm_nouveau_gem_pushbuf_push *push = NULL;
	struct drm_gem_object **objs = NULL;
	struct ww_acquire_ctx ticket;
	struct dma_fence *fence = NULL;
	bool set_held = false;		/* pf->set_lock (the address checks' set) */
	struct pd_submit *pds = NULL;		/* the push dump's reads after the fence */
	bool held = false, switched;
	struct ps3rsx_chan *ch;
	struct ps3rsx_vstate vs;
	bool sync = req->vram_available & NOUVEAU_GEM_PUSHBUF_SYNC;
	u64 total = 0;
	u32 i, *ring, restore, ring_len;
	int ret;

	req->suffix0 = 0;		/* pushes are copied: no RETURN needed after them */
	req->suffix1 = 0;
	req->vram_available = rsx->vram_size;
	req->gart_available = rsx->gart_size - RING_RESERVE;

	ch = chan_get(pf, req->channel);
	if (!ch)
		return -ENOENT;
	ret = 0;
	if (!req->nr_push)
		goto out;		/* Mesa's first call only asks for the suffixes */
	ret = -ENODEV;
	if (ps3rsx_uw_display_only())
		goto out;		/* display-only: the GPU is gone, KMS stays */
	/* an in-place recovery is running: wait for its outcome (10 s at most) */
	ret = -EINTR;
	if (READ_ONCE(rsx->recovering) &&
	    wait_event_interruptible_timeout(rsx->recover_wq, !READ_ONCE(rsx->recovering),
					     10 * HZ) < 0)
		goto out;
	ret = -EIO;
	if (READ_ONCE(rsx->wedged))
		goto out;
	ret = -EINVAL;
	if (req->nr_push > NOUVEAU_GEM_MAX_PUSH || req->nr_buffers > NOUVEAU_GEM_MAX_BUFFERS ||
	    req->nr_relocs > NOUVEAU_GEM_MAX_RELOCS || !req->nr_buffers)
		goto out;

	push = memdup_array_user(u64_to_user_ptr(req->push), req->nr_push, sizeof(*push));
	bos = memdup_array_user(u64_to_user_ptr(req->buffers), req->nr_buffers, sizeof(*bos));
	if (IS_ERR(push) || IS_ERR(bos)) {
		ret = IS_ERR(push) ? PTR_ERR(push) : PTR_ERR(bos);
		push = IS_ERR(push) ? NULL : push;
		bos = IS_ERR(bos) ? NULL : bos;
		goto out;
	}
	if (req->nr_relocs) {
		relocs = memdup_array_user(u64_to_user_ptr(req->relocs), req->nr_relocs,
					   sizeof(*relocs));
		if (IS_ERR(relocs)) {
			ret = PTR_ERR(relocs);
			relocs = NULL;
			goto out;
		}
		for (i = 0; i < req->nr_relocs; i++) {
			ret = ps3rsx_reloc_args_check(relocs[i].reloc_bo_offset);
			if (ret)
				goto out;
		}
	}
	objs = kcalloc(req->nr_buffers, sizeof(*objs), GFP_KERNEL);
	if (!objs) {
		ret = -ENOMEM;
		goto out;
	}
	for (i = 0; i < req->nr_buffers; i++) {
		objs[i] = drm_gem_object_lookup(file, bos[i].handle);
		if (!objs[i]) {
			ret = -ENOENT;
			goto out;
		}
	}
	for (i = 0; i < req->nr_push; i++) {
		u64 len = push[i].length & ~(u64)NOUVEAU_GEM_PUSHBUF_NO_PREFETCH;
		struct ps3rsx_bo *pb;

		if (push[i].bo_index >= req->nr_buffers) {
			ret = -EINVAL;
			goto out;
		}
		pb = to_bo(objs[push[i].bo_index]);
		if (pb->kind != BO_GART) {
			ret = -EINVAL;	/* pushes must come from GART BOs we can read */
			goto out;
		}
		ret = ps3rsx_pushbuf_args_check(push[i].offset, len, pb->base.size, &total);
		if (ret)
			goto out;
	}

	ret = ps3rsx_console_hold(rsx, ch, pf);	/* may wait while a text console has the GPU */
	if (ret)
		goto out;
	held = true;
	/* the file's set stays as checked until the commit: a handle closed meanwhile waits, so its
	 * BO can't be freed between the checks and the ring (mem.c's bo_close) */
	ret = mutex_lock_interruptible(&pf->set_lock);
	if (ret)
		goto out;
	set_held = true;
	mutex_lock(&rsx->exec_lock);
	if (ch->dead) {			/* freed by another thread meanwhile */
		mutex_unlock(&rsx->exec_lock);
		ret = -ENOENT;
		goto out;
	}
	if (ch->fatal) {		/* this client's push trapped the GPU */
		mutex_unlock(&rsx->exec_lock);
		ret = -ENODEV;
		goto out;
	}
	if (ch->lost) {			/* the GPU was recovered under this channel */
		ch->lost = false;
		/* none of its fields is what the GPU holds now: here, under exec_lock, so no
		 * submit of it still in flight can bring them back */
		ps3rsx_vstate_forget(&ch->vstate);
		if (chan_recovers(ch)) {
			ch->cancelled++;
			ret = -ECANCELED;	/* Mesa (0053) re-sends its whole state */
		} else {
			ch->broken = true;
			WRITE_ONCE(ch->pf->refused, true);	/* the whole client */
			ret = -EIO;
		}
		mutex_unlock(&rsx->exec_lock);
		goto out;
	}
	if (ch->broken) {
		mutex_unlock(&rsx->exec_lock);
		ret = -EIO;
		goto out;
	}
	/*
	 * Another channel left a primitive open (Mesa flushes in the middle of big inline
	 * draws): nothing may be written to the GPU until it closes it, or the next state write
	 * raises a graph exception. Wait for it; if it never comes back, close the primitive and
	 * refuse that channel from then on.
	 */
	while (rsx->prim_owner && rsx->prim_owner != ch) {
		long t;

		/* the set too: another channel of this file may be the one to continue (and the
		 * file's GEM closes go on); nothing is checked yet */
		mutex_unlock(&rsx->exec_lock);
		mutex_unlock(&pf->set_lock);
		set_held = false;
		t = wait_event_interruptible_timeout(rsx->prim_wq, !READ_ONCE(rsx->prim_owner), HZ);
		if (t >= 0 && mutex_lock_interruptible(&pf->set_lock))
			t = -EINTR;
		if (t < 0) {
			ret = t;
			goto out;
		}
		set_held = true;
		mutex_lock(&rsx->exec_lock);
		if (!t && rsx->prim_owner && rsx->prim_owner != ch) {
			ret = close_stale_primitive(rsx);
			if (ret) {
				mutex_unlock(&rsx->exec_lock);
				goto out;
			}
		}
	}
	if (ch->dead) {			/* freed while it waited above (exec_lock dropped) */
		mutex_unlock(&rsx->exec_lock);
		ret = -ENOENT;
		goto out;
	}
	/* another channel ran last: put this channel's state back first */
	restore = 0;
	switched = READ_ONCE(ctxsw) && rsx->last_chan != ch->id;
	if (switched) {
		hw_shadow_sync(rsx);
		ret = ps3rsx_shadow_restore(ch->shadow, rsx->hw_shadow, rsx->restore_buf,
					    RESTORE_MAX_WORDS);
		if (ret < 0) {
			mutex_unlock(&rsx->exec_lock);
			goto out;
		}
		restore = ret * 4;
	}
	ret = ps3rsx_pushbuf_ring_len(restore, total, &ring_len);
	if (ret) {
		mutex_unlock(&rsx->exec_lock);
		goto out;
	}
	ring = ps3rsx_ring_begin(rsx, ring_len, &ret);
	if (!ring) {
		mutex_unlock(&rsx->exec_lock);
		goto out;
	}
	memcpy(ring, rsx->restore_buf, restore);
	/*
	 * Every push is checked before anything of the submit counts: the binds and primitives
	 * they leave go into a copy of the channel's validator state, kept only if all pass.
	 */
	vs = ch->vstate;
	{
		u32 *dst = ring + restore / 4;

		for (i = 0; i < req->nr_push; i++) {
			u64 len = push[i].length & ~(u64)NOUVEAU_GEM_PUSHBUF_NO_PREFETCH;
			struct ps3rsx_bo *pb = to_bo(objs[push[i].bo_index]);

			ps3rsx_gart_read(rsx, dst, pb->offset + push[i].offset, len);
			apply_relocs(relocs, req->nr_relocs, bos, objs, req->nr_buffers,
				     push[i].bo_index, push[i].offset, dst, len);
			ret = ps3rsx_validate(&vs, dst, len / 4);
			if (ret) {
				/* the push dump (test builds), before an END can reuse the ring space */
				ps3rsx_pd_submit(rsx, ch, false, i, push, req->nr_push, bos, objs,
						 req->nr_buffers, ring, restore / 4, ring + restore / 4);
				ret = submit_rejected(rsx, ch);
				mutex_unlock(&rsx->exec_lock);
				goto out;
			}
			if (trace)
				print_hex_dump(KERN_INFO, "ps3rsx push: ", DUMP_PREFIX_OFFSET, 32, 4,
					       dst, min_t(u64, len, 4096), false);
			dst += len / 4;
		}
	}
	rsx->blame_chan = ch->id;	/* blame.h's record of this commit */
	rsx->blame_serial = ch->serial;
	fence = ps3rsx_ring_commit(rsx, ring_len, !dry_run);
	rsx->blame_chan = -1;
	rsx->blame_serial = 0;
	if (!IS_ERR(fence)) {
		u32 *dst = ring + restore / 4;

		/* the push dump (test builds): the words as they went to the ring */
		pds = ps3rsx_pd_submit(rsx, ch, true, 0, push, req->nr_push, bos, objs,
				       req->nr_buffers, ring, restore / 4, dst);
		/*
		 * The GPU now holds this channel's state, then what its pushes set: they go into
		 * the channel's validator state and shadow (from the ring, where they still are).
		 * Until another channel runs, the GPU's state is hw_shadow with this channel's
		 * taken over it (hw_shadow_sync), so the pushes aren't replayed into hw_shadow
		 * word by word too. After a switch hw_shadow takes the restore now (before the
		 * pushes go into the channel's shadow), the pushes at the next sync.
		 */
		if (rsx->hw_behind != ch->shadow)
			hw_shadow_sync(rsx);
		if (switched)
			ps3rsx_shadow_take_restored(ch->shadow, rsx->hw_shadow, rsx->restore_buf,
						    restore / 4);
		ch->vstate = vs;
		for (i = 0; i < req->nr_push; i++) {
			u64 len = push[i].length & ~(u64)NOUVEAU_GEM_PUSHBUF_NO_PREFETCH;

			ps3rsx_shadow_push(ch->shadow, dst, len / 4);
			dst += len / 4;
		}
		rsx->hw_behind = ch->shadow;
		rsx->last_chan = ch->id;
		/* who to blame if the GPU raises an exception on this submit */
		rsx->last_pid = task_pid_nr(current);
		get_task_comm(rsx->last_comm, current);
		{	/* the crash record: this channel's last submits */
			struct ps3rsx_subhist *h = &ch->hist[ch->hist_n++ % PS3RSX_HIST];
			u32 *w = ring + restore / 4;

			h->seq = rsx->seqno;
			h->pid = rsx->last_pid;
			h->len = total;
			h->first = total ? w[0] : 0;
			h->last = total ? w[total / 4 - 1] : 0;
		}
		rsx->prim_owner = ch->vstate.prim_open ? ch : NULL;
		rsx->last_submit = jiffies;
		if (rsx->prim_owner) {
			rsx->prim_since = jiffies;	/* its owner continues it, or after 1 s ends */
		} else {
			wake_up_all(&rsx->prim_wq);
			if (READ_ONCE(rsx->vc.wait_prim))
				ps3rsx_vclear_kick(rsx, 0);
		}
	}
	mutex_unlock(&rsx->exec_lock);
	mutex_unlock(&pf->set_lock);	/* on the ring (or refused): the checks are done */
	set_held = false;
	if (IS_ERR(fence)) {
		ret = PTR_ERR(fence);
		fence = NULL;
		goto out;
	}

	/* attach the fence to every referenced BO except pure push sources (Mesa maps those
	 * at each buffer switch; their contents were copied) */
	ret = drm_gem_lock_reservations(objs, req->nr_buffers, &ticket);
	if (ret)
		goto out;
	for (i = 0; i < req->nr_buffers; i++) {
		bool push_src = false;
		u32 k;

		for (k = 0; k < req->nr_push; k++)
			push_src |= push[k].bo_index == i;
		if (push_src && !(bos[i].read_domains | bos[i].write_domains))
			continue;
		ret = dma_resv_reserve_fences(objs[i]->resv, 1);
		if (ret)
			break;
		dma_resv_add_fence(objs[i]->resv, fence, DMA_RESV_USAGE_WRITE);
	}
	drm_gem_unlock_reservations(objs, req->nr_buffers, &ticket);

	/* tell userspace where its BOs are (they never move) */
	for (i = 0; i < req->nr_buffers; i++) {
		struct ps3rsx_bo *bo = to_bo(objs[i]);

		bos[i].presumed.valid = 1;
		bos[i].presumed.domain = bo->domain;
		bos[i].presumed.offset = bo->offset;
	}
	if (copy_to_user(u64_to_user_ptr(req->buffers), bos, req->nr_buffers * sizeof(*bos)))
		ret = -EFAULT;
	if (!ret && sync) {
		/* ended by a wedge (-EIO), not by the GPU passing it: not done (wait.h) */
		long t = dma_fence_wait_timeout(fence, true, 30 * HZ);

		ret = ps3rsx_wait_result(t, t > 0 ? dma_fence_get_status(fence) : 0, false);
	}
out:
	if (set_held)
		mutex_unlock(&pf->set_lock);
	if (held)
		ps3rsx_console_unhold(rsx);
	/* the push dump (test builds): what the pushes fetched, once the GPU has passed them */
	ps3rsx_pd_contents(rsx, pds, fence, objs, req->nr_buffers);
	chan_put(ch);			/* exec_lock isn't held here */
	if (fence)
		dma_fence_put(fence);
	if (objs)
		for (i = 0; i < req->nr_buffers; i++)
			if (objs[i])
				drm_gem_object_put(objs[i]);
	kfree(objs);
	kfree(relocs);
	kfree(bos);
	kfree(push);
	return ret;
}

/* An error one of the BO's write fences ended with (a wedge: -EIO), or 0 */
static int resv_write_error(struct dma_resv *resv)
{
	struct dma_resv_iter cursor;
	struct dma_fence *f;
	int err = 0;

	dma_resv_iter_begin(&cursor, resv, DMA_RESV_USAGE_WRITE);
	dma_resv_for_each_fence_unlocked(&cursor, f) {
		int s = dma_fence_get_status(f);

		if (dma_resv_iter_is_restarted(&cursor))
			err = 0;
		if (s < 0)
			err = s;
	}
	dma_resv_iter_end(&cursor);
	return err;
}

int ps3rsx_ioctl_gem_cpu_prep(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_nouveau_gem_cpu_prep *req = data;
	struct drm_gem_object *obj = drm_gem_object_lookup(file, req->handle);
	long t;
	int ret;

	if (!obj)
		return -ENOENT;
	if (req->flags & NOUVEAU_GEM_CPU_PREP_NOWAIT)
		t = dma_resv_test_signaled(obj->resv, DMA_RESV_USAGE_WRITE) ? 1 : 0;
	else
		t = dma_resv_wait_timeout(obj->resv, DMA_RESV_USAGE_WRITE, true, 30 * HZ);
	/* the GPU wedged under the BO's last write: not done (wait.h) */
	ret = ps3rsx_wait_result(t, t > 0 ? resv_write_error(obj->resv) : 0, true);
	drm_gem_object_put(obj);
	return ret;
}

int ps3rsx_ioctl_gem_cpu_fini(struct drm_device *dev, void *data, struct drm_file *file)
{
	return 0;
}
