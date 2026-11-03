/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The push dump (pushdump.c): only in a test build made with DEB_BUILD_OPTIONS=ps3rsx-pushdump
 * (PS3RSX_PUSH_DUMP); everywhere else these hooks are empty and compile away.
 */
#ifndef PS3RSX_PUSHDUMP_H
#define PS3RSX_PUSHDUMP_H

struct ps3rsx_device;
struct ps3rsx_chan;
struct ps3rsx_bo;
struct drm_file;
struct drm_gem_object;
struct dma_fence;
struct drm_nouveau_gem_pushbuf_bo;
struct drm_nouveau_gem_pushbuf_push;
struct pd_submit;

#ifdef PS3RSX_PUSH_DUMP
void ps3rsx_pd_init(struct ps3rsx_device *rsx);
void ps3rsx_pd_fini(struct ps3rsx_device *rsx);
void ps3rsx_pd_bo_event(struct ps3rsx_bo *bo, struct drm_file *file, bool open);
struct pd_submit *ps3rsx_pd_submit(struct ps3rsx_device *rsx, struct ps3rsx_chan *ch,
				   bool accepted, u32 bad_push,
				   const struct drm_nouveau_gem_pushbuf_push *push, u32 nr_push,
				   const struct drm_nouveau_gem_pushbuf_bo *bos,
				   struct drm_gem_object **objs, u32 nr_buffers,
				   const u32 *restore, u32 restore_words, const u32 *pushes);
void ps3rsx_pd_contents(struct ps3rsx_device *rsx, struct pd_submit *ps, struct dma_fence *fence,
			struct drm_gem_object **objs, u32 nr_buffers);
void ps3rsx_pd_end(struct ps3rsx_device *rsx, struct ps3rsx_chan *ch, u32 header);
#else
static inline void ps3rsx_pd_init(struct ps3rsx_device *rsx) {}
static inline void ps3rsx_pd_fini(struct ps3rsx_device *rsx) {}
static inline void ps3rsx_pd_bo_event(struct ps3rsx_bo *bo, struct drm_file *file, bool open) {}
static inline struct pd_submit *
ps3rsx_pd_submit(struct ps3rsx_device *rsx, struct ps3rsx_chan *ch, bool accepted, u32 bad_push,
		 const struct drm_nouveau_gem_pushbuf_push *push, u32 nr_push,
		 const struct drm_nouveau_gem_pushbuf_bo *bos, struct drm_gem_object **objs,
		 u32 nr_buffers, const u32 *restore, u32 restore_words, const u32 *pushes)
{
	return NULL;
}
static inline void ps3rsx_pd_contents(struct ps3rsx_device *rsx, struct pd_submit *ps,
				      struct dma_fence *fence, struct drm_gem_object **objs,
				      u32 nr_buffers) {}
static inline void ps3rsx_pd_end(struct ps3rsx_device *rsx, struct ps3rsx_chan *ch, u32 header) {}
#endif

#endif
