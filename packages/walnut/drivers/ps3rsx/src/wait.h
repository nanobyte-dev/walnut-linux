/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A wait for ps3rsx fences as userspace gets it (GEM_PUSHBUF's SYNC, GEM_CPU_PREP, which Mesa's
 * fence wait uses). waited: the wait's result (jiffies left, 0 timed out, < 0 interrupted);
 * error: an error a waited-for fence ended with (dma_fence_get_status() < 0), or 0, read after
 * the wait. A wedge ends every pending fence at once with -EIO (exec.c, fences_update()), so the
 * wait returns with jiffies left: without the error, userspace took work the GPU never ran as
 * done (rsxdrm-smoke quirks, session M, 2026-11-01: the push that wedged the RSX got 0).
 * restartable: an interrupted wait goes back to userspace to be restarted (CPU_PREP); otherwise
 * it is -EBUSY like a timeout (PUSHBUF: a restart would submit the pushes again).
 * Userspace test: test/validate-test.c, wait_cases().
 */
#ifndef PS3RSX_WAIT_H
#define PS3RSX_WAIT_H

static inline int ps3rsx_wait_result(long waited, int error, bool restartable)
{
	if (waited < 0 && restartable)
		return waited;
	if (waited <= 0)
		return -EBUSY;
	return error < 0 ? error : 0;
}

#endif
