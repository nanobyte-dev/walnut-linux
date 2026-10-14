// SPDX-License-Identifier: GPL-2.0
/*
 * Command stream checks before anything reaches the ring: a single bad command wedges the RSX
 * until the console reboots (docs/RSX.md), so user pushes are decoded word by word.
 *
 * Enforced now: only plain method headers (no JMP/CALL/RETURN, whose targets we don't
 * control), complete bursts, object binds (method 0) only with the hypervisor's handles, and
 * context DMA slots (0x180-0x1bc) only with its known context DMAs.
 * Still to come before Mesa may submit (docs/DRIVER-PLAN.md): the per-class method allowlist
 * generated from docs/MESA-METHODS.md and bounds checks at trigger methods.
 */
#include <linux/kernel.h>

#include "ps3rsx.h"

static const u32 object_handles[] = {
	0x31337000,	/* 3D (0x4097) */
	0x31337303,	/* M2MF (0x0039), XDR -> VRAM */
	0x3137c0de,	/* M2MF (0x0039), VRAM -> XDR */
	0x313371c3,	/* surface 2D (0x3062) */
	0x31337a73,	/* swizzled surface (0x309e) */
	0x31337808,	/* image from CPU (0x308a) */
	0x3137af00,	/* scaled image (0x3089) */
};

static const u32 dma_handles[] = {
	0,		/* unused slot */
	0xfeed0000,	/* VRAM */
	0xfeed0001,	/* XDR (GART, IO window) */
	0x66604200,	/* notifier */
	0x66606660,	/* semaphores (3D) */
	0x66616661,	/* semaphores (FIFO) */
	0x66626660,	/* reports */
};

bool ps3rsx_known_object(u32 handle)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(object_handles); i++)
		if (object_handles[i] == handle)
			return true;
	return false;
}

static bool known_dma(u32 handle)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dma_handles); i++)
		if (dma_handles[i] == handle)
			return true;
	return false;
}

/* One method write: returns 0 or a negative errno (the caller logs where) */
static int check_method(struct ps3rsx_vstate *vs, u32 subch, u32 mthd, u32 data, const char **why)
{
	if (mthd == 0) {
		if (!ps3rsx_known_object(data)) {
			*why = "bind of an unknown object handle";
			return -EINVAL;
		}
		vs->bound[subch] = data;
		return 0;
	}
	if (mthd < 0x100)
		return 0;		/* host (FIFO) methods: reference, semaphores, NOP */
	if (!vs->bound[subch]) {
		*why = "method on a subchannel with no object bound in this channel";
		return -EINVAL;
	}
	if (mthd >= 0x180 && mthd <= 0x1bc && !known_dma(data)) {
		*why = "unknown context DMA handle";
		return -EINVAL;
	}
	return 0;
}

int ps3rsx_validate(struct ps3rsx_vstate *vs, const u32 *w, u32 nwords)
{
	const char *why = NULL;
	u32 i = 0, k;
	int ret;

	while (i < nwords) {
		u32 h = w[i], count, subch, mthd;
		bool noninc;

		if (h == 0) {		/* NOP header */
			i++;
			continue;
		}
		if ((h & 0xe0030003) == 0) {
			noninc = false;
		} else if ((h & 0xe0030003) == 0x40000000) {
			noninc = true;
		} else {
			why = "jump/call/return or malformed header";
			goto bad;
		}
		count = (h >> 18) & 0x7ff;
		subch = (h >> 13) & 7;
		mthd = h & 0x1ffc;
		if (i + 1 + count > nwords) {
			why = "burst runs past the end of the push";
			goto bad;
		}
		for (k = 0; k < count; k++) {
			u32 m = noninc ? mthd : mthd + 4 * k;

			if (m > 0x1ffc) {
				why = "method burst past 0x1ffc";
				goto bad;
			}
			ret = check_method(vs, subch, m, w[i + 1 + k], &why);
			if (ret)
				goto bad;
		}
		i += 1 + count;
	}
	return 0;
bad:
	pr_warn("ps3rsx: rejected push: %s at word %u (%08x)\n", why, i, w[i]);
	return -EINVAL;
}
