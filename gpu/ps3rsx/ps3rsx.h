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
#include <asm/ps3gpu.h>

#define PS3RSX_CHIPSET		0x47		/* NV47-like: Mesa picks the 0x4097 3D class */
#define PS3RSX_PCI_VENDOR	0x104d		/* Sony: what the patched Mesa checks for */
/* Private GETPARAM: the patched Mesa queries it; channels are refused until it did, so an
 * unpatched Mesa fails at init instead of submitting streams with handles the RSX lacks. */
#define PS3RSX_GETPARAM_PS3_ABI	0x1000
#define PS3RSX_ABI_VERSION	1

#define CPU_VRAM_LIMIT		0xe000000ULL	/* ps3fb's VRAM mapping size: CPU access above it is unproven */

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

/* Per open file */
struct ps3rsx_file {
	struct mutex lock;
	bool abi_ok;			/* PS3_ABI was queried */
	struct list_head objects;	/* NVIF objects (device, engine objects) */
};

int ps3rsx_ioctl_getparam(struct drm_device *dev, void *data, struct drm_file *file);
int ps3rsx_ioctl_nvif(struct drm_file *file, void __user *user, u32 size);
void ps3rsx_file_objects_free(struct ps3rsx_file *pf);

#endif
