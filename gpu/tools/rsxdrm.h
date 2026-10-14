/* Shared by the rsxdrm-* tools: just enough of drm.h / nouveau_drm.h / nvif for raw ioctls. */
#ifndef RSXDRM_H
#define RSXDRM_H
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>

#define DRM_COMMAND_BASE 0x40
struct drm_nouveau_getparam { uint64_t param, value; };
#define DRM_IOCTL_NOUVEAU_GETPARAM _IOWR('d', DRM_COMMAND_BASE + 0x00, struct drm_nouveau_getparam)

#define NOUVEAU_GEM_DOMAIN_VRAM     (1 << 1)
#define NOUVEAU_GEM_DOMAIN_GART     (1 << 2)
#define NOUVEAU_GEM_DOMAIN_MAPPABLE (1 << 3)
struct drm_nouveau_gem_info {
	uint32_t handle, domain;
	uint64_t size, offset, map_handle;
	uint32_t tile_mode, tile_flags;
};
struct drm_nouveau_gem_new {
	struct drm_nouveau_gem_info info;
	uint32_t channel_hint, align;
};
#define DRM_IOCTL_NOUVEAU_GEM_NEW  _IOWR('d', DRM_COMMAND_BASE + 0x40, struct drm_nouveau_gem_new)
#define DRM_IOCTL_NOUVEAU_GEM_INFO _IOWR('d', DRM_COMMAND_BASE + 0x44, struct drm_nouveau_gem_info)

struct drm_gem_close { uint32_t handle, pad; };
#define DRM_IOCTL_GEM_CLOSE _IOW('d', 0x09, struct drm_gem_close)

static inline int gem_new(int fd, uint32_t domain, uint64_t size, uint32_t align,
			  struct drm_nouveau_gem_info *out)
{
	struct drm_nouveau_gem_new req = { .info = { .domain = domain, .size = size }, .align = align };
	if (ioctl(fd, DRM_IOCTL_NOUVEAU_GEM_NEW, &req))
		return -errno;
	*out = req.info;
	return 0;
}

static inline void gem_close(int fd, uint32_t handle)
{
	struct drm_gem_close c = { .handle = handle };
	ioctl(fd, DRM_IOCTL_GEM_CLOSE, &c);
}
#endif
