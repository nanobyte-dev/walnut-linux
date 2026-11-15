// SPDX-License-Identifier: GPL-2.0
/*
 * Host test of src/busy.h: the released-busy flag as exec.c uses it (kernel local/0057).
 * - exec_init with the accessor present: refuses when ps3fb says the GPU was released busy, loads when
 *   it doesn't;
 * - exec_init with the accessor absent (the stable kernel, symbol_get() gave NULL): loads, and the
 *   absence is told apart from "clear" so exec.c can log it once;
 * - exec_fini: the GPU goes back to ps3fb only when idle and not wedged; a drain that timed out, a
 *   wedged GPU, either alone, keep it and so set the flag; a recovered GPU stays with ps3rsx and sets
 *   nothing;
 * - end to end on a model of ps3fb: unload busy, then a reload is refused; unload idle, the reload
 *   loads; the flag stays set across more loads (only a "reboot" clears it).
 * fix-off.sh builds it with each rule off and expects its FAIL line.
 */
#include "shim.h"
#include <errno.h>
#ifndef BUSY_H
#define BUSY_H "../src/busy.h"
#endif
#define PS3RSX_BUSY_NO_PROTOS
#include BUSY_H

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* a model of ps3fb's flag, and of exec_init / exec_fini's use of it */
static bool fb_flag;
static bool fb_released_busy(void) { return fb_flag; }
static int fb_release_busy(void) { fb_flag = true; return 0; }

/* exec_init: 0 loads, -EBUSY refuses; have = the kernel has the accessor */
static int model_init(bool have, enum busy_flag *seen)
{
	*seen = busy_flag_read(have ? fb_released_busy : NULL);
	return busy_refuse_load(*seen) ? -EBUSY : 0;
}

/* exec_fini: the GPU kept busy sets the flag where the kernel has the call */
static void model_fini(bool have, bool recovered, bool wedged, int drained)
{
	if (busy_fini_action(recovered, wedged, drained) == FINI_KEEP_BUSY && have)
		fb_release_busy();
}

int main(void)
{
	enum busy_flag seen;

	/* init, accessor present */
	fb_flag = false;
	CHECK(model_init(true, &seen) == 0 && seen == BUSY_CLEAR, "busy: a clear flag refused a load");
	fb_flag = true;
	CHECK(model_init(true, &seen) == -EBUSY && seen == BUSY_SET, "busy: a set flag didn't refuse the load");

	/* init, accessor absent: loads, and says so */
	fb_flag = true;	/* whatever the flag would be, nobody can ask */
	CHECK(model_init(false, &seen) == 0 && seen == BUSY_ABSENT, "busy: the old kernel's load refused or not told apart");

	/* exec_init's order: the exception first, then the flag; both before any allocation */
	CHECK(busy_init_refusal(0, BUSY_CLEAR) == 0 && busy_init_refusal(0, BUSY_ABSENT) == 0 &&
	      busy_init_refusal(0, BUSY_SET) == -EBUSY && busy_init_refusal(3, BUSY_CLEAR) == -EIO &&
	      busy_init_refusal(3, BUSY_SET) == -EIO, "busy: exec_init's refusals (exception, flag, order)");

	/* fini's decision */
	CHECK(busy_fini_action(false, false, 0) == FINI_GIVE_BACK, "busy: an idle GPU not handed back");
	CHECK(busy_fini_action(false, false, -ETIMEDOUT) == FINI_KEEP_BUSY, "busy: a drain timeout not kept from ps3fb");
	CHECK(busy_fini_action(false, true, 0) == FINI_KEEP_BUSY, "busy: a wedged GPU not kept from ps3fb");
	CHECK(busy_fini_action(false, true, -ETIMEDOUT) == FINI_KEEP_BUSY, "busy: a wedged, busy GPU not kept from ps3fb");
	CHECK(busy_fini_action(true, true, -ETIMEDOUT) == FINI_RECOVERED &&
	      busy_fini_action(true, false, 0) == FINI_RECOVERED, "busy: a recovered GPU is neither handed back nor flagged");
	CHECK(busy_fini_action(false, false, -EIO) == FINI_GIVE_BACK, "busy: another drain error treated as busy (as before)");

	/* end to end */
	fb_flag = false;
	model_fini(true, false, false, 0);			/* idle unload */
	CHECK(!fb_flag && model_init(true, &seen) == 0, "busy: a reload after an idle unload refused");
	model_fini(true, false, false, -ETIMEDOUT);		/* busy unload */
	CHECK(fb_flag && model_init(true, &seen) == -EBUSY, "busy: a reload after a busy unload not refused");
	model_fini(true, false, false, 0);			/* nothing clears it but a reboot */
	CHECK(model_init(true, &seen) == -EBUSY, "busy: the flag cleared by a later unload");
	fb_flag = false;					/* the old kernel: no call, no flag */
	model_fini(false, false, true, -ETIMEDOUT);
	CHECK(!fb_flag && model_init(false, &seen) == 0, "busy: the old kernel's unload set a flag");
	fb_flag = false;					/* a recovered GPU sets nothing */
	model_fini(true, true, true, -ETIMEDOUT);
	CHECK(!fb_flag, "busy: a recovered GPU's unload set the flag");

	printf("busy-test: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
