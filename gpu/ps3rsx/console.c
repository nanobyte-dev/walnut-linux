// SPDX-License-Identifier: GPL-2.0
/*
 * Sharing the RSX with ps3fb's text consoles (Ctrl+Alt+Fn).
 *
 * The 3D engine can't be shared (lv1 does no context switching, docs/RSX.md E5), so ps3fb and
 * ps3rsx take turns owning it (ps3fb_gpu_takeover). ps3rsx owns it while a DRM master (X) is
 * active or while clients without one (offscreen GL) are submitting; otherwise ps3fb gets it
 * back and draws the console:
 *  - master_drop (VT switch away, X exiting): new submits wait, except from a channel with a
 *    primitive open (ps3fb must not touch the GPU mid-primitive); once idle, ps3fb gets the GPU.
 *  - master_set (VT switch back): take the GPU, forget what PGRAPH held (ps3fb changed it: the
 *    next submit restores its channel's whole state), show the master's frame again.
 *  - no X at all: the first submit takes the GPU, 2 s of idle gives it back.
 * While an X server is open but switched away, its clients (e.g. GL apps) wait for its return.
 */
#include <drm/drm_print.h>
#include <asm/ps3gpu.h>

#include "ps3rsx.h"

#define IDLE_RELEASE	msecs_to_jiffies(2000)

/* an X server is open but not in front: its clients wait */
static bool x_away(struct ps3rsx_device *rsx)
{
	return rsx->masters_open > 0 && !rsx->master_active;
}

/* own_lock held */
static int retake(struct ps3rsx_device *rsx)
{
	int ret;

	if (rsx->gpu_owned)
		return 0;
	ret = ps3fb_gpu_takeover(true);
	if (ret) {
		pr_warn_ratelimited("ps3rsx: ps3fb won't hand the GPU back: %d\n", ret);
		return ret;
	}
	/* no submitter is running (submitting == 0 while released): the shadow is ours */
	ps3rsx_exec_forget_hw_state(rsx);
	rsx->gpu_owned = true;
	return 0;
}

static void release_work(struct work_struct *work)
{
	struct ps3rsx_device *rsx = container_of(to_delayed_work(work), struct ps3rsx_device,
						 release_work);

	mutex_lock(&rsx->own_lock);
	if (!rsx->gpu_owned || rsx->master_active)
		goto out;
	if (rsx->submitting || !ps3rsx_exec_idle(rsx)) {
		/* work in flight or a primitive left open: look again shortly */
		schedule_delayed_work(&rsx->release_work, msecs_to_jiffies(20));
		goto out;
	}
	ps3rsx_kms_show_console(rsx);
	if (ps3fb_gpu_takeover(false) == 0) {
		rsx->gpu_owned = false;
		rsx->console_pending = false;
		drm_dbg_driver(&rsx->drm, "GPU handed to ps3fb (console)\n");
	}
	wake_up_all(&rsx->own_wq);
out:
	mutex_unlock(&rsx->own_lock);
}

/*
 * Before a submit touches the GPU. Returns 0 (call ps3rsx_console_unhold after) or an errno.
 * A former master (X itself, e.g. cleaning up on exit while switched away) is never made to
 * wait for its own return: it takes the GPU briefly and the console gets it back when idle.
 */
int ps3rsx_console_hold(struct ps3rsx_device *rsx, struct ps3rsx_chan *ch, struct ps3rsx_file *pf)
{
	int ret;

	for (;;) {
		mutex_lock(&rsx->own_lock);
		/* the channel holding a primitive open finishes it even while the console waits */
		if ((rsx->gpu_owned && !rsx->console_pending) ||
		    (rsx->gpu_owned && (READ_ONCE(rsx->prim_owner) == ch || pf->was_master))) {
			rsx->submitting++;
			mutex_unlock(&rsx->own_lock);
			return 0;
		}
		if (!rsx->gpu_owned && (!x_away(rsx) || pf->was_master)) {
			ret = retake(rsx);	/* a client without X: the console freezes meanwhile */
			if (!ret) {
				rsx->submitting++;
				/* X working while switched away (panel clock, exit cleanup): give the
				 * console the GPU back as soon as this is done, not after 2 s idle */
				if (x_away(rsx))
					rsx->console_pending = true;
			}
			mutex_unlock(&rsx->own_lock);
			return ret;
		}
		mutex_unlock(&rsx->own_lock);
		ret = wait_event_interruptible(rsx->own_wq,
			(READ_ONCE(rsx->gpu_owned) && !READ_ONCE(rsx->console_pending)) ||
			(!READ_ONCE(rsx->gpu_owned) && !x_away(rsx)));
		if (ret)
			return ret;
	}
}

void ps3rsx_console_unhold(struct ps3rsx_device *rsx)
{
	mutex_lock(&rsx->own_lock);
	if (!--rsx->submitting && !rsx->master_active)
		mod_delayed_work(system_wq, &rsx->release_work,
				 rsx->console_pending ? 0 : IDLE_RELEASE);
	mutex_unlock(&rsx->own_lock);
}

/* drm_driver.master_set: X is (back) in front */
void ps3rsx_master_set(struct drm_device *dev, struct drm_file *file, bool from_open)
{
	struct ps3rsx_device *rsx = to_rsx(dev);
	struct ps3rsx_file *pf = file->driver_priv;

	mutex_lock(&rsx->own_lock);
	if (!pf->was_master) {
		pf->was_master = true;
		rsx->masters_open++;
	}
	rsx->master_active = true;
	rsx->console_pending = false;
	cancel_delayed_work(&rsx->release_work);
	if (!retake(rsx))
		ps3rsx_kms_reflip(rsx);	/* the master's frame, not ps3fb's console */
	wake_up_all(&rsx->own_wq);
	mutex_unlock(&rsx->own_lock);
}

/* drm_driver.master_drop: VT switch away, or the master closing */
void ps3rsx_master_drop(struct drm_device *dev, struct drm_file *file)
{
	struct ps3rsx_device *rsx = to_rsx(dev);

	mutex_lock(&rsx->own_lock);
	rsx->master_active = false;
	rsx->console_pending = true;
	mod_delayed_work(system_wq, &rsx->release_work, 0);
	mutex_unlock(&rsx->own_lock);
}

/* postclose: a former master is gone; clients that waited for it may run again */
void ps3rsx_console_file_closed(struct ps3rsx_device *rsx, struct ps3rsx_file *pf)
{
	if (!pf->was_master)
		return;
	mutex_lock(&rsx->own_lock);
	rsx->masters_open--;
	wake_up_all(&rsx->own_wq);
	mutex_unlock(&rsx->own_lock);
}

/* after exec init (which took the GPU): give the console back until someone needs the GPU */
void ps3rsx_console_init(struct ps3rsx_device *rsx)
{
	mutex_init(&rsx->own_lock);
	init_waitqueue_head(&rsx->own_wq);
	INIT_DELAYED_WORK(&rsx->release_work, release_work);
	rsx->gpu_owned = true;
	schedule_delayed_work(&rsx->release_work, 0);
}

/* before exec fini: own the GPU again so the teardown can drain and release it */
void ps3rsx_console_fini(struct ps3rsx_device *rsx)
{
	cancel_delayed_work_sync(&rsx->release_work);
	mutex_lock(&rsx->own_lock);
	retake(rsx);
	mutex_unlock(&rsx->own_lock);
}
