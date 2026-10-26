/* SPDX-License-Identifier: MIT */
/*
 * The NVIF ioctl subset Mesa's nouveau winsys uses, copied from the nouveau kernel driver's
 * include/nvif/{ioctl,cl0080,class}.h (not exported to modules outside drivers/gpu/drm/nouveau).
 */
#ifndef PS3RSX_NVIF_H
#define PS3RSX_NVIF_H

#include <linux/types.h>

struct nvif_ioctl_v0 {
	__u8  version;
#define NVIF_IOCTL_V0_SCLASS	0x01
#define NVIF_IOCTL_V0_NEW	0x02
#define NVIF_IOCTL_V0_DEL	0x03
#define NVIF_IOCTL_V0_MTHD	0x04
	__u8  type;
	__u8  pad02[4];
#define NVIF_IOCTL_V0_OWNER_ANY	0xff
	__u8  owner;
#define NVIF_IOCTL_V0_ROUTE_NVIF	0x00
#define NVIF_IOCTL_V0_ROUTE_HIDDEN	0xff
	__u8  route;
	__u64 token;
	__u64 object;
	__u8  data[];
};

struct nvif_ioctl_sclass_v0 {
	__u8  version;
	__u8  count;
	__u8  pad02[6];
	struct nvif_ioctl_sclass_oclass_v0 {
		__s32 oclass;
		__s16 minver;
		__s16 maxver;
	} oclass[];
};

struct nvif_ioctl_new_v0 {
	__u8  version;
	__u8  pad01[6];
	__u8  route;
	__u64 token;
	__u64 object;
	__u32 handle;
	__s32 oclass;
	__u8  data[];
};

struct nvif_ioctl_mthd_v0 {
	__u8  version;
	__u8  method;
	__u8  pad02[6];
	__u8  data[];
};

#define NV_DEVICE		0x00000080
#define NV_DEVICE_V0_INFO	0x00

struct nv_device_info_v0 {
	__u8  version;
#define NV_DEVICE_INFO_V0_PCIE	0x03
	__u8  platform;
	__u16 chipset;
	__u8  revision;
#define NV_DEVICE_INFO_V0_CURIE	0x05
	__u8  family;
	__u8  pad06[2];
	__u64 ram_size;
	__u64 ram_user;
	char  chip[16];
	char  name[64];
};

#endif
