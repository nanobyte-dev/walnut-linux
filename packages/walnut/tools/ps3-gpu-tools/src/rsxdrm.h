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

struct drm_nouveau_channel_alloc {
	uint32_t fb_ctxdma_handle, tt_ctxdma_handle;
	int32_t channel;
	uint32_t pushbuf_domains, notifier_handle;
	struct { uint32_t handle, grclass; } subchan[8];
	uint32_t nr_subchan;
};
#define DRM_IOCTL_NOUVEAU_CHANNEL_ALLOC _IOWR('d', DRM_COMMAND_BASE + 0x02, struct drm_nouveau_channel_alloc)
struct drm_nouveau_notifierobj_alloc { uint32_t channel, handle, size, offset; };
#define DRM_IOCTL_NOUVEAU_NOTIFIEROBJ_ALLOC _IOWR('d', DRM_COMMAND_BASE + 0x05, struct drm_nouveau_notifierobj_alloc)

struct drm_nouveau_gem_pushbuf_bo {
	uint64_t user_priv;
	uint32_t handle, read_domains, write_domains, valid_domains;
	struct { uint32_t valid, domain; uint64_t offset; } presumed;
};
struct drm_nouveau_gem_pushbuf_push { uint32_t bo_index, pad; uint64_t offset, length; };
struct drm_nouveau_gem_pushbuf {
	uint32_t channel, nr_buffers;
	uint64_t buffers;
	uint32_t nr_relocs, nr_push;
	uint64_t relocs, push;
	uint32_t suffix0, suffix1;
	uint64_t vram_available, gart_available;
};
#define NOUVEAU_GEM_PUSHBUF_SYNC 1ull
#define DRM_IOCTL_NOUVEAU_GEM_PUSHBUF _IOWR('d', DRM_COMMAND_BASE + 0x41, struct drm_nouveau_gem_pushbuf)
struct drm_nouveau_gem_cpu_prep { uint32_t handle, flags; };
#define DRM_IOCTL_NOUVEAU_GEM_CPU_PREP _IOW('d', DRM_COMMAND_BASE + 0x42, struct drm_nouveau_gem_cpu_prep)

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
