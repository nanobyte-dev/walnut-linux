// SPDX-License-Identifier: GPL-2.0
/*
 * Channels and submission (nouveau ABI): CHANNEL_ALLOC/FREE, NOTIFIEROBJ_ALLOC, GROBJ_ALLOC,
 * GPUOBJ_FREE, GEM_PUSHBUF, GEM_CPU_PREP/FINI. After drivers/gpu/drm/nouveau/nouveau_abi16.c
 * and nouveau_gem.c, but pushes are copied into the kernel's ring and validated there instead
 * of being CALLed in user memory.
 */
#include <linux/dma-fence.h>
#include <linux/dma-resv.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <drm/drm_gem.h>
#include <drm/nouveau_drm.h>

#include "ps3rsx.h"

static bool ctxsw = true;
module_param(ctxsw, bool, 0644);
MODULE_PARM_DESC(ctxsw, "Restore a channel's GPU state when switching between channels");

static bool trace;
module_param(trace, bool, 0644);
MODULE_PARM_DESC(trace, "Dump every submitted push to the kernel log (small tests only: "
		 "a GL program's worth through netconsole has hung the console)");

static bool dry_run;
module_param_named(dry_run, dry_run, bool, 0644);
MODULE_PARM_DESC(dry_run, "Validate submissions but send nothing to the GPU");

/* All channels share one reports area; each gets its own fence label and query slice */
#define FENCE_LABEL(id)		(0x20 + 32 * (id))	/* Mesa asks for 32 bytes */
#define QUERY_SLICE(id)		(0x1400 + 0x1000 * (id))
#define NTFY_OFFSET		0x1000

struct ps3rsx_chan *ps3rsx_chan_find(struct ps3rsx_file *pf, u32 id)
{
	struct ps3rsx_chan *ch;

	list_for_each_entry(ch, &pf->chans, head)
		if (ch->id == id)
			return ch;
	return NULL;
}

/* The owner of an open primitive didn't continue it: end it, and mark that channel broken
 * (its next push would continue a primitive that no longer exists). exec_lock held. */
static int close_stale_primitive(struct ps3rsx_device *rsx)
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
	f = ps3rsx_ring_commit(rsx, 8, true);
	if (IS_ERR(f))
		return PTR_ERR(f);
	dma_fence_put(f);
	pr_warn("ps3rsx: channel %u left a primitive open for 1 s: closed it\n", owner->id);
	owner->broken = true;
	owner->vstate.prim_open = false;
	rsx->prim_owner = NULL;
	wake_up_all(&rsx->prim_wq);
	return 0;
}

/* Bring hw_shadow up to date with the last channel's pushes. exec_lock held. */
static void hw_shadow_sync(struct ps3rsx_device *rsx)
{
	if (rsx->hw_behind) {
		ps3rsx_shadow_take(rsx->hw_behind, rsx->hw_shadow);
		rsx->hw_behind = NULL;
	}
}

static void chan_free(struct ps3rsx_device *rsx, struct ps3rsx_chan *ch)
{
	mutex_lock(&rsx->chan_lock);
	clear_bit(ch->id, rsx->chan_ids);
	mutex_unlock(&rsx->chan_lock);
	mutex_lock(&rsx->exec_lock);
	if (rsx->hw_behind == ch->shadow)
		hw_shadow_sync(rsx);
	if (rsx->last_chan == ch->id)
		rsx->last_chan = -1;
	if (rsx->prim_owner == ch && close_stale_primitive(rsx)) {
		rsx->prim_owner = NULL;		/* couldn't end it (wedged): don't block the others */
		wake_up_all(&rsx->prim_wq);
	}
	mutex_unlock(&rsx->exec_lock);
	list_del(&ch->head);
	ps3rsx_shadow_free(ch->shadow);
	kfree(ch);
}

void ps3rsx_file_chans_free(struct ps3rsx_device *rsx, struct ps3rsx_file *pf)
{
	struct ps3rsx_chan *ch, *tmp;

	list_for_each_entry_safe(ch, tmp, &pf->chans, head)
		chan_free(rsx, ch);
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
	if (id < PS3RSX_MAX_CHANNELS)
		set_bit(id, rsx->chan_ids);
	mutex_unlock(&rsx->chan_lock);
	if (id >= PS3RSX_MAX_CHANNELS) {
		ps3rsx_shadow_free(ch->shadow);
		kfree(ch);
		return -ENOSPC;
	}
	ch->id = id;

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
		chan_free(to_rsx(dev), ch);
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


#define MAX_SUBMIT	(512 * 1024 + 4096)

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
		if (r->reloc_bo_offset < push_off || r->reloc_bo_offset + 4 > push_off + len)
			continue;
		at = (r->reloc_bo_offset - push_off) / 4;
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
	bool held = false;
	struct ps3rsx_chan *ch;
	bool sync = req->vram_available & NOUVEAU_GEM_PUSHBUF_SYNC;
	u64 total = 0;
	u32 i, *ring, restore;
	int ret;

	req->suffix0 = 0;		/* pushes are copied: no RETURN needed after them */
	req->suffix1 = 0;
	req->vram_available = rsx->vram_size;
	req->gart_available = rsx->gart_size - RING_RESERVE;

	mutex_lock(&pf->lock);
	ch = ps3rsx_chan_find(pf, req->channel);
	mutex_unlock(&pf->lock);
	if (!ch)
		return -ENOENT;
	if (!req->nr_push)
		return 0;		/* Mesa's first call only asks for the suffixes */
	if (READ_ONCE(rsx->wedged))
		return -EIO;
	if (req->nr_push > NOUVEAU_GEM_MAX_PUSH || req->nr_buffers > NOUVEAU_GEM_MAX_BUFFERS ||
	    req->nr_relocs > NOUVEAU_GEM_MAX_RELOCS || !req->nr_buffers)
		return -EINVAL;

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

		if (push[i].bo_index >= req->nr_buffers || (len & 3) || (push[i].offset & 3)) {
			ret = -EINVAL;
			goto out;
		}
		pb = to_bo(objs[push[i].bo_index]);
		if (pb->kind != BO_GART || push[i].offset + len > pb->base.size) {
			ret = -EINVAL;	/* pushes must come from GART BOs we can read */
			goto out;
		}
		total += len;
	}
	if (total > MAX_SUBMIT) {
		ret = -E2BIG;
		goto out;
	}

	ret = ps3rsx_console_hold(rsx, ch, pf);	/* may wait while a text console has the GPU */
	if (ret)
		goto out;
	held = true;
	mutex_lock(&rsx->exec_lock);
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

		mutex_unlock(&rsx->exec_lock);
		t = wait_event_interruptible_timeout(rsx->prim_wq, !READ_ONCE(rsx->prim_owner), HZ);
		mutex_lock(&rsx->exec_lock);
		if (t < 0) {
			mutex_unlock(&rsx->exec_lock);
			ret = t;
			goto out;
		}
		if (!t && rsx->prim_owner && rsx->prim_owner != ch) {
			ret = close_stale_primitive(rsx);
			if (ret) {
				mutex_unlock(&rsx->exec_lock);
				goto out;
			}
		}
	}
	/* another channel ran last: put this channel's state back first */
	restore = 0;
	if (ctxsw && rsx->last_chan != ch->id) {
		hw_shadow_sync(rsx);
		ret = ps3rsx_shadow_restore(ch->shadow, rsx->hw_shadow, rsx->restore_buf,
					    RESTORE_MAX_WORDS);
		if (ret < 0) {
			mutex_unlock(&rsx->exec_lock);
			goto out;
		}
		restore = ret * 4;
	}
	ring = ps3rsx_ring_begin(rsx, restore + total, &ret);
	if (!ring) {
		mutex_unlock(&rsx->exec_lock);
		goto out;
	}
	memcpy(ring, rsx->restore_buf, restore);
	{
		u32 *dst = ring + restore / 4;

		for (i = 0; i < req->nr_push; i++) {
			u64 len = push[i].length & ~(u64)NOUVEAU_GEM_PUSHBUF_NO_PREFETCH;
			struct ps3rsx_bo *pb = to_bo(objs[push[i].bo_index]);

			ps3rsx_gart_read(rsx, dst, pb->offset + push[i].offset, len);
			apply_relocs(relocs, req->nr_relocs, bos, objs, req->nr_buffers,
				     push[i].bo_index, push[i].offset, dst, len);
			ret = ps3rsx_validate(&ch->vstate, ch->shadow, dst, len / 4);
			if (ret) {
				mutex_unlock(&rsx->exec_lock);
				goto out;
			}
			if (trace)
				print_hex_dump(KERN_INFO, "ps3rsx push: ", DUMP_PREFIX_OFFSET, 32, 4,
					       dst, min_t(u64, len, 4096), false);
			dst += len / 4;
		}
	}
	fence = ps3rsx_ring_commit(rsx, restore + total, !dry_run);
	if (!IS_ERR(fence)) {
		/*
		 * The GPU now holds this channel's state, then what its pushes set; the validator
		 * already replayed the pushes into the channel's shadow. Until another channel
		 * runs, the GPU's state is hw_shadow with this channel's taken over it
		 * (hw_shadow_sync), so the pushes aren't replayed into hw_shadow word by word too.
		 */
		if (rsx->hw_behind != ch->shadow)
			hw_shadow_sync(rsx);
		if (ctxsw && rsx->last_chan != ch->id)
			ps3rsx_shadow_take(ch->shadow, rsx->hw_shadow);
		rsx->hw_behind = ch->shadow;
		rsx->last_chan = ch->id;
		/* who to blame if the GPU raises an exception on this submit */
		rsx->last_pid = task_pid_nr(current);
		get_task_comm(rsx->last_comm, current);
		rsx->prim_owner = ch->vstate.prim_open ? ch : NULL;
		if (!rsx->prim_owner)
			wake_up_all(&rsx->prim_wq);
	}
	mutex_unlock(&rsx->exec_lock);
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
	if (!ret && sync && dma_fence_wait_timeout(fence, true, 30 * HZ) <= 0)
		ret = -EBUSY;
out:
	if (held)
		ps3rsx_console_unhold(rsx);
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

int ps3rsx_ioctl_gem_cpu_prep(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_nouveau_gem_cpu_prep *req = data;
	struct drm_gem_object *obj = drm_gem_object_lookup(file, req->handle);
	long t;

	if (!obj)
		return -ENOENT;
	if (req->flags & NOUVEAU_GEM_CPU_PREP_NOWAIT)
		t = dma_resv_test_signaled(obj->resv, DMA_RESV_USAGE_WRITE) ? 1 : 0;
	else
		t = dma_resv_wait_timeout(obj->resv, DMA_RESV_USAGE_WRITE, true, 30 * HZ);
	drm_gem_object_put(obj);
	if (t < 0)
		return t;
	return t ? 0 : -EBUSY;
}

int ps3rsx_ioctl_gem_cpu_fini(struct drm_device *dev, void *data, struct drm_file *file)
{
	return 0;
}
