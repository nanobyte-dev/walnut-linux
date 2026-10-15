// SPDX-License-Identifier: GPL-2.0
/*
 * Buffer objects. No TTM: every BO is pinned at one GPU offset for life, so the offsets
 * userspace sees ("presumed") never change.
 *
 *   VRAM, CPU-visible  ps3fb's reservation .. 224 MiB   offset = VRAM offset (ctxdma 0xfeed0000)
 *   VRAM, GPU-only     224 MiB .. memory_size (254)     no CPU mapping (unproven, could BUG())
 *   GART               main memory in 1 MiB IO pages, on demand from a CMA area or a boot
 *                      reservation (gart.c); offset = IO address (ctxdma 0xfeed0001)
 */
#include <linux/mm.h>
#include <linux/slab.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_vma_manager.h>
#include <drm/nouveau_drm.h>
#include <asm/cell-regs.h>
#include <asm/ps3.h>

#include <drm/drm_debugfs.h>
#include <linux/seq_file.h>

#include "ps3rsx.h"

#define IOPTE_RW (CBE_IOPTE_PP_W | CBE_IOPTE_PP_R | CBE_IOPTE_M)

static const struct vm_operations_struct ps3rsx_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static void bo_free(struct drm_gem_object *obj)
{
	struct ps3rsx_bo *bo = to_bo(obj);
	struct ps3rsx_device *rsx = to_rsx(obj->dev);

	if (drm_mm_node_allocated(&bo->node)) {
		mutex_lock(&rsx->mm_lock);
		if (bo->kind == BO_GART)	/* fences up to the last submitted may still read it */
			ps3rsx_gart_put(rsx, bo->offset, bo->base.size, READ_ONCE(rsx->seqno));
		drm_mm_remove_node(&bo->node);
		mutex_unlock(&rsx->mm_lock);
	}
	drm_gem_object_release(obj);
	kfree(bo);
}

/* Physical frame number of the CPU mapping, or 0 if the BO has none */
static unsigned long bo_pfn(struct ps3rsx_bo *bo)
{
	struct ps3rsx_device *rsx = to_rsx(bo->base.dev);

	switch (bo->kind) {
	case BO_GART:
		return ps3rsx_gart_pfn(rsx, bo->offset);
	case BO_VRAM:
		return (rsx->info.ddr_lpar + bo->offset) >> PAGE_SHIFT;
	case BO_REPORTS:
		return rsx->ctx.reports_lpar >> PAGE_SHIFT;
	default:
		return 0;
	}
}

static int bo_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct ps3rsx_bo *bo = to_bo(obj);
	unsigned long pfn = bo_pfn(bo);
	unsigned long pgoff = vma->vm_pgoff - drm_vma_node_start(&obj->vma_node);
	unsigned long len = vma->vm_end - vma->vm_start;

	if (!pfn)
		return -EINVAL;		/* GPU-only VRAM */
	if ((pgoff << PAGE_SHIFT) + len > obj->size)
		return -EINVAL;
	vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);
	if (bo->kind == BO_GART) {
		/* ordinary RAM, kept coherent with the GPU by the IOMMU (CBE_IOPTE_M); the 1 MiB
		 * chunks behind it aren't contiguous: one remap per chunk */
		struct ps3rsx_device *rsx = to_rsx(obj->dev);
		u64 io = bo->offset + (pgoff << PAGE_SHIFT);
		unsigned long addr = vma->vm_start;
		int ret = 0;

		vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
		while (!ret && addr < vma->vm_end) {
			unsigned long n = min_t(unsigned long, vma->vm_end - addr,
						SZ_1M - io % SZ_1M);

			ret = remap_pfn_range(vma, addr, ps3rsx_gart_pfn(rsx, io), n,
					      vma->vm_page_prot);
			addr += n;
			io += n;
		}
		return ret;
	}
	vma->vm_page_prot = pgprot_noncached(vm_get_page_prot(vma->vm_flags));
	return io_remap_pfn_range(vma, vma->vm_start, pfn + pgoff, len, vma->vm_page_prot);
}

static const struct drm_gem_object_funcs ps3rsx_bo_funcs = {
	.free = bo_free,
	.mmap = bo_mmap,
	.vm_ops = &ps3rsx_vm_ops,
};

static struct ps3rsx_bo *bo_alloc(struct ps3rsx_device *rsx, enum bo_kind kind, size_t size)
{
	struct ps3rsx_bo *bo = kzalloc(sizeof(*bo), GFP_KERNEL);

	if (!bo)
		return ERR_PTR(-ENOMEM);
	bo->kind = kind;
	bo->base.funcs = &ps3rsx_bo_funcs;
	drm_gem_private_object_init(&rsx->drm, &bo->base, size);
	return bo;
}

/* Place a BO in one heap; returns 0 or -ENOSPC */
static int bo_place(struct ps3rsx_device *rsx, struct ps3rsx_bo *bo, struct drm_mm *mm,
		    u64 align)
{
	int ret;

	mutex_lock(&rsx->mm_lock);
	ret = drm_mm_insert_node_generic(mm, &bo->node, bo->base.size, align, 0,
					 DRM_MM_INSERT_BEST);
	mutex_unlock(&rsx->mm_lock);
	if (!ret)
		bo->offset = bo->node.start;
	return ret;
}

/* Allocate a BO. domain: NOUVEAU_GEM_DOMAIN_* (0 = VRAM|GART). */
struct ps3rsx_bo *ps3rsx_bo_new(struct ps3rsx_device *rsx, u32 domain, size_t size, u32 align)
{
	bool vram = !domain || (domain & NOUVEAU_GEM_DOMAIN_VRAM);
	bool gart = !domain || (domain & NOUVEAU_GEM_DOMAIN_GART);
	bool mappable = domain & NOUVEAU_GEM_DOMAIN_MAPPABLE;
	struct ps3rsx_bo *bo;
	int ret = -ENOSPC;

	size = PAGE_ALIGN(size);
	if (!size || size > rsx->vram_size)
		return ERR_PTR(-EINVAL);
	align = max_t(u32, align, PAGE_SIZE);
	if (!is_power_of_2(align))
		return ERR_PTR(-EINVAL);

	if (vram) {
		bo = bo_alloc(rsx, BO_VRAM, size);
		if (IS_ERR(bo))
			return bo;
		/* GPU-only VRAM first, unless the CPU will map it */
		if (!mappable)
			ret = bo_place(rsx, bo, &rsx->vram_gpu, align);
		if (ret)
			ret = bo_place(rsx, bo, &rsx->vram_cpu, align);
		/* games first: the swap device gives VRAM back (its pages go to the disk) */
		while (ret && ps3rsx_vswap_shrink(rsx, size))
			ret = bo_place(rsx, bo, &rsx->vram_cpu, align);
		if (!ret) {
			bo->domain = NOUVEAU_GEM_DOMAIN_VRAM;
			if (bo->offset >= CPU_VRAM_LIMIT)
				bo->kind = BO_VRAM_GPU_ONLY;
			return bo;
		}
		drm_gem_object_put(&bo->base);
	}
	if (gart) {
		bo = bo_alloc(rsx, BO_GART, size);
		if (IS_ERR(bo))
			return bo;
		ret = bo_place(rsx, bo, &rsx->gart, align);
		if (!ret) {
			/* memory behind the IO pages it takes (on demand from CMA) */
			mutex_lock(&rsx->mm_lock);
			ret = ps3rsx_gart_get(rsx, bo->offset, size);
			if (ret)
				drm_mm_remove_node(&bo->node);
			mutex_unlock(&rsx->mm_lock);
		}
		if (!ret) {
			bo->domain = NOUVEAU_GEM_DOMAIN_GART;
			ps3rsx_gart_clear(rsx, bo->offset, size);
			return bo;
		}
		drm_gem_object_put(&bo->base);
	}
	if (ret == -ENOSPC)
		pr_warn_ratelimited("ps3rsx: out of %s for a %zu KiB BO\n",
				    gart ? "GART" : "VRAM", size >> 10);
	return ERR_PTR(ret);
}

/* The channel notifier: a BO that maps the context's reports area (labels + reports) */
struct ps3rsx_bo *ps3rsx_bo_new_reports(struct ps3rsx_device *rsx)
{
	struct ps3rsx_bo *bo = bo_alloc(rsx, BO_REPORTS, rsx->ctx.reports_size);

	if (!IS_ERR(bo))
		bo->domain = NOUVEAU_GEM_DOMAIN_GART;
	return bo;
}

static void bo_fill_info(struct ps3rsx_bo *bo, struct drm_nouveau_gem_info *info)
{
	info->domain = bo->domain;
	info->size = bo->base.size;
	info->offset = bo->offset;
	info->map_handle = bo_pfn(bo) ? drm_vma_node_offset_addr(&bo->base.vma_node) : 0;
	info->tile_mode = 0;
	info->tile_flags = 0;
}

int ps3rsx_bo_handle(struct drm_file *file, struct ps3rsx_bo *bo,
		     struct drm_nouveau_gem_info *info)
{
	int ret;

	if (bo_pfn(bo)) {
		ret = drm_gem_create_mmap_offset(&bo->base);
		if (ret)
			return ret;
	}
	ret = drm_gem_handle_create(file, &bo->base, &info->handle);
	if (!ret)
		bo_fill_info(bo, info);
	return ret;
}

int ps3rsx_ioctl_gem_new(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_nouveau_gem_new *req = data;
	struct ps3rsx_bo *bo;
	int ret;

	/* tile_mode/tile_flags (NONCONTIG etc.) are ignored: no tiling yet, BOs are contiguous */
	bo = ps3rsx_bo_new(to_rsx(dev), req->info.domain, req->info.size, req->align);
	if (IS_ERR(bo))
		return PTR_ERR(bo);
	ret = ps3rsx_bo_handle(file, bo, &req->info);
	drm_gem_object_put(&bo->base);	/* the handle holds the reference */
	return ret;
}

int ps3rsx_ioctl_gem_info(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_nouveau_gem_info *info = data;
	struct drm_gem_object *obj = drm_gem_object_lookup(file, info->handle);

	if (!obj)
		return -ENOENT;
	bo_fill_info(to_bo(obj), info);
	drm_gem_object_put(obj);
	return 0;
}

/* --- heaps and the GART's IO mapping ----------------------------------------------------- */

/* debugfs dri/N/ps3rsx_mm: used/total and buffer count per heap (finding leaks) */
static void mm_usage(struct seq_file *m, const char *name, struct drm_mm *mm, u64 total)
{
	struct drm_mm_node *node;
	u64 used = 0;
	unsigned int n = 0;

	drm_mm_for_each_node(node, mm) {
		used += node->size;
		n++;
	}
	seq_printf(m, "%-9s %7llu KiB used of %7llu KiB in %u buffers\n", name, used >> 10,
		   total >> 10, n);
}

/* per open file: GEM handles and the bytes they reference (who holds the buffers) */
static void file_usage(struct seq_file *m, struct ps3rsx_device *rsx)
{
	struct drm_device *dev = &rsx->drm;
	struct drm_file *file;

	mutex_lock(&dev->filelist_mutex);
	list_for_each_entry(file, &dev->filelist, lhead) {
		struct drm_gem_object *obj;
		u64 vram = 0, gart = 0;
		unsigned int n = 0, i, nsz = 0;
		struct { size_t size; unsigned int count; } sz[8] = {};
		struct task_struct *task;
		char comm[TASK_COMM_LEN] = "?";
		int id;

		rcu_read_lock();
		task = pid_task(rcu_dereference(file->pid), PIDTYPE_TGID);
		if (task)
			get_task_comm(comm, task);
		rcu_read_unlock();
		spin_lock(&file->table_lock);
		idr_for_each_entry(&file->object_idr, obj, id) {
			n++;
			/* the most common sizes above 64 KiB, to tell buffer kinds apart */
			if (obj->size > SZ_64K) {
				for (i = 0; i < nsz && sz[i].size != obj->size; i++)
					;
				if (i < nsz)
					sz[i].count++;
				else if (nsz < ARRAY_SIZE(sz))
					sz[nsz++] = (typeof(sz[0])){ obj->size, 1 };
			}
			if (to_bo(obj)->domain == NOUVEAU_GEM_DOMAIN_GART)
				gart += obj->size;
			else
				vram += obj->size;
		}
		spin_unlock(&file->table_lock);
		seq_printf(m, "%-16s %5u handles, VRAM %7llu KiB, GART %6llu KiB\n", comm, n,
			   vram >> 10, gart >> 10);
		for (i = 0; i < nsz; i++)
			seq_printf(m, "    %5u x %6zu KiB\n", sz[i].count, sz[i].size >> 10);
	}
	mutex_unlock(&dev->filelist_mutex);
}

static int mm_show(struct seq_file *m, void *unused)
{
	struct drm_debugfs_entry *entry = m->private;
	struct ps3rsx_device *rsx = to_rsx(entry->dev);

	mutex_lock(&rsx->mm_lock);
	mm_usage(m, "VRAM cpu", &rsx->vram_cpu, CPU_VRAM_LIMIT - rsx->info.reserved);
	mm_usage(m, "VRAM gpu", &rsx->vram_gpu, rsx->info.memory_size - CPU_VRAM_LIMIT);
	mm_usage(m, "GART", &rsx->gart, rsx->gart_size);
	seq_printf(m, "GART backed by memory: %llu KiB (%s)\n", rsx->gart_backed >> 10,
		   rsx->gart_dynamic ? "on demand" : "boot reservation");
	ps3rsx_vswap_show(m, rsx);
	mutex_unlock(&rsx->mm_lock);
	/* display heads as lv1 reports them to our context (driver info head[n] at 0x10b8 + 64n) */
	for (int h = 0; h < 2; h++) {
		void __iomem *hd = rsx->dinfo + 0x10b8 + 64 * h;

		seq_printf(m, "head %d: status %08x offset %08x\n", h, in_be32(hd + 8),
			   in_be32(hd + 12));
	}
	seq_printf(m, "GPU owned: %d, master active: %d, masters open: %d, console pending: %d\n",
		   rsx->gpu_owned, rsx->master_active, rsx->masters_open, rsx->console_pending);
	file_usage(m, rsx);
	return 0;
}

int ps3rsx_mem_init(struct ps3rsx_device *rsx)
{
	u64 cpu_end = min_t(u64, CPU_VRAM_LIMIT, rsx->info.memory_size);
	int ret;

	mutex_init(&rsx->mm_lock);
	drm_mm_init(&rsx->vram_cpu, rsx->info.reserved, cpu_end - rsx->info.reserved);
	drm_mm_init(&rsx->vram_gpu, cpu_end, rsx->info.memory_size - cpu_end);
	drm_debugfs_add_file(&rsx->drm, "ps3rsx_mm", mm_show, NULL);

	ret = ps3rsx_gart_init(rsx);
	if (ret) {
		drm_mm_takedown(&rsx->vram_gpu);
		drm_mm_takedown(&rsx->vram_cpu);
		return ret;
	}
	/* the first RING_RESERVE bytes hold the reset program and the ring (exec.c) */
	drm_mm_init(&rsx->gart, RING_RESERVE, rsx->gart_size - RING_RESERVE);
	return 0;
}

void ps3rsx_mem_fini(struct ps3rsx_device *rsx)
{
	if (drm_mm_initialized(&rsx->gart))
		drm_mm_takedown(&rsx->gart);
	ps3rsx_gart_fini(rsx);
	drm_mm_takedown(&rsx->vram_gpu);
	drm_mm_takedown(&rsx->vram_cpu);
}
