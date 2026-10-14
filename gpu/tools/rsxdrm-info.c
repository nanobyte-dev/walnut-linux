/*
 * rsxdrm-info: Phase 2 step 1 check of the ps3rsx DRM driver, the way Mesa's nouveau winsys
 * talks to it: DRM version, GETPARAMs, NVIF device object + device info.
 *
 *   rsxdrm-info [/dev/dri/renderD128]
 */
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* drm.h / nouveau_drm.h / nvif (just what is needed) */
struct drm_version {
	int version_major, version_minor, version_patchlevel;
	size_t name_len; char *name;
	size_t date_len; char *date;
	size_t desc_len; char *desc;
};
#define DRM_IOCTL_VERSION _IOWR('d', 0x00, struct drm_version)
#define DRM_COMMAND_BASE 0x40
struct drm_nouveau_getparam { uint64_t param, value; };
#define DRM_IOCTL_NOUVEAU_GETPARAM _IOWR('d', DRM_COMMAND_BASE + 0x00, struct drm_nouveau_getparam)
#define DRM_NOUVEAU_NVIF 0x07

struct nvif_ioctl_v0 {
	uint8_t version, type, pad02[4], owner, route;
	uint64_t token, object;
};
struct nvif_ioctl_new_v0 {
	uint8_t version, pad01[6], route;
	uint64_t token, object;
	uint32_t handle;
	int32_t oclass;
};
struct nvif_ioctl_mthd_v0 { uint8_t version, method, pad02[6]; };
struct nv_device_info_v0 {
	uint8_t version, platform;
	uint16_t chipset;
	uint8_t revision, family, pad06[2];
	uint64_t ram_size, ram_user;
	char chip[16], name[64];
};

static int nvif(int fd, void *data, size_t size)
{
	unsigned long req = _IOC(_IOC_READ | _IOC_WRITE, 'd', DRM_COMMAND_BASE + DRM_NOUVEAU_NVIF, size);
	return ioctl(fd, req, data) ? -errno : 0;
}

static void getparam(int fd, const char *name, uint64_t param)
{
	struct drm_nouveau_getparam gp = { .param = param };
	if (ioctl(fd, DRM_IOCTL_NOUVEAU_GETPARAM, &gp))
		printf("  %-16s (%#llx): %s\n", name, (unsigned long long)param, strerror(errno));
	else
		printf("  %-16s (%#llx): %#llx (%llu)\n", name, (unsigned long long)param,
		       (unsigned long long)gp.value, (unsigned long long)gp.value);
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/dev/dri/renderD128";
	int fd = open(path, O_RDWR);
	if (fd < 0) { perror(path); return 1; }

	char name[64] = "", date[64] = "", desc[128] = "";
	struct drm_version v = { .name_len = sizeof(name) - 1, .name = name,
				 .date_len = sizeof(date) - 1, .date = date,
				 .desc_len = sizeof(desc) - 1, .desc = desc };
	if (ioctl(fd, DRM_IOCTL_VERSION, &v)) { perror("DRM_IOCTL_VERSION"); return 1; }
	printf("%s: driver '%s' %d.%d.%d, '%s'\n", path, name, v.version_major, v.version_minor,
	       v.version_patchlevel, desc);

	printf("GETPARAM:\n");
	getparam(fd, "CHIPSET_ID", 11);
	getparam(fd, "PCI_VENDOR", 3);
	getparam(fd, "BUS_TYPE", 5);
	getparam(fd, "FB_SIZE", 8);
	getparam(fd, "AGP_SIZE", 9);
	getparam(fd, "PTIMER_TIME", 14);
	getparam(fd, "HAS_VMA_TILEMODE", 20);
	getparam(fd, "PS3_ABI", 0x1000);

	/* NVIF: NEW device (route 0), then MTHD NV_DEVICE_V0_INFO, as Mesa's nouveau_device_new */
	struct { struct nvif_ioctl_v0 io; struct nvif_ioctl_new_v0 new; } n = {
		.io = { .type = 0x02, .owner = 0xff },
		.new = { .object = 0x1234, .oclass = 0x80 },
	};
	int rc = nvif(fd, &n, sizeof(n));
	printf("NVIF NEW device: %s\n", rc ? strerror(-rc) : "ok");
	struct { struct nvif_ioctl_v0 io; struct nvif_ioctl_mthd_v0 m; struct nv_device_info_v0 info; } m = {
		.io = { .type = 0x04, .owner = 0xff, .object = 0x1234 },
		.m = { .method = 0x00 },
	};
	rc = nvif(fd, &m, sizeof(m));
	if (rc)
		printf("NVIF device info: %s\n", strerror(-rc));
	else
		printf("NVIF device info: chipset %#x family %u platform %u ram %llu MiB (user %llu MiB) "
		       "chip '%s' name '%s'\n", m.info.chipset, m.info.family, m.info.platform,
		       (unsigned long long)m.info.ram_size >> 20,
		       (unsigned long long)m.info.ram_user >> 20, m.info.chip, m.info.name);
	return 0;
}
