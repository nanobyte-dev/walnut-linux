// SPDX-License-Identifier: GPL-2.0
/*
 * ps3rsx: DRM driver for the PS3 RSX speaking nouveau's ABI. This file: device registration,
 * file lifetime and the ioctl table. See ps3rsx.h and docs/DRIVER-PLAN.md.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/nouveau_drm.h>
#include <asm/ps3.h>

#include <asm/ps3av.h>

#include "ps3rsx.h"

static int ps3rsx_open(struct drm_device *dev, struct drm_file *file)
{
	struct ps3rsx_file *pf = kzalloc(sizeof(*pf), GFP_KERNEL);

	if (!pf)
		return -ENOMEM;
	mutex_init(&pf->lock);
	INIT_LIST_HEAD(&pf->objects);
	INIT_LIST_HEAD(&pf->chans);
	file->driver_priv = pf;
	return 0;
}

static void ps3rsx_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct ps3rsx_file *pf = file->driver_priv;

	ps3rsx_file_chans_free(to_rsx(dev), pf);
	ps3rsx_file_objects_free(pf);
	ps3rsx_console_file_closed(to_rsx(dev), pf);
	kfree(pf);
}

static const struct drm_ioctl_desc ps3rsx_ioctls[] = {
	DRM_IOCTL_DEF_DRV(NOUVEAU_GETPARAM, ps3rsx_ioctl_getparam, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_NEW, ps3rsx_ioctl_gem_new, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_INFO, ps3rsx_ioctl_gem_info, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_ALLOC, ps3rsx_ioctl_channel_alloc, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_FREE, ps3rsx_ioctl_channel_free, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GROBJ_ALLOC, ps3rsx_ioctl_grobj_alloc, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_NOTIFIEROBJ_ALLOC, ps3rsx_ioctl_notifierobj_alloc, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GPUOBJ_FREE, ps3rsx_ioctl_gpuobj_free, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_PUSHBUF, ps3rsx_ioctl_gem_pushbuf, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_PREP, ps3rsx_ioctl_gem_cpu_prep, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_FINI, ps3rsx_ioctl_gem_cpu_fini, DRM_RENDER_ALLOW),
};

static long ps3rsx_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct drm_file *file = filp->private_data;

	if (_IOC_TYPE(cmd) == DRM_IOCTL_BASE &&
	    _IOC_NR(cmd) == DRM_COMMAND_BASE + DRM_NOUVEAU_NVIF)
		return ps3rsx_ioctl_nvif(file, (void __user *)arg, _IOC_SIZE(cmd));
	return drm_ioctl(filp, cmd, arg);
}

static const struct file_operations ps3rsx_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = ps3rsx_unlocked_ioctl,
	.mmap = drm_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.llseek = noop_llseek,
	.fop_flags = FOP_UNSIGNED_OFFSET,
};

static const struct drm_driver ps3rsx_driver = {
	.driver_features = DRIVER_GEM | DRIVER_RENDER | DRIVER_MODESET | DRIVER_ATOMIC,
	.dumb_create = ps3rsx_dumb_create,
	.open = ps3rsx_open,
	.postclose = ps3rsx_postclose,
	.master_set = ps3rsx_master_set,
	.master_drop = ps3rsx_master_drop,
	.ioctls = ps3rsx_ioctls,
	.num_ioctls = ARRAY_SIZE(ps3rsx_ioctls),
	.fops = &ps3rsx_fops,
	/* Mesa matches the driver name and needs at least 1.3.1 */
	.name = "nouveau",
	.desc = "PS3 RSX (nouveau ABI on lv1)",
	.major = 1,
	.minor = 3,
	.patchlevel = 1,
};

/*
 * The frame the display scans is the video mode's native size (1920x1080 for 1080p). In ps3fb's
 * underscan modes (Petitboot's default, video=ps3fb:mode:5) its xres x yres is only the console's
 * picture inset in that frame (1688x964), and X's frame of that size never showed (2026-10-23,
 * the first public install): X gets the whole frame, as in the full-screen modes (mode:133).
 */
static void native_frame(struct ps3rsx_device *rsx)
{
	int mode = ps3av_get_mode();
	u32 w, h;

	if (mode <= 0 || ps3av_video_mode2res(mode, &w, &h) || w * 4 != rsx->info.line_length)
		return;
	if (w != rsx->info.xres || h != rsx->info.yres)
		drm_info(&rsx->drm, "console %ux%u inset (underscan): the display gets the whole %ux%u frame\n",
			 rsx->info.xres, rsx->info.yres, w, h);
	rsx->info.xres = w;
	rsx->info.yres = h;
}

static int ps3rsx_probe(struct platform_device *pdev)
{
	struct ps3rsx_device *rsx;
	int ret;

	rsx = devm_drm_dev_alloc(&pdev->dev, &ps3rsx_driver, struct ps3rsx_device, drm);
	if (IS_ERR(rsx))
		return PTR_ERR(rsx);
	ret = ps3fb_gpu_get_info(&rsx->info);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "ps3fb GPU info\n");
	native_frame(rsx);
	rsx->vram_size = rsx->info.memory_size - rsx->info.reserved;
	platform_set_drvdata(pdev, rsx);

	/* our context: allocated by ps3fb on first use and kept until it shuts down */
	ret = ps3fb_gpu_ctx_get(0x820, &rsx->ctx);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "no lv1 GPU context\n");
	mutex_init(&rsx->chan_lock);
	ret = ps3rsx_mem_init(rsx);
	if (ret)
		return ret;
	ret = ps3rsx_exec_init(rsx);
	if (ret) {
		ps3rsx_mem_fini(rsx);
		return ret;
	}
	ret = ps3rsx_vswap_init(rsx);
	if (ret)
		drm_warn(&rsx->drm, "no VRAM swap device: %d\n", ret);
	ret = ps3rsx_kms_init(rsx);
	if (ret) {
		ps3rsx_vswap_fini(rsx);
		ps3rsx_exec_fini(rsx);
		ps3rsx_mem_fini(rsx);
		return ret;
	}

	ret = drm_dev_register(&rsx->drm, 0);
	if (ret) {
		ps3rsx_kms_fini(rsx);
		ps3rsx_vswap_fini(rsx);
		ps3rsx_exec_fini(rsx);
		ps3rsx_mem_fini(rsx);
		return ret;
	}
	ps3rsx_console_init(rsx);	/* the console gets the GPU back until X or a client needs it */
	drm_info(&rsx->drm, "VRAM %llu MiB (from %#llx), GART %llu MiB (%s), display %ux%u pitch %u\n",
		 rsx->vram_size >> 20, rsx->info.reserved, rsx->gart_size >> 20,
		 rsx->gart_dynamic ? "on demand" : "reserved",
		 rsx->info.xres, rsx->info.yres, rsx->info.line_length);
	return 0;
}

static void ps3rsx_remove(struct platform_device *pdev)
{
	struct ps3rsx_device *rsx = platform_get_drvdata(pdev);

	drm_dev_unregister(&rsx->drm);
	drm_atomic_helper_shutdown(&rsx->drm);	/* flips back to ps3fb's frame */
	ps3rsx_kms_fini(rsx);
	ps3rsx_console_fini(rsx);	/* own the GPU again for the drain and release */
	ps3rsx_vswap_fini(rsx);
	ps3rsx_exec_fini(rsx);
	ps3rsx_mem_fini(rsx);
}

static struct platform_driver ps3rsx_platform_driver = {
	.probe = ps3rsx_probe,
	.remove = ps3rsx_remove,
	.driver = {
		.name = "ps3rsx",
	},
};

static struct platform_device *ps3rsx_pdev;

static int __init ps3rsx_init(void)
{
	int ret;

	ps3rsx_validate_init();
	ps3rsx_shadow_init();
	ret = platform_driver_register(&ps3rsx_platform_driver);
	if (ret)
		return ret;
	/* No device tree node: libdrm identifies the device by MODALIAS (platform:ps3rsx) */
	ps3rsx_pdev = platform_device_register_simple("ps3rsx", -1, NULL, 0);
	if (IS_ERR(ps3rsx_pdev)) {
		platform_driver_unregister(&ps3rsx_platform_driver);
		return PTR_ERR(ps3rsx_pdev);
	}
	return 0;
}

static void __exit ps3rsx_exit(void)
{
	platform_device_unregister(ps3rsx_pdev);
	platform_driver_unregister(&ps3rsx_platform_driver);
}

module_init(ps3rsx_init);
module_exit(ps3rsx_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PS3 RSX DRM driver (nouveau ABI)");
