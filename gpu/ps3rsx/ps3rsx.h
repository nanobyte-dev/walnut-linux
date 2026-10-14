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
	u64 vram_size;			/* VRAM we manage: from info.reserved up to memory_size */
	u64 gart_size;
};

#define to_rsx(d) container_of(d, struct ps3rsx_device, drm)

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
