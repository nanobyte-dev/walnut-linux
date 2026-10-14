// SPDX-License-Identifier: GPL-2.0
/*
 * nouveau ABI: GETPARAM and the NVIF subset Mesa's winsys uses (device info, engine objects),
 * after drivers/gpu/drm/nouveau/nouveau_abi16.c.
 */
#include <linux/slab.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>
#include <drm/nouveau_drm.h>
#include <asm/ps3.h>

#include "ps3rsx.h"
#include "nvif.h"

int ps3rsx_ioctl_getparam(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ps3rsx_device *rsx = to_rsx(dev);
	struct ps3rsx_file *pf = file->driver_priv;
	struct drm_nouveau_getparam *gp = data;

	switch (gp->param) {
	case NOUVEAU_GETPARAM_CHIPSET_ID:
		gp->value = PS3RSX_CHIPSET;
		break;
	case NOUVEAU_GETPARAM_PCI_VENDOR:
		gp->value = PS3RSX_PCI_VENDOR;
		break;
	case NOUVEAU_GETPARAM_PCI_DEVICE:
		gp->value = 0;
		break;
	case NOUVEAU_GETPARAM_BUS_TYPE:
		gp->value = 2;			/* PCIE */
		break;
	case NOUVEAU_GETPARAM_FB_SIZE:
		gp->value = rsx->vram_size;
		break;
	case NOUVEAU_GETPARAM_AGP_SIZE:
		gp->value = rsx->gart_size;
		break;
	case NOUVEAU_GETPARAM_PTIMER_TIME:
		gp->value = ktime_get_raw_ns();
		break;
	case PS3RSX_GETPARAM_PS3_ABI:
		mutex_lock(&pf->lock);
		pf->abi_ok = true;
		mutex_unlock(&pf->lock);
		gp->value = PS3RSX_ABI_VERSION;
		break;
	default:
		/* includes HAS_VMA_TILEMODE: Mesa then doesn't prefer zink */
		return -EINVAL;
	}
	return 0;
}

/* --- NVIF ------------------------------------------------------------------------------- */

enum obj_type { OBJ_DEVICE, OBJ_ENGINE };

struct ps3rsx_obj {
	struct list_head head;
	enum obj_type type;
	u64 object;			/* userspace's object id */
	u32 handle;			/* engine objects: the hypervisor handle */
	s32 oclass;
	u64 channel;
};

static struct ps3rsx_obj *obj_find(struct ps3rsx_file *pf, u64 object)
{
	struct ps3rsx_obj *obj;

	list_for_each_entry(obj, &pf->objects, head)
		if (obj->object == object)
			return obj;
	return NULL;
}

void ps3rsx_file_objects_free(struct ps3rsx_file *pf)
{
	struct ps3rsx_obj *obj, *tmp;

	list_for_each_entry_safe(obj, tmp, &pf->objects, head) {
		list_del(&obj->head);
		kfree(obj);
	}
}

static int nvif_new(struct ps3rsx_file *pf, struct nvif_ioctl_v0 *ioctl, u32 argc)
{
	struct nvif_ioctl_new_v0 *args = (void *)ioctl->data;
	struct ps3rsx_obj *obj;

	if (argc < sizeof(*args) || args->version != 0)
		return -EINVAL;
	if (obj_find(pf, args->object))
		return -EEXIST;

	if (ioctl->route == NVIF_IOCTL_V0_ROUTE_NVIF) {
		if (ioctl->object || args->oclass != NV_DEVICE)
			return -EINVAL;
		obj = kzalloc(sizeof(*obj), GFP_KERNEL);
		if (!obj)
			return -ENOMEM;
		obj->type = OBJ_DEVICE;
		obj->object = args->object;
		list_add(&obj->head, &pf->objects);
		return 0;
	}

	/* Engine objects on a channel (route 0xff, token = channel): no channels yet */
	pr_debug("ps3rsx: engine object %#x class %#x on channel %llu: no channels yet\n",
		 args->handle, args->oclass, ioctl->token);
	return -ENODEV;
}

static int nvif_del(struct ps3rsx_file *pf, struct nvif_ioctl_v0 *ioctl, u32 argc)
{
	struct ps3rsx_obj *obj;

	if (ioctl->route || argc)
		return -EINVAL;
	obj = obj_find(pf, ioctl->object);
	if (obj) {
		list_del(&obj->head);
		kfree(obj);
	}
	return 0;
}

static int nvif_mthd(struct ps3rsx_device *rsx, struct ps3rsx_file *pf,
		     struct nvif_ioctl_v0 *ioctl, u32 argc)
{
	struct nvif_ioctl_mthd_v0 *args = (void *)ioctl->data;
	struct nv_device_info_v0 *info;
	struct ps3rsx_obj *obj;

	if (ioctl->route || argc < sizeof(*args))
		return -EINVAL;
	argc -= sizeof(*args);
	obj = obj_find(pf, ioctl->object);
	if (!obj || obj->type != OBJ_DEVICE)
		return -EINVAL;
	if (args->method != NV_DEVICE_V0_INFO || argc != sizeof(*info))
		return -EINVAL;
	info = (void *)args->data;
	if (info->version != 0)
		return -EINVAL;

	memset(info, 0, sizeof(*info));
	info->platform = NV_DEVICE_INFO_V0_PCIE;
	info->chipset = PS3RSX_CHIPSET;
	info->family = NV_DEVICE_INFO_V0_CURIE;
	info->ram_size = rsx->info.memory_size;
	info->ram_user = rsx->vram_size;
	strscpy(info->chip, "RSX", sizeof(info->chip));
	strscpy(info->name, "PS3 RSX", sizeof(info->name));
	return 0;
}

static int nvif_sclass(struct ps3rsx_file *pf, struct nvif_ioctl_v0 *ioctl, u32 argc)
{
	/* nv30 never asks (it picks classes by chipset); answer once channels exist */
	return -ENODEV;
}

/* DRM_NOUVEAU_NVIF: variable-size argument, so it bypasses drm_ioctl (as in nouveau) */
int ps3rsx_ioctl_nvif(struct drm_file *file, void __user *user, u32 size)
{
	struct ps3rsx_device *rsx = to_rsx(file->minor->dev);
	struct ps3rsx_file *pf = file->driver_priv;
	struct nvif_ioctl_v0 *ioctl;
	u32 argc = size;
	int ret;

	if (argc < sizeof(*ioctl) || size > PAGE_SIZE)
		return -EINVAL;
	argc -= sizeof(*ioctl);
	ioctl = memdup_user(user, size);
	if (IS_ERR(ioctl))
		return PTR_ERR(ioctl);
	if (ioctl->version != 0 ||
	    (ioctl->route && ioctl->route != NVIF_IOCTL_V0_ROUTE_HIDDEN)) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&pf->lock);
	switch (ioctl->type) {
	case NVIF_IOCTL_V0_SCLASS: ret = nvif_sclass(pf, ioctl, argc); break;
	case NVIF_IOCTL_V0_NEW:    ret = nvif_new(pf, ioctl, argc); break;
	case NVIF_IOCTL_V0_DEL:    ret = nvif_del(pf, ioctl, argc); break;
	case NVIF_IOCTL_V0_MTHD:   ret = nvif_mthd(rsx, pf, ioctl, argc); break;
	default:                   ret = -EINVAL; break;
	}
	mutex_unlock(&pf->lock);

	if (!ret && copy_to_user(user, ioctl, size))
		ret = -EFAULT;
out:
	kfree(ioctl);
	return ret;
}
