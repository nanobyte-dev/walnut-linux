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
 * Data checks: vertex format type 0 and UB256 with other than 4 components, 1-pixel-wide
 * swizzled render targets, texture offsets not 128-byte aligned, the X16 texture format,
 * SIFM surfaces.
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
	{ 0x1e94, 0x1eac }, { 0x1ee0, 0x1ee8 },	/* .., point size, params enable, sprite */
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

/*
 * The allowlist as one bit per method, built from the ranges at module load: the check runs for
 * every word of every push, and searching the ranges was the driver's largest cost in a game
 * (OpenArena, 2026-10-15). VC_NONE: no object bound, or one without methods (image from CPU).
 */
enum { VC_NONE, VC_3D, VC_M2MF, VC_SURF2D, VC_SWZ, VC_SIFM, VC_N };
static unsigned long allowed[VC_N][BITS_TO_LONGS(0x2000 / 4)];

/*
 * The validator's fast path, fast[class][method / 4]: F_OK when check_method() accepts that
 * method on that class whatever the data, and changes nothing (allowed, not a bind or host
 * method, not data-checked: fast_kind()); F_VTXFMT, F_TEXOFS and F_TEXFMT when its only check is
 * the vertex format (vtxfmt_ok()), the texture offset alignment or the texture format
 * (texfmt_ok()). Such words, nearly all of a push, cost a table lookup; a non-incrementing burst
 * to an F_OK method (inline vertex and index data) costs one for all its words. Everything else
 * goes through check_method(), so the fast path accepts nothing it wouldn't.
 */
enum { F_SLOW, F_OK, F_VTXFMT, F_TEXOFS, F_TEXFMT };
static u8 fast[VC_N][0x2000 / 4];

/*
 * A vertex format (0x1740-0x177c) the RSX fetches without a graph exception: not type 0
 * (B8G8R8A8_UNORM, which it lacks), and type 7 (U8_USCALED, libgcm's UB256) only with 4
 * components or disabled (size 0). Mesa 0043 sent UB256 with 3 components (Sauerbraten's
 * normals) and the RSX raised exception 0x106 at the draw; RPCS3 takes UB256 as size 4 only,
 * and nouveau's own table has it only as R8G8B8A8_USCALED (docs/RSX.md).
 */
static __always_inline bool vtxfmt_ok(u32 data)
{
	u32 type = data & 0xf, size = (data >> 4) & 0xf;

	return type != 0 && (type != 7 || size == 0 || size == 4);
}

/*
 * A texture format word (TEX_FORMAT, 0x1a04 + 0x20 * unit, and the vertex units' 0x904 + 0x20 *
 * unit) the RSX samples without a graph exception: not format 0x14 (nouveau's A16, libgcm's
 * X16), which nothing had sampled on the RSX before mpv's dither texture (GL_LUMINANCE16, Mesa's
 * L16_UNORM): its first draw raised exception 0x105 and wedged the RSX (2026-10-28,
 * docs/RSX.md). Mesa 0052 no longer offers the format on the PS3.
 */
#define TEXFMT_X16	0x14

static __always_inline bool texfmt_ok(u32 data)
{
	return ((data >> 8) & 0x1f) != TEXFMT_X16;
}

static __always_inline bool fast_ok(u8 f, u32 data)
{
	return f == F_OK || (f == F_VTXFMT && vtxfmt_ok(data)) || (f == F_TEXOFS && !(data & 127)) ||
	       (f == F_TEXFMT && texfmt_ok(data));
}

static void allow(int c, const struct mrange *r, int n)
{
	u32 m;
	int i;

	for (i = 0; i < n; i++)
		for (m = r[i].lo; m <= r[i].hi; m += 4)
			__set_bit(m / 4, allowed[c]);
}

static u8 fast_kind(int c, u32 m);

void ps3rsx_validate_init(void)
{
	int c;
	u32 m;

	allow(VC_3D, m_3d, ARRAY_SIZE(m_3d));
	allow(VC_M2MF, m_m2mf, ARRAY_SIZE(m_m2mf));
	allow(VC_SURF2D, m_surf2d, ARRAY_SIZE(m_surf2d));
	allow(VC_SWZ, m_swz, ARRAY_SIZE(m_swz));
	allow(VC_SIFM, m_sifm, ARRAY_SIZE(m_sifm));
	for (c = VC_NONE + 1; c < VC_N; c++)
		for (m = 0x100; m < 0x2000; m += 4)
			fast[c][m / 4] = test_bit(m / 4, allowed[c]) ? fast_kind(c, m) : F_SLOW;
}

/* The validator class of an object handle */
static u8 vclass(u32 handle)
{
	switch (handle) {
	case 0x31337000: return VC_3D;
	case 0x31337303:
	case 0x3137c0de: return VC_M2MF;
	case 0x313371c3: return VC_SURF2D;
	case 0x31337a73: return VC_SWZ;
	case 0x3137af00: return VC_SIFM;
	default:	 return VC_NONE;	/* image-from-CPU: Mesa doesn't use it */
	}
}

/* Is method m (at most 0x1ffc) allowed on the object with this handle? */
static bool method_allowed(u32 handle, u32 m)
{
	return test_bit(m / 4, allowed[vclass(handle)]);
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

/* A 3D texture offset (fragment units 0-15, vertex units 0-3)? */
static bool tex_offset(u32 m)
{
	return (m & ~0x1e0) == 0x1a00 || (m & ~0x60) == 0x0900;
}

/* A 3D texture format word (fragment units 0-15, vertex units 0-3)? */
static bool tex_format(u32 m)
{
	return (m & ~0x1e0) == 0x1a04 || (m & ~0x60) == 0x0904;
}

/*
 * How the fast path may take allowed method m of class c, from what check_method() does with
 * it (keep in step: a data check missing here would be skipped).
 */
static u8 fast_kind(int c, u32 m)
{
	if (m >= 0x180 && m <= 0x1bc)
		return F_SLOW;			/* context DMAs; SIFM 0x198 surface */
	if (c != VC_3D)
		return F_OK;
	if (m == 0x1808 || m == 0x208)
		return F_SLOW;			/* BEGIN_END state, RT_FORMAT */
	if (m >= 0x1740 && m <= 0x177c)
		return F_VTXFMT;		/* vertex format: vtxfmt_ok() */
	if (tex_offset(m))
		return F_TEXOFS;		/* texture offset alignment */
	if (tex_format(m))
		return F_TEXFMT;		/* texture format: texfmt_ok() */
	return F_OK;
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
		vs->cls[subch] = vclass(data);
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
	if (vs->bound[subch] == 0x31337000 && mthd == 0x1808)
		vs->prim_open = data != 0;	/* BEGIN_END: Mesa splits draws across pushes */
	if (vs->bound[subch] == 0x31337000 && mthd >= 0x1740 && mthd <= 0x177c &&
	    !vtxfmt_ok(data)) {
		/* vertex format type 0 raises a graph exception on the RSX (quirks test), and so
		 * did UB256 with 3 components (0x106, Mesa 0043): vtxfmt_ok() */
		*why = (data & 0xf) ? "vertex format UB256 with other than 4 components" :
				      "vertex format type 0";
		return -EINVAL;
	}
	if (vs->bound[subch] == 0x31337000 && mthd == 0x208 &&
	    (data & 0xf00) == 0x200 && !(data & 0x00ff0000)) {
		/* RT_FORMAT swizzled with log2 width 0: rendering to a 1-pixel-wide swizzled
		 * surface raises graph exception 0x106 (docs/RSX.md) */
		*why = "swizzled render target 1 pixel wide";
		return -EINVAL;
	}
	if (vs->bound[subch] == 0x31337000 && tex_offset(mthd) && (data & 127)) {
		/* a texture sampled at an offset only 32-byte aligned raised graph exception
		 * 0x105 (Neverball, +0xaa0); libgcm aligns texture offsets to 128 bytes, and Mesa
		 * 0014 never sends anything else (docs/GAME-PLAN.md, driver bugs) */
		*why = "texture offset not 128-byte aligned";
		return -EINVAL;
	}
	if (vs->bound[subch] == 0x31337000 && tex_format(mthd) && !texfmt_ok(data)) {
		/* the X16 (A16) format: mpv's first draw sampling one raised graph exception 0x105
		 * (texfmt_ok()) */
		*why = "texture format X16 (A16)";
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

/*
 * Checks a push. *vs follows the push's binds and primitives as it goes: the caller keeps it,
 * and replays the pushes into the channel's shadow (ps3rsx_shadow_push()), only once the whole
 * submit passed and went to the GPU, so a rejected push changes nothing.
 */
int ps3rsx_validate(struct ps3rsx_vstate *vs, const u32 *w, u32 nwords)
{
	const char *why = NULL;
	u32 i = 0, k;
	int ret;

	while (i < nwords) {
		u32 h = w[i], count, subch, mthd;
		const u32 *data = &w[i + 1];
		const u8 *ok;
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
		ok = fast[vs->cls[subch]];
		if (noninc) {
			/* the same method for every word: one lookup when its data isn't checked */
			u8 f = ok[mthd / 4];

			if (f != F_OK) {
				for (k = 0; k < count; k++) {
					if (fast_ok(f, data[k]))
						continue;
					ret = check_method(vs, subch, mthd, data[k], &why);
					if (ret)
						goto bad;
					f = fast[vs->cls[subch]][mthd / 4];	/* a bind */
				}
			}
		} else {
			for (k = 0; k < count; k++) {
				u32 m = mthd + 4 * k;

				if (m > 0x1ffc) {
					why = "method burst past 0x1ffc";
					goto bad;
				}
				if (fast_ok(ok[m / 4], data[k]))
					continue;
				ret = check_method(vs, subch, m, data[k], &why);
				if (ret)
					goto bad;
				ok = fast[vs->cls[subch]];	/* a bind changes the class */
			}
		}
		i += 1 + count;
	}
	return 0;
bad:
	pr_warn("ps3rsx: rejected push: %s at word %u (%08x)\n", why, i, w[i]);
	return -EINVAL;
}
