/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The "released busy" flag (ps3fb, kernel local/0057) as exec.c uses it, as pure code so test/busy-test.c
 * builds it on the host.
 *
 * ps3fb keeps the lv1 GPU context across loads of this module. When exec_fini gives the GPU up with a job
 * or a stall behind it ("keeping it from ps3fb until reboot"), PGRAPH may still run the old job, and a
 * later load's FIFO_SETUP would run under it. ps3fb remembers that (ps3fb_gpu_release_busy); exec_init asks
 * (ps3fb_gpu_released_busy) and refuses while it is set. Only a reboot clears it.
 *
 * The two calls exist only in kernels with local/0057. The stable kernel's modules are built without them
 * and must still load, so exec.c looks them up with symbol_get() at run time (no link-time dependency, no
 * entry in the module's undefined symbols) and a missing one means "no flag": the check is absent, logged
 * once at load, and tools/rsx-ab's rsx-arm.sh remains the guard there. The prototypes are declared here
 * too, as the old kernel's ps3gpu.h lacks them (the same declaration as the new one).
 */
#ifndef PS3RSX_BUSY_H
#define PS3RSX_BUSY_H

#ifndef PS3RSX_BUSY_NO_PROTOS
int ps3fb_gpu_release_busy(void);
bool ps3fb_gpu_released_busy(void);
#endif

enum busy_flag { BUSY_ABSENT, BUSY_CLEAR, BUSY_SET };

/* what the flag says; released is the looked-up ps3fb_gpu_released_busy, NULL when the kernel lacks it */
static inline enum busy_flag busy_flag_read(bool (*released)(void))
{
	if (!released)
		return BUSY_ABSENT;
	return released() ? BUSY_SET : BUSY_CLEAR;
}

/* exec_init: only a set flag refuses; an absent one is the old kernel and loads as before */
static inline bool busy_refuse_load(enum busy_flag f)
{
	return f == BUSY_SET;
}

/*
 * exec_init's refusals, checked before anything is allocated (a refused insmod leaks nothing): a graph
 * exception since boot (-EIO) first, then the released-busy flag (-EBUSY); 0 loads.
 */
static inline int busy_init_refusal(unsigned int exceptions, enum busy_flag f)
{
	if (exceptions)
		return -EIO;
	return busy_refuse_load(f) ? -EBUSY : 0;
}

enum fini_action { FINI_RECOVERED, FINI_GIVE_BACK, FINI_KEEP_BUSY };

/*
 * exec_fini: what becomes of the GPU. recovered: a recovery ran in this boot (it stays with ps3rsx).
 * Otherwise it goes back to ps3fb only when the GPU is not wedged and the ring drained (drained is
 * exec_drain's result; -ETIMEDOUT: still busy). Anything else is kept from ps3fb, and that is when the
 * flag must be set.
 */
static inline enum fini_action busy_fini_action(bool recovered, bool wedged, int drained)
{
	if (recovered)
		return FINI_RECOVERED;
	if (!wedged && drained != -ETIMEDOUT)
		return FINI_GIVE_BACK;
	return FINI_KEEP_BUSY;
}

#endif /* PS3RSX_BUSY_H */
