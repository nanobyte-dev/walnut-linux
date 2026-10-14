/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ps3rsx: a DRM driver for the PS3's RSX on top of the lv1 hypervisor's GPU calls. It speaks
 * nouveau's userspace ABI (driver name "nouveau"), so Mesa's nv30 driver can run on it with a
 * small patch for the hypervisor's fixed object handles. Plan: docs/DRIVER-PLAN.md.
 */
#ifndef PS3RSX_H
#define PS3RSX_H

#include <linux/list.h>
#include <linux/mutex.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_mm.h>
#include <linux/dma-fence.h>
#include <linux/hrtimer.h>
#include <drm/drm.h>
#include <drm/nouveau_drm.h>
#include <asm/ps3gpu.h>

struct ps3rsx_vstate;
struct ps3rsx_file;
struct ps3rsx_chan;

/* Deprecated nouveau ioctls Mesa's winsys still uses, no longer in the kernel's uapi header
 * (layouts as in libdrm's nouveau_drm.h) */
struct drm_nouveau_grobj_alloc {
	__s32 channel;
	__u32 handle;
	__s32 class;
};
#define DRM_IOCTL_NOUVEAU_GROBJ_ALLOC \
	DRM_IOW(DRM_COMMAND_BASE + DRM_NOUVEAU_GROBJ_ALLOC, struct drm_nouveau_grobj_alloc)
#define DRM_IOCTL_NOUVEAU_NOTIFIEROBJ_ALLOC \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_NOTIFIEROBJ_ALLOC, struct drm_nouveau_notifierobj_alloc)
#define DRM_IOCTL_NOUVEAU_GPUOBJ_FREE \
	DRM_IOW(DRM_COMMAND_BASE + DRM_NOUVEAU_GPUOBJ_FREE, struct drm_nouveau_gpuobj_free)

#define PS3RSX_CHIPSET		0x47		/* NV47-like: Mesa picks the 0x4097 3D class */
#define PS3RSX_PCI_VENDOR	0x104d		/* Sony: what the patched Mesa checks for */
/* Private GETPARAM: the patched Mesa queries it; channels are refused until it did, so an
 * unpatched Mesa fails at init instead of submitting streams with handles the RSX lacks. */
#define PS3RSX_GETPARAM_PS3_ABI	0x1000
#define PS3RSX_ABI_VERSION	1

#define CPU_VRAM_LIMIT		0xe000000ULL	/* ps3fb's VRAM mapping size: CPU access above it is unproven */

/* GART IO layout: reset program, then the ring, then BOs (exec.c) */
#define RING_START		0x1000
#define RING_END		0x200000
#define RING_RESERVE		RING_END
#define PS3RSX_MAX_CHANNELS	14	/* one 4 KiB query slice each in the 64 KiB reports area */

struct ps3rsx_fence {
	struct dma_fence base;
	struct list_head head;
};

struct ps3rsx_device {
	struct drm_device drm;
	struct ps3fb_gpu_info info;
	struct ps3fb_gpu_ctx ctx;	/* our lv1 context (ps3fb keeps it across module reloads) */
	u64 vram_size;			/* VRAM we manage: from info.reserved up to memory_size */
	u64 gart_size;

	struct mutex mm_lock;
	struct drm_mm vram_cpu;		/* VRAM the CPU may map (below CPU_VRAM_LIMIT) */
	struct drm_mm vram_gpu;		/* VRAM only the GPU touches */
	struct drm_mm gart;		/* IO addresses of the GART reservation */
	void *gart_cpu;			/* kernel address of the GART reservation */
	u64 gart_mapped;		/* bytes of it iomapped into our context */

	/* execution (exec.c) */
	void __iomem *ctrl;		/* our context's FIFO control: put 0x40, get 0x44, ref 0x48 */
	struct ps3fb_gpu_hook hook;
	struct mutex exec_lock;		/* ring writes */
	u32 ring_put;			/* IO address the next command goes to */
	u32 seqno;			/* last SET_REFERENCE value submitted */
	u64 fence_context;
	spinlock_t fence_lock;
	struct list_head fences;	/* pending, in seqno order */
	struct hrtimer fence_timer;
	u32 last_ref;
	ktime_t last_progress;
	bool wedged;			/* a graph exception or a stall: nothing runs until reboot */
	bool trace;

	struct mutex chan_lock;
	DECLARE_BITMAP(chan_ids, PS3RSX_MAX_CHANNELS);
};

#define to_rsx(d) container_of(d, struct ps3rsx_device, drm)

enum bo_kind { BO_VRAM, BO_VRAM_GPU_ONLY, BO_GART, BO_REPORTS };

struct ps3rsx_bo {
	struct drm_gem_object base;
	enum bo_kind kind;
	u32 domain;			/* NOUVEAU_GEM_DOMAIN_VRAM or _GART */
	u64 offset;			/* GPU offset in its domain's ctxdma */
	struct drm_mm_node node;
};

#define to_bo(o) container_of(o, struct ps3rsx_bo, base)

struct drm_nouveau_gem_info;
int ps3rsx_mem_init(struct ps3rsx_device *rsx);
void ps3rsx_mem_fini(struct ps3rsx_device *rsx);
struct ps3rsx_bo *ps3rsx_bo_new(struct ps3rsx_device *rsx, u32 domain, size_t size, u32 align);
struct ps3rsx_bo *ps3rsx_bo_new_reports(struct ps3rsx_device *rsx);
int ps3rsx_bo_handle(struct drm_file *file, struct ps3rsx_bo *bo,
		     struct drm_nouveau_gem_info *info);
int ps3rsx_ioctl_gem_new(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_gem_info(struct drm_device *dev, void *data, struct drm_file *file);

int ps3rsx_exec_init(struct ps3rsx_device *rsx);
void ps3rsx_exec_fini(struct ps3rsx_device *rsx);
u32 *ps3rsx_ring_begin(struct ps3rsx_device *rsx, u32 len, int *err);
struct dma_fence *ps3rsx_ring_commit(struct ps3rsx_device *rsx, u32 len, bool kick);

bool ps3rsx_known_object(u32 handle);
int ps3rsx_validate(struct ps3rsx_vstate *vs, const u32 *w, u32 nwords);

struct ps3rsx_chan *ps3rsx_chan_find(struct ps3rsx_file *pf, u32 id);
void ps3rsx_file_chans_free(struct ps3rsx_device *rsx, struct ps3rsx_file *pf);
int ps3rsx_ioctl_channel_alloc(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_channel_free(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_notifierobj_alloc(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_grobj_alloc(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_gpuobj_free(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_gem_pushbuf(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_gem_cpu_prep(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_gem_cpu_fini(struct drm_device *dev, void *data, struct drm_file *file);

/* Per channel validator state: which object this channel bound on each subchannel */
struct ps3rsx_vstate {
	u32 bound[8];
};

struct ps3rsx_chan {
	struct list_head head;
	u32 id;
	struct ps3rsx_vstate vstate;
};

/* Per open file */
struct ps3rsx_file {
	struct mutex lock;
	bool abi_ok;			/* PS3_ABI was queried */
	struct list_head objects;	/* NVIF objects (device, engine objects) */
	struct list_head chans;
};

int ps3rsx_ioctl_getparam(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_nvif(struct drm_file *file, void __user *user, u32 size);
void ps3rsx_file_objects_free(struct ps3rsx_file *pf);

#endif
