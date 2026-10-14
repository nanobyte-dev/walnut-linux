// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal KMS: one CRTC with a primary and a hardware cursor plane, one connector, for the mode
 * ps3fb set up (ps3av), scanning out a VRAM
 * BO by flipping both display heads to it from our context (lv1_gpu_display_flip, E6 in
 * docs/RSX.md). ps3fb is kept off the GPU and the display while we are loaded (takeover), and
 * flips back to its own frame when we go.
 *
 * Pixel format: the RSX scans out bytes X,R,G,B, which is DRM_FORMAT_BGRX8888 (DRM formats are
 * little-endian); on this big-endian host that is "host byte order XRGB".
 */
#include <drm/drm_atomic.h>
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
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <asm/lv1call.h>
#include <asm/ps3.h>
#include <asm/ps3gpu.h>

#include "ps3rsx.h"

static const u32 ps3rsx_formats[] = {
	DRM_FORMAT_BGRX8888,	/* bytes X,R,G,B: what the RSX shows */
	DRM_FORMAT_XRGB8888,	/* accepted for clients that insist; colours come out swapped */
};

static const u32 cursor_formats[] = {
	DRM_FORMAT_ARGB8888,	/* what legacy cursor ioctls create; X fills it with host-order ARGB */
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

/* --- primary plane: the scanout buffer ------------------------------------------------- */

static int primary_check(struct drm_plane *plane, struct drm_atomic_state *state)
{
	struct ps3rsx_device *rsx = to_rsx(plane->dev);
	struct drm_plane_state *ps = drm_atomic_get_new_plane_state(state, plane);
	struct drm_framebuffer *fb = ps->fb;
	struct drm_crtc_state *cs;
	struct ps3rsx_bo *bo;
	int ret;

	if (!fb || !ps->crtc)
		return 0;
	cs = drm_atomic_get_new_crtc_state(state, ps->crtc);
	ret = drm_atomic_helper_check_plane_state(ps, cs, DRM_PLANE_NO_SCALING, DRM_PLANE_NO_SCALING,
						  false, true);
	if (ret)
		return ret;
	bo = fb_bo(fb);
	/* the display reads VRAM at ps3av's fixed pitch, from the start of the frame */
	if (bo->domain != NOUVEAU_GEM_DOMAIN_VRAM || fb->pitches[0] != rsx->info.line_length ||
	    fb->offsets[0] || fb->width != rsx->info.xres || fb->height != rsx->info.yres)
		return -EINVAL;
	return 0;
}

static void primary_update(struct drm_plane *plane, struct drm_atomic_state *state)
{
	struct drm_plane_state *ps = plane->state;

	if (ps->fb && ps->crtc && ps->crtc->state->active)
		flip(to_rsx(plane->dev), fb_bo(ps->fb)->offset);
}

/* the client removed its framebuffer (e.g. X exited): show ps3fb's console frame again
 * rather than a freed buffer */
static void primary_disable(struct drm_plane *plane, struct drm_atomic_state *state)
{
	struct ps3rsx_device *rsx = to_rsx(plane->dev);

	flip(rsx, rsx->info.full_offset);
}

static const struct drm_plane_helper_funcs primary_helper_funcs = {
	.atomic_check = primary_check,
	.atomic_update = primary_update,
	.atomic_disable = primary_disable,
};

/* --- cursor plane: the RSX's 64x64 hardware cursor ---------------------------------------
 * lv1 attributes: 0x10d (head, 1) init; 0x10b (head, op): op 2 image at a VRAM offset, op 3
 * position; 0x10c (head, 1 on / 2 off). As libgcm/RPCS3 use them, from our context.
 * The image is A8R8G8B8 words, which is what X writes into an ARGB8888 buffer on this host.
 */
#define CURSOR_SIZE 64

static bool cursor_init_10d;
module_param(cursor_init_10d, bool, 0444);
MODULE_PARM_DESC(cursor_init_10d, "init the hardware cursor with lv1 attribute 0x10d (untested)");

static void cursor_call(struct ps3rsx_device *rsx, u64 attr, u64 op, u64 a, u64 b)
{
	for (int head = 0; head < 2; head++) {
		int err = lv1_gpu_context_attribute(rsx->ctx.handle, attr, head, op, a, b);

		if (err)
			drm_warn_once(&rsx->drm, "cursor attribute %#llx op %llu failed: %d\n",
				      attr, op, err);
	}
}

static int cursor_check(struct drm_plane *plane, struct drm_atomic_state *state)
{
	struct drm_plane_state *ps = drm_atomic_get_new_plane_state(state, plane);
	struct drm_framebuffer *fb = ps->fb;
	struct drm_crtc_state *cs;
	int ret;

	if (!fb || !ps->crtc)
		return 0;
	cs = drm_atomic_get_new_crtc_state(state, ps->crtc);
	ret = drm_atomic_helper_check_plane_state(ps, cs, DRM_PLANE_NO_SCALING, DRM_PLANE_NO_SCALING,
						  true, true);
	if (ret)
		return ret;
	if (fb->width != CURSOR_SIZE || fb->height != CURSOR_SIZE ||
	    fb->pitches[0] != CURSOR_SIZE * 4 || fb->offsets[0] ||
	    fb_bo(fb)->domain != NOUVEAU_GEM_DOMAIN_VRAM)
		return -EINVAL;
	return 0;
}

static void cursor_update(struct drm_plane *plane, struct drm_atomic_state *state)
{
	struct ps3rsx_device *rsx = to_rsx(plane->dev);
	struct drm_plane_state *old = drm_atomic_get_old_plane_state(state, plane);
	struct drm_plane_state *ps = plane->state;

	if (!ps->fb || !ps->visible) {
		cursor_call(rsx, 0x10c, 2, 0, 0);
		return;
	}
	if (ps->fb != old->fb)
		cursor_call(rsx, 0x10b, 2, fb_bo(ps->fb)->offset, 0);
	/* negative positions (cursor partly off the left/top edge) are not known to work: clamp */
	cursor_call(rsx, 0x10b, 3, max(ps->crtc_x, 0), max(ps->crtc_y, 0));
	if (!old->fb || !old->visible)
		cursor_call(rsx, 0x10c, 1, 0, 0);
}

static void cursor_disable(struct drm_plane *plane, struct drm_atomic_state *state)
{
	cursor_call(to_rsx(plane->dev), 0x10c, 2, 0, 0);
}

static const struct drm_plane_helper_funcs cursor_helper_funcs = {
	.atomic_check = cursor_check,
	.atomic_update = cursor_update,
	.atomic_disable = cursor_disable,
};

static const struct drm_plane_funcs plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

/* Show ps3fb's frame (the console). Flips from ps3fb's own context don't override a frame our
 * context flipped to, so the handover has to point the display there itself. */
void ps3rsx_kms_show_console(struct ps3rsx_device *rsx)
{
	/* libgcm's cellGcmResetFlipStatus before flipping: clear "flip waiting" (bit 31) of each
	 * head, which nothing acknowledges for our context (no vsync handler). A precaution: the
	 * console showed without it too. */
	lv1_gpu_context_attribute(rsx->ctx.handle, 0x10a, 0, 0x7fffffff, 0, 0);
	lv1_gpu_context_attribute(rsx->ctx.handle, 0x10a, 1, 0x7fffffff, 0, 0);
	flip(rsx, rsx->info.full_offset);
}

/* Show the current primary framebuffer again (after ps3fb had the display) */
void ps3rsx_kms_reflip(struct ps3rsx_device *rsx)
{
	struct drm_plane_state *ps = rsx->primary.state;

	if (ps && ps->fb && rsx->crtc.state && rsx->crtc.state->active)
		flip(rsx, fb_bo(ps->fb)->offset);
}

/* --- CRTC ------------------------------------------------------------------------------- */

/*
 * vblank: the display's vsync interrupt on our context's IRQ outlet (E7), head A like ps3fb.
 * Flips latch at that vsync, so flip events sent from it match what is on screen; a free-running
 * timer drifted against the real vsync: events came before the flip had latched (clients drew
 * into the buffer still being scanned out) and vblank waits timed out. vblank_irq=0 brings the
 * timer back.
 */
static bool vblank_irq = true;
module_param(vblank_irq, bool, 0444);
MODULE_PARM_DESC(vblank_irq, "vblank from the RSX vsync interrupt (default) instead of a timer");

/* driver info: display heads at 0x10b8 (8 x 64 bytes), then the IRQ block */
#define DINFO_IRQ_OUTLET	0x12b8
#define DINFO_IRQ_MASK		0x12c0
#define INTR_VSYNC_A		(1 << 0)

static irqreturn_t vsync_interrupt(int irq, void *data)
{
	struct ps3rsx_device *rsx = data;
	u64 v = 0;

	if (lv1_gpu_context_intr(rsx->ctx.handle, &v))
		return IRQ_NONE;
	if (v & INTR_VSYNC_A)
		drm_crtc_handle_vblank(&rsx->crtc);
	return IRQ_HANDLED;
}

static enum hrtimer_restart vblank_tick(struct hrtimer *t)
{
	struct ps3rsx_device *rsx = container_of(t, struct ps3rsx_device, vblank_timer);

	/* false once vblank is off: disable_vblank's cancel can miss a running tick */
	if (!drm_crtc_handle_vblank(&rsx->crtc))
		return HRTIMER_NORESTART;
	hrtimer_forward_now(t, rsx->vblank_period);
	return HRTIMER_RESTART;
}

static int enable_vblank(struct drm_crtc *crtc)
{
	struct ps3rsx_device *rsx = to_rsx(crtc->dev);

	if (rsx->vsync_virq) {
		out_be32(rsx->dinfo + DINFO_IRQ_MASK, INTR_VSYNC_A);
		return 0;
	}
	rsx->vblank_period = ns_to_ktime(drm_crtc_vblank_crtc(crtc)->framedur_ns);
	hrtimer_start(&rsx->vblank_timer, rsx->vblank_period, HRTIMER_MODE_REL);
	return 0;
}

static void disable_vblank(struct drm_crtc *crtc)
{
	struct ps3rsx_device *rsx = to_rsx(crtc->dev);

	if (rsx->vsync_virq)
		out_be32(rsx->dinfo + DINFO_IRQ_MASK, 0);
	else
		hrtimer_try_to_cancel(&rsx->vblank_timer);
}

static void vsync_irq_init(struct ps3rsx_device *rsx)
{
	unsigned int virq;
	int ret;

	if (!vblank_irq)
		return;
	out_be32(rsx->dinfo + DINFO_IRQ_MASK, 0);
	ret = ps3_irq_plug_setup(PS3_BINDING_CPU_ANY, in_be32(rsx->dinfo + DINFO_IRQ_OUTLET), &virq);
	if (ret) {
		drm_warn(&rsx->drm, "vsync IRQ plug: %d, timer vblank\n", ret);
		return;
	}
	ret = request_irq(virq, vsync_interrupt, 0, "ps3rsx vsync", rsx);
	if (ret) {
		drm_warn(&rsx->drm, "vsync IRQ: %d, timer vblank\n", ret);
		ps3_irq_plug_destroy(virq);
		return;
	}
	rsx->vsync_virq = virq;
}

void ps3rsx_kms_fini(struct ps3rsx_device *rsx)
{
	hrtimer_cancel(&rsx->vblank_timer);
	if (rsx->vsync_virq) {
		out_be32(rsx->dinfo + DINFO_IRQ_MASK, 0);
		free_irq(rsx->vsync_virq, rsx);
		ps3_irq_plug_destroy(rsx->vsync_virq);
		rsx->vsync_virq = 0;
	}
}

/* hand the commit's flip event to the next vblank */
static void arm_event(struct drm_crtc *crtc)
{
	struct drm_pending_vblank_event *event = crtc->state->event;

	if (!event)
		return;
	crtc->state->event = NULL;
	spin_lock_irq(&crtc->dev->event_lock);
	if (drm_crtc_vblank_get(crtc) == 0)
		drm_crtc_arm_vblank_event(crtc, event);
	else
		drm_crtc_send_vblank_event(crtc, event);
	spin_unlock_irq(&crtc->dev->event_lock);
}

static void crtc_flush(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	arm_event(crtc);
}

static void crtc_enable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	drm_crtc_vblank_on(crtc);
}

static void crtc_disable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct ps3rsx_device *rsx = to_rsx(crtc->dev);

	drm_crtc_vblank_off(crtc);
	cursor_call(rsx, 0x10c, 2, 0, 0);
	/* back to ps3fb's frame (it redraws the console when it gets the GPU back) */
	flip(rsx, rsx->info.full_offset);
	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);
		crtc->state->event = NULL;
	}
}

static const struct drm_crtc_helper_funcs crtc_helper_funcs = {
	.atomic_flush = crtc_flush,
	.atomic_enable = crtc_enable,
	.atomic_disable = crtc_disable,
};

static const struct drm_crtc_funcs crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = enable_vblank,
	.disable_vblank = disable_vblank,
};

/* Debug: echo OFFSET > /sys/module/ps3rsx/parameters/flip_test flips both heads there */
static struct ps3rsx_device *flip_test_rsx;

static int flip_test_set(const char *val, const struct kernel_param *kp)
{
	u64 off;
	int ret = kstrtou64(val, 0, &off);

	if (ret)
		return ret;
	if (!flip_test_rsx)
		return -ENODEV;
	flip(flip_test_rsx, off);
	return 0;
}

static const struct kernel_param_ops flip_test_ops = { .set = flip_test_set };
module_param_cb(flip_test, &flip_test_ops, NULL, 0200);

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
	/* no minimum: cursor framebuffers are 64x64 (primary_check enforces the screen size) */
	dev->mode_config.max_width = rsx->info.xres;
	dev->mode_config.max_height = rsx->info.yres;
	dev->mode_config.preferred_depth = 24;
	dev->mode_config.prefer_shadow = 1;	/* CPU reads of VRAM run at 10 MB/s */
	dev->mode_config.quirk_addfb_prefer_host_byte_order = true;
	dev->mode_config.cursor_width = CURSOR_SIZE;
	dev->mode_config.cursor_height = CURSOR_SIZE;
	dev->mode_config.funcs = &mode_config_funcs;

	ret = drm_connector_init(dev, &rsx->connector, &connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret)
		return ret;
	drm_connector_helper_add(&rsx->connector, &connector_helper_funcs);

	ret = drm_universal_plane_init(dev, &rsx->primary, 1, &plane_funcs, ps3rsx_formats,
				       ARRAY_SIZE(ps3rsx_formats), NULL, DRM_PLANE_TYPE_PRIMARY,
				       NULL);
	if (ret)
		return ret;
	drm_plane_helper_add(&rsx->primary, &primary_helper_funcs);
	ret = drm_universal_plane_init(dev, &rsx->cursor, 1, &plane_funcs, cursor_formats,
				       ARRAY_SIZE(cursor_formats), NULL, DRM_PLANE_TYPE_CURSOR, NULL);
	if (ret)
		return ret;
	drm_plane_helper_add(&rsx->cursor, &cursor_helper_funcs);
	ret = drm_crtc_init_with_planes(dev, &rsx->crtc, &rsx->primary, &rsx->cursor, &crtc_funcs,
					NULL);
	if (ret)
		return ret;
	drm_crtc_helper_add(&rsx->crtc, &crtc_helper_funcs);
	ret = drm_simple_encoder_init(dev, &rsx->encoder, DRM_MODE_ENCODER_TMDS);
	if (ret)
		return ret;
	rsx->encoder.possible_crtcs = drm_crtc_mask(&rsx->crtc);
	ret = drm_connector_attach_encoder(&rsx->connector, &rsx->encoder);
	if (ret)
		return ret;
	/* ps3fb's init is 0x10b op 1: lv1 accepts it, but the cursor stays hidden. libgcm's
	 * cellGcmInitCursor is 0x10d (RPCS3 sys_rsx.cpp); the PS3 dropped off the network right after
	 * a reload that used it (2026-10-14, cause unknown), so it is opt-in until tested supervised. */
	cursor_call(rsx, cursor_init_10d ? 0x10d : 0x10b, 1, 0, 0);
	ret = drm_vblank_init(dev, 1);
	if (ret)
		return ret;
	hrtimer_init(&rsx->vblank_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	rsx->vblank_timer.function = vblank_tick;
	vsync_irq_init(rsx);
	drm_mode_config_reset(dev);
	flip_test_rsx = rsx;
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
