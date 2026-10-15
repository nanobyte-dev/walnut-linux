// SPDX-License-Identifier: GPL-2.0
/*
 * Kernel-submitted GPU copies (M2MF) between client submits, for the VRAM swap (vswap.c): the
 * PPE reads VRAM at 10 MB/s, the RSX copies VRAM to GART at ~600 MB/s (docs/RSX.md).
 * Design: docs/RSX-SPU-FEASIBILITY.md section 3.
 *
 * The swap path must never wait for anything a submitter might hold while it reclaims memory
 * (a submitter allocates under exec_lock), so every lock here is a trylock and every failure
 * means "use the PPE instead":
 *  - the GPU must already be ours (own_lock, gpu_owned, no console handover pending), held
 *    for the copy like a submit (submitting++), so ps3fb can't take it back meanwhile;
 *  - exec_lock, and no channel with a primitive open (no method may go between BEGIN and END);
 *  - context switching on (ctxsw) and not a dry run.
 * The copy uses Mesa's M2MF object on a subchannel Mesa leaves free, records every word it
 * emits in hw_shadow and makes the next client submit restore its own state (last_chan = -1),
 * so no client ever sees our M2MF settings. No dma_fence: a bare SET_REFERENCE, polled.
 */
#include <linux/delay.h>
#include <linux/ktime.h>

#include "ps3rsx.h"

#define SC_K		6		/* Mesa nv30 uses 2-5 and 7 (nv30_winsys.h) */
#define M2MF		0x31337303	/* the M2MF object Mesa binds (same per-object state) */
#define DMA_NOTIFY	0x66604200
#define DMA_VRAM	0xfeed0000
#define DMA_GART	0xfeed0001

/* Take the GPU for a kernel copy without waiting; 0, or -EAGAIN (use the PPE) */
int ps3rsx_kcopy_begin(struct ps3rsx_device *rsx)
{
	if (!ps3rsx_kcopy_allowed() || READ_ONCE(rsx->wedged))
		return -EAGAIN;
	if (!mutex_trylock(&rsx->own_lock))
		return -EAGAIN;
	if (!rsx->gpu_owned || rsx->console_pending) {
		mutex_unlock(&rsx->own_lock);
		return -EAGAIN;
	}
	rsx->submitting++;			/* the console can't take the GPU back meanwhile */
	mutex_unlock(&rsx->own_lock);
	if (!mutex_trylock(&rsx->exec_lock))
		goto unhold;
	if (READ_ONCE(rsx->prim_owner)) {
		mutex_unlock(&rsx->exec_lock);
		goto unhold;
	}
	return 0;
unhold:
	ps3rsx_console_unhold(rsx);
	return -EAGAIN;
}

/* Emit n words for method m on SC_K, mirrored into hw_shadow. exec_lock held. */
static void kput(struct ps3rsx_device *rsx, u32 **p, u32 m, const u32 *v, u32 n)
{
	u32 i;

	*(*p)++ = (n << 18) | (SC_K << 13) | m;
	for (i = 0; i < n; i++) {
		*(*p)++ = v[i];
		ps3rsx_shadow_write(rsx->hw_shadow, SC_K, m ? m + 4 * i : 0, v[i]);
	}
}

/*
 * Queue copies VRAM -> GART (n runs, each npages pages of 4 KiB, contiguous on both sides) and
 * return the reference to wait for. Between ps3rsx_kcopy_begin() and _end().
 */
int ps3rsx_kcopy_to_gart(struct ps3rsx_device *rsx, const struct kcopy_run *r, unsigned int n,
			 u32 *seq)
{
	static const u32 obj = M2MF, dma[3] = { DMA_NOTIFY, DMA_VRAM, DMA_GART };
	u32 len = 4 * (2 + 4 + 9 * n), *p, *start;
	unsigned int i;
	int err;

	start = p = ps3rsx_ring_begin(rsx, len, &err);
	if (!p)
		return err;
	ps3rsx_hw_shadow_sync(rsx);		/* hw_shadow = what the GPU holds before our words */
	kput(rsx, &p, 0x000, &obj, 1);
	kput(rsx, &p, 0x180, dma, 3);		/* notify, buffer in = VRAM, out = GART */
	for (i = 0; i < n; i++) {
		/* offset in/out, pitch in/out, line length, line count, format, notify (launch) */
		u32 v[8] = { r[i].vram, r[i].gart, PAGE_SIZE, PAGE_SIZE, PAGE_SIZE, r[i].npages,
			     0x101, 0 };

		kput(rsx, &p, 0x30c, v, 8);
	}
	*seq = ps3rsx_ring_commit_seq(rsx, (p - start) * 4);
	rsx->last_chan = -1;			/* the next client submit restores its own state */
	return 0;
}

void ps3rsx_kcopy_end(struct ps3rsx_device *rsx)
{
	mutex_unlock(&rsx->exec_lock);
	ps3rsx_console_unhold(rsx);
}

/* Wait (sleeping) until the GPU has passed seq: 0, -EIO if it wedged, -ETIMEDOUT */
int ps3rsx_kcopy_wait(struct ps3rsx_device *rsx, u32 seq, unsigned int timeout_us)
{
	ktime_t end = ktime_add_us(ktime_get(), timeout_us);
	int spins = 50;

	while (!ps3rsx_seq_done(rsx, seq)) {
		if (READ_ONCE(rsx->wedged))
			return -EIO;
		if (ktime_after(ktime_get(), end))
			return -ETIMEDOUT;
		if (spins-- > 0)
			udelay(2);
		else
			usleep_range(20, 50);
	}
	rmb();				/* the copied data before our reads of it */
	return 0;
}
