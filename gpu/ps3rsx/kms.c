// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal KMS: one CRTC/plane/connector for the mode ps3fb set up (ps3av), scanning out a VRAM
 * BO by flipping both display heads to it from our context (lv1_gpu_display_flip, E6 in
 * docs/RSX.md). ps3fb is kept off the GPU and the display while we are loaded (takeover), and
 * flips back to its own frame when we go.
 *
 * Pixel format: the RSX scans out bytes X,R,G,B, which is DRM_FORMAT_BGRX8888 (DRM formats are
 * little-endian); on this big-endian host that is "host byte order XRGB".
 */
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>
#include <asm/ps3gpu.h>

#include "ps3rsx.h"

static const u32 ps3rsx_formats[] = {
	DRM_FORMAT_BGRX8888,	/* bytes X,R,G,B: what the RSX shows */
	DRM_FORMAT_XRGB8888,	/* accepted for clients that insist; colours come out swapped */
};

static void flip(struct ps3rsx_device *rsx, u64 offset)
{
	int s0 = lv1_gpu_display_flip(rsx->ctx.handle, 0, offset);
	int s1 = lv1_gpu_display_flip(rsx->ctx.handle, 1, offset);

	if (s0 || s1)
		drm_warn_once(&rsx->drm, "display flip to %#llx failed: %d %d\n", offset, s0, s1);
}

static int connector_get_modes(struct drm_connector *connector)
{
	struct ps3rsx_device *rsx = to_rsx(connector->dev);
	struct drm_display_mode *mode;

	mode = drm_cvt_mode(connector->dev, rsx->info.xres, rsx->info.yres, 60, false, false, false);
	if (!mode)
		return 0;
	mode->type |= DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);
	return 1;
}

static const struct drm_connector_helper_funcs connector_helper_funcs = {
	.get_modes = connector_get_modes,
};

static const struct drm_connector_funcs connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct ps3rsx_bo *fb_bo(struct drm_framebuffer *fb)
{
	return to_bo(drm_gem_fb_get_obj(fb, 0));
}

static int pipe_check(struct drm_simple_display_pipe *pipe, struct drm_plane_state *plane_state,
		      struct drm_crtc_state *crtc_state)
{
	struct ps3rsx_device *rsx = to_rsx(pipe->crtc.dev);
	struct drm_framebuffer *fb = plane_state->fb;
	struct ps3rsx_bo *bo;

	/* no vblank interrupt wired up yet: the atomic helpers send flip events at once */
	crtc_state->no_vblank = true;
	if (!fb)
		return 0;
	bo = fb_bo(fb);
	/* the display reads VRAM at ps3av's fixed pitch, from the start of the frame */
	if (bo->domain != NOUVEAU_GEM_DOMAIN_VRAM || fb->pitches[0] != rsx->info.line_length ||
	    fb->offsets[0] || fb->width != rsx->info.xres || fb->height != rsx->info.yres)
		return -EINVAL;
	return 0;
}

static void pipe_update(struct drm_simple_display_pipe *pipe, struct drm_plane_state *old)
{
	struct ps3rsx_device *rsx = to_rsx(pipe->crtc.dev);
	struct drm_framebuffer *fb = pipe->plane.state->fb;

	if (fb && pipe->crtc.state->active)
		flip(rsx, fb_bo(fb)->offset);
}

static void pipe_enable(struct drm_simple_display_pipe *pipe, struct drm_crtc_state *crtc_state,
			struct drm_plane_state *plane_state)
{
	struct ps3rsx_device *rsx = to_rsx(pipe->crtc.dev);

	if (plane_state->fb)
		flip(rsx, fb_bo(plane_state->fb)->offset);
}

static void pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct ps3rsx_device *rsx = to_rsx(pipe->crtc.dev);

	/* back to ps3fb's frame (it redraws the console when it gets the GPU back) */
	flip(rsx, rsx->info.full_offset);
}

static const struct drm_simple_display_pipe_funcs pipe_funcs = {
	.check = pipe_check,
	.enable = pipe_enable,
	.disable = pipe_disable,
	.update = pipe_update,
};

static const struct drm_mode_config_funcs mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

int ps3rsx_kms_init(struct ps3rsx_device *rsx)
{
	struct drm_device *dev = &rsx->drm;
	int ret;

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;
	dev->mode_config.min_width = rsx->info.xres;
	dev->mode_config.max_width = rsx->info.xres;
	dev->mode_config.min_height = rsx->info.yres;
	dev->mode_config.max_height = rsx->info.yres;
	dev->mode_config.preferred_depth = 24;
	dev->mode_config.prefer_shadow = 1;	/* CPU reads of VRAM run at 10 MB/s */
	dev->mode_config.quirk_addfb_prefer_host_byte_order = true;
	dev->mode_config.funcs = &mode_config_funcs;

	ret = drm_connector_init(dev, &rsx->connector, &connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret)
		return ret;
	drm_connector_helper_add(&rsx->connector, &connector_helper_funcs);
	ret = drm_simple_display_pipe_init(dev, &rsx->pipe, &pipe_funcs, ps3rsx_formats,
					   ARRAY_SIZE(ps3rsx_formats), NULL, &rsx->connector);
	if (ret)
		return ret;
	drm_mode_config_reset(dev);
	return 0;
}

/* --- dumb buffers: CPU-visible VRAM at the display pitch --------------------------------- */

int ps3rsx_dumb_create(struct drm_file *file, struct drm_device *dev,
		       struct drm_mode_create_dumb *args)
{
	struct ps3rsx_device *rsx = to_rsx(dev);
	struct drm_nouveau_gem_info info = {};
	struct ps3rsx_bo *bo;
	int ret;

	if (args->bpp != 32)
		return -EINVAL;
	args->pitch = args->width == rsx->info.xres ? rsx->info.line_length
						      : ALIGN(args->width * 4, 64);
	args->size = PAGE_ALIGN((u64)args->pitch * args->height);
	bo = ps3rsx_bo_new(rsx, NOUVEAU_GEM_DOMAIN_VRAM | NOUVEAU_GEM_DOMAIN_MAPPABLE,
			   args->size, 0);
	if (IS_ERR(bo))
		return PTR_ERR(bo);
	ret = ps3rsx_bo_handle(file, bo, &info);
	drm_gem_object_put(&bo->base);
	if (!ret)
		args->handle = info.handle;
	return ret;
}
