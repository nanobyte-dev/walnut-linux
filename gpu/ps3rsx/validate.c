// SPDX-License-Identifier: GPL-2.0
/*
 * Command stream checks before anything reaches the ring: a single bad command wedges the RSX
 * until the console reboots (docs/RSX.md), so user pushes are decoded word by word.
 *
 * Enforced: only plain method headers (no JMP/CALL/RETURN, whose targets we don't control),
 * complete bursts, object binds (method 0) only with the hypervisor's handles, context DMA
 * slots (0x180-0x1bc) only with its known context DMAs, and a per-class method allowlist: the
 * methods Mesa's nv30 driver can emit (docs/MESA-METHODS.md section 4, indexed arrays
 * expanded to all units) plus what our raw tests use, all of which ran on the RSX.
 * Not yet: bounds checks of offsets against BOs at trigger methods.
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

/* Allowed method ranges [lo, hi] per class */
struct mrange { u16 lo, hi; };

static const struct mrange m_3d[] = {
	{ 0x0100, 0x0100 }, { 0x0110, 0x0110 },	/* NOP, wait for idle */
	{ 0x0180, 0x01bc },			/* context DMAs (values checked) */
	{ 0x0200, 0x022c }, { 0x0280, 0x028c },	/* surfaces */
	{ 0x02b8, 0x02c4 },			/* window offset */
	{ 0x0300, 0x03bc },			/* blend, stencil, depth, line, control0 */
	{ 0x08c0, 0x08c4 }, { 0x08e4, 0x08e4 },	/* scissor, fragment program */
	{ 0x0900, 0x097c },			/* vertex texture units */
	{ 0x0a00, 0x0a7c },			/* viewport, polygon offset, depth func */
	{ 0x0b00, 0x0b7c },			/* texture control2, tex coord control */
	{ 0x0b80, 0x0bfc },			/* vertex program upload window */
	{ 0x142c, 0x142c }, { 0x1450, 0x1454 }, { 0x1478, 0x14fc },
	{ 0x1500, 0x15fc },			/* immediate 3F attributes */
	{ 0x1680, 0x16bc },			/* vertex buffer offsets */
	{ 0x1710, 0x1718 },			/* vertex cache invalidate, pipe nop */
	{ 0x173c, 0x177c },			/* base index, vertex formats */
	{ 0x17c8, 0x17cc },			/* report reset, zpass enable */
	{ 0x1800, 0x183c },			/* report, begin/end, elements, inline, index, poly mode, cull */
	{ 0x1840, 0x187c },			/* texture control3 */
	{ 0x1880, 0x19fc },			/* immediate 2F/2S/4UB/4S attributes */
	{ 0x1a00, 0x1bfc },			/* texture units */
	{ 0x1c00, 0x1cfc },			/* immediate 4F attributes */
	{ 0x1d60, 0x1d64 }, { 0x1d6c, 0x1d7c }, { 0x1d88, 0x1d94 },
	{ 0x1da4, 0x1da4 }, { 0x1dac, 0x1db8 },
	{ 0x1e40, 0x1e7c },			/* immediate 1F attributes */
	{ 0x1e94, 0x1eac }, { 0x1ee0, 0x1ee0 }, { 0x1ee8, 0x1ee8 },
	{ 0x1ef8, 0x1f7c },			/* transform timeout, constants */
	{ 0x1fc4, 0x1fd8 }, { 0x1ff0, 0x1ff4 },
};
static const struct mrange m_m2mf[] = {
	{ 0x0100, 0x0100 }, { 0x0180, 0x0188 }, { 0x030c, 0x0328 },
};
static const struct mrange m_surf2d[] = {
	{ 0x0180, 0x0188 }, { 0x0300, 0x030c },
};
static const struct mrange m_swz[] = {
	{ 0x0180, 0x0184 }, { 0x0300, 0x0304 },
};
static const struct mrange m_sifm[] = {
	{ 0x0180, 0x0184 }, { 0x0198, 0x0198 }, { 0x02fc, 0x031c }, { 0x0400, 0x040c },
};

static bool in_ranges(const struct mrange *r, int n, u32 m)
{
	int i;

	for (i = 0; i < n; i++)
		if (m >= r[i].lo && m <= r[i].hi)
			return true;
	return false;
}

/* Is method m allowed on the object with this handle? */
static bool method_allowed(u32 handle, u32 m)
{
	switch (handle) {
	case 0x31337000: return in_ranges(m_3d, ARRAY_SIZE(m_3d), m);
	case 0x31337303:
	case 0x3137c0de: return in_ranges(m_m2mf, ARRAY_SIZE(m_m2mf), m);
	case 0x313371c3: return in_ranges(m_surf2d, ARRAY_SIZE(m_surf2d), m);
	case 0x31337a73: return in_ranges(m_swz, ARRAY_SIZE(m_swz), m);
	case 0x3137af00: return in_ranges(m_sifm, ARRAY_SIZE(m_sifm), m);
	default:	 return false;	/* image-from-CPU: Mesa doesn't use it */
	}
}

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
	if (mthd < 0x100) {
		/* host methods: SET_REFERENCE, semaphore context/offset/release. Not the FIFO
		 * semaphore acquire (0x68): it can block forever (docs/RSX.md) */
		if (mthd == 0x50 || mthd == 0x60 || mthd == 0x64 || mthd == 0x6c)
			return 0;
		*why = "host method not allowed";
		return -EINVAL;
	}
	if (!vs->bound[subch]) {
		*why = "method on a subchannel with no object bound in this channel";
		return -EINVAL;
	}
	if (!method_allowed(vs->bound[subch], mthd)) {
		*why = "method not in the allowlist for this object";
		return -EINVAL;
	}
	if (vs->bound[subch] == 0x31337000 && mthd >= 0x1740 && mthd <= 0x177c &&
	    (data & 0xf) == 0) {
		/* vertex format type 0 raises a graph exception on the RSX (quirks test) */
		*why = "vertex format type 0";
		return -EINVAL;
	}
	if (vs->bound[subch] == 0x3137af00 && mthd == 0x198) {
		/* SIFM surface: an object (surface 2D or swizzled), not a context DMA */
		if (data != 0x313371c3 && data != 0x31337a73) {
			*why = "SIFM surface is not a surface object";
			return -EINVAL;
		}
		return 0;
	}
	if (mthd >= 0x180 && mthd <= 0x1bc && !known_dma(data)) {
		*why = "unknown context DMA handle";
		return -EINVAL;
	}
	return 0;
}

int ps3rsx_validate(struct ps3rsx_vstate *chan_vs, const u32 *w, u32 nwords)
{
	struct ps3rsx_vstate tmp = *chan_vs, *vs = &tmp;	/* kept only if the push passes */
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
	*chan_vs = tmp;
	return 0;
bad:
	pr_warn("ps3rsx: rejected push: %s at word %u (%08x)\n", why, i, w[i]);
	return -EINVAL;
}
