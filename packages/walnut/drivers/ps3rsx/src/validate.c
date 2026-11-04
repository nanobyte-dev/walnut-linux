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
 * swizzled render targets, RT_ENABLE other than Mesa's values (rt_enable_ok()), fragment
 * texture offsets not 128-byte aligned, the X16 texture format, every word of the vertex
 * texture units (vtxtex_ok()), SIFM surfaces.
 * Order checks: BEGIN only with no primitive open, END and vertex data only inside one (the
 * channel's primitive state carries over from its accepted submits: Mesa splits draws).
 * Address checks (p4_method()): each context DMA slot takes only its own handles; semaphore
 * releases, reports and the render-enable read stay in the channel's own label and query slice;
 * M2MF and scaled-image copies and clears stay inside buffers the submitting file holds handles
 * to (the set, p4.h), with every field they use set by the channel itself. Not yet: draws.
 *
 * Every check reads only the push's own words and the channel's validator state, which only
 * accepted submits change: a rejected submit leaves nothing behind, so the streams that reach
 * the GPU, laid end to end, pass these checks as one stream (docs/DRIVER.md, recovery).
 */
#include <linux/kernel.h>
#ifdef __KERNEL__
#include <linux/moduleparam.h>
#include <linux/overflow.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#endif

#include "ps3rsx.h"
#include "p4.h"

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

/* --- the address checks (P4): docs/DRIVER.md, the validator ------------------------------- */

#define H_NOTIFY	0x66604200
#define H_SEM		0x66606660
#define H_REPORT	0x66626660
#define H_VRAM		0xfeed0000
#define H_GART		0xfeed0001
#define P4_OBJ_3D		0x31337000
#define P4_OBJ_SURF2D	0x313371c3
#define P4_OBJ_SWZ		0x31337a73

/* The fields' methods (p4.h's enum p4_field): class and method, the table the checks, the
 * shadow coverage test and the counters' log share */
struct p4_fm { u8 cls; u16 m; };

static const struct p4_fm p4_fields[P4F_N] = {
#define DMA3(i) [P4F_3D_DMA + (i)] = { VC_3D, 0x180 + 4 * (i) }
	DMA3(0), DMA3(1), DMA3(2), DMA3(3), DMA3(4), DMA3(5), DMA3(6), DMA3(7),
	DMA3(8), DMA3(9), DMA3(10), DMA3(11), DMA3(12), DMA3(13), DMA3(14), DMA3(15),
#undef DMA3
	[P4F_FENCE] = { VC_3D, 0x1d6c },
	[P4F_RT_HORIZ] = { VC_3D, 0x200 }, [P4F_RT_VERT] = { VC_3D, 0x204 },
	[P4F_RT_FORMAT] = { VC_3D, 0x208 }, [P4F_C0_PITCH] = { VC_3D, 0x20c },
	[P4F_C0_OFF] = { VC_3D, 0x210 }, [P4F_Z_OFF] = { VC_3D, 0x214 },
	[P4F_C1_OFF] = { VC_3D, 0x218 }, [P4F_C1_PITCH] = { VC_3D, 0x21c },
	[P4F_RT_ENABLE] = { VC_3D, 0x220 }, [P4F_Z_PITCH] = { VC_3D, 0x22c },
	[P4F_C2_PITCH] = { VC_3D, 0x280 }, [P4F_C3_PITCH] = { VC_3D, 0x284 },
	[P4F_C2_OFF] = { VC_3D, 0x288 }, [P4F_C3_OFF] = { VC_3D, 0x28c },
	[P4F_WIN_OFF] = { VC_3D, 0x2b8 }, [P4F_WIN_MODE] = { VC_3D, 0x2bc },
	[P4F_WIN_H] = { VC_3D, 0x2c0 }, [P4F_WIN_V] = { VC_3D, 0x2c4 },
	[P4F_COORD] = { VC_3D, 0x1d88 },
	[P4F_M_NOTIFY] = { VC_M2MF, 0x180 }, [P4F_M_IN_DMA] = { VC_M2MF, 0x184 },
	[P4F_M_OUT_DMA] = { VC_M2MF, 0x188 }, [P4F_M_OFF_IN] = { VC_M2MF, 0x30c },
	[P4F_M_OFF_OUT] = { VC_M2MF, 0x310 }, [P4F_M_PITCH_IN] = { VC_M2MF, 0x314 },
	[P4F_M_PITCH_OUT] = { VC_M2MF, 0x318 }, [P4F_M_LEN] = { VC_M2MF, 0x31c },
	[P4F_M_COUNT] = { VC_M2MF, 0x320 }, [P4F_M_FORMAT] = { VC_M2MF, 0x324 },
	[P4F_S_NOTIFY] = { VC_SURF2D, 0x180 }, [P4F_S_SRC_DMA] = { VC_SURF2D, 0x184 },
	[P4F_S_DST_DMA] = { VC_SURF2D, 0x188 }, [P4F_S_FORMAT] = { VC_SURF2D, 0x300 },
	[P4F_S_PITCH] = { VC_SURF2D, 0x304 }, [P4F_S_OFF_SRC] = { VC_SURF2D, 0x308 },
	[P4F_S_OFF_DST] = { VC_SURF2D, 0x30c },
	[P4F_W_NOTIFY] = { VC_SWZ, 0x180 }, [P4F_W_DMA] = { VC_SWZ, 0x184 },
	[P4F_W_FORMAT] = { VC_SWZ, 0x300 }, [P4F_W_OFF] = { VC_SWZ, 0x304 },
	[P4F_F_NOTIFY] = { VC_SIFM, 0x180 }, [P4F_F_DMA] = { VC_SIFM, 0x184 },
	[P4F_F_SURFACE] = { VC_SIFM, 0x198 }, [P4F_F_FORMAT] = { VC_SIFM, 0x300 },
	[P4F_F_CLIP_PT] = { VC_SIFM, 0x308 }, [P4F_F_CLIP_SZ] = { VC_SIFM, 0x30c },
	[P4F_F_OUT_PT] = { VC_SIFM, 0x310 }, [P4F_F_OUT_SZ] = { VC_SIFM, 0x314 },
	[P4F_F_DUDX] = { VC_SIFM, 0x318 }, [P4F_F_DVDY] = { VC_SIFM, 0x31c },
	[P4F_F_SIZE] = { VC_SIFM, 0x400 }, [P4F_F_IN_FMT] = { VC_SIFM, 0x404 },
	[P4F_F_OFF] = { VC_SIFM, 0x408 },
};

/* field_of[class][method / 4]: its field + 1, or 0 (built at load) */
static u8 field_of[VC_N][0x2000 / 4];

/* Methods that make the GPU touch memory (their own words, or through the fields), and 3D's
 * 0x224 and 0x228, which no source names (only 0 passes) */
static bool p4_trigger(int c, u32 m)
{
	switch (c) {
	case VC_3D:
		return m == 0x1d70 || m == 0x1d74 || m == 0x1800 || m == 0x1e98 || m == 0x1d94 ||
		       m == 0x224 || m == 0x228;
	case VC_M2MF:
		return m == 0x328;
	case VC_SIFM:
		return m == 0x40c;
	}
	return false;
}

/*
 * The context DMA each slot takes (6.1 of the design): a mask over dma_handles[]. The texture,
 * vertex buffer, semaphore and report slots have one handle each, so a location bit or an
 * offset names one domain or one area; the render target slots and the copy objects' buffers
 * take VRAM or GART; the notifiers only the notifier; slots nothing reads through only 0.
 */
#define DM(i)		(1u << (i))
#define D_0		DM(0)
#define D_VRAM		DM(1)
#define D_GART		DM(2)
#define D_NOTIFY	DM(3)
#define D_SEM		DM(4)
#define D_REPORT	DM(6)

static u8 slot_allow(int c, u32 m)
{
	if (c == VC_3D) {
		switch (m) {
		case 0x180: return D_NOTIFY | D_0;
		case 0x184: return D_VRAM;		/* textures and programs, location 1 */
		case 0x188: return D_GART;		/* location 2 */
		case 0x18c: case 0x194: case 0x198: case 0x1b4: case 0x1b8:
			return D_VRAM | D_GART;		/* render targets and zeta */
		case 0x19c: return D_VRAM;		/* vertex and index buffers, location 0 */
		case 0x1a0: return D_GART;		/* location 1 */
		case 0x1a4: return D_SEM;
		case 0x1a8: return D_REPORT;
		default:    return D_0;		/* 0x190, 0x1ac, 0x1b0, 0x1bc */
		}
	}
	if (m == 0x180)
		return D_NOTIFY;
	if (m == 0x184 || (m == 0x188 && (c == VC_M2MF || c == VC_SURF2D)))
		return D_VRAM | D_GART;
	return 0;
}

static int dma_index(u32 handle)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dma_handles); i++)
		if (dma_handles[i] == handle)
			return i;
	return -1;
}

/* The count-only build: a P4 rule that fails counts and lets the word through, per slice */
#ifdef PS3RSX_P4_COUNT
#define P4S_A	1u
#define P4S_B	2u
#define P4S_P7	4u
static const u8 p4_rule_slice[P4R_N] = {
	[P4R_BIND] = P4S_A, [P4R_SLOT] = P4S_A, [P4R_FENCE] = P4S_A, [P4R_QUERY] = P4S_A,
	[P4R_RENDER] = P4S_A, [P4R_UNSET] = P4S_B, [P4R_FIELD] = P4S_B, [P4R_M2MF] = P4S_B,
	[P4R_SIFM] = P4S_B, [P4R_CLEAR] = P4S_B, [P4R_P7] = P4S_P7,
};
static const char *const p4_rule_name[P4R_N] = {
	"bind", "slot", "fence", "query", "render-enable", "unset", "field", "m2mf", "sifm",
	"clear", "p7",
};
#ifdef __KERNEL__
static unsigned int test_p4_enforce;
module_param(test_p4_enforce, uint, 0644);
MODULE_PARM_DESC(test_p4_enforce, "TEST BUILD: address check slices that refuse (1 a, 2 b, 4 P7); "
		 "the others only count (debugfs ps3rsx_p4)");
#else
static unsigned int test_p4_enforce;	/* the host tests set it */
#endif
static u64 p4_hits[P4R_N];
static u32 p4_last_m[P4R_N], p4_last_data[P4R_N];

static void p4_count(int rule, u32 m, u32 data, const char *why)
{
	if (p4_hits[rule]++ < 16) {
#ifdef __KERNEL__
		pr_info("ps3rsx: TEST BUILD: address check %s would refuse: %s (method %#x data %#x, "
			"pid %d, %s)\n", p4_rule_name[rule], why, m, data, task_pid_nr(current),
			current->comm);
#endif
	}
	p4_last_m[rule] = m;
	p4_last_data[rule] = data;
}

#ifdef __KERNEL__
/* debugfs dri/N/ps3rsx_p4: what each rule would have refused */
void ps3rsx_p4_show(struct seq_file *sm)
{
	int r;

	seq_printf(sm, "enforcing: %#x (1 a, 2 b, 4 P7)\n", READ_ONCE(test_p4_enforce));
	for (r = 0; r < P4R_N; r++)
		seq_printf(sm, "%-14s %10llu  last method %#06x data %#010x\n", p4_rule_name[r],
			   p4_hits[r], p4_last_m[r], p4_last_data[r]);
}
#endif
#endif

static int p4_fail(int rule, u32 m, u32 data, const char *why, const char **out)
{
#ifdef PS3RSX_P4_COUNT
	if (!(READ_ONCE(test_p4_enforce) & p4_rule_slice[rule])) {
		p4_count(rule, m, data, why);
		return 0;
	}
#endif
	*out = why;
	return -EINVAL;
}

static __always_inline bool fvalid(const struct ps3rsx_vstate *vs, int f)
{
	return (vs->valid[f / 64] >> (f % 64)) & 1;
}

static __always_inline void fset(struct ps3rsx_vstate *vs, int f, u32 v)
{
	vs->f[f] = v;
	vs->valid[f / 64] |= 1ULL << (f % 64);
}

/* The channel's fields are no longer what the GPU holds (a recovery under it): none counts */
void ps3rsx_vstate_forget(struct ps3rsx_vstate *vs)
{
	memset(vs->valid, 0, sizeof(vs->valid));
}

/* Are all these fields set? */
static bool fall(const struct ps3rsx_vstate *vs, const u8 *f, int n)
{
	int i;

	for (i = 0; i < n; i++)
		if (!fvalid(vs, f[i]))
			return false;
	return true;
}

/* The domain a context DMA handle names, or -1 */
static int dom_of(u32 handle)
{
	return handle == H_VRAM ? P4_VRAM : handle == H_GART ? P4_GART : -1;
}

/* [lo, lo + len) through the DMA handle inside one of the file's buffers (64-bit, no overflow) */
static bool in_set(const struct ps3rsx_vstate *vs, u32 handle, u64 lo, u64 len)
{
	u64 hi;

	if (!len || check_add_overflow(lo, len, &hi))
		return false;
	return p4set_find(vs->set, dom_of(handle), lo, hi);
}

/* rows · pitch + cols · bpp style extents, in u64 with every step checked: false on overflow */
static bool ext2(u64 pitch, u64 rows, u64 row_bytes, u64 *len)
{
	u64 a;

	if (!rows || !row_bytes)
		return false;
	if (check_mul_overflow(pitch, rows - 1, &a) || check_add_overflow(a, row_bytes, len))
		return false;
	return true;
}

/* Bytes per pixel of an RT colour format (RT_FORMAT bits 0-4; libgcm's SURFACE_* codes),
 * unknown ones the largest (16): a format we don't know refuses more, never less */
static u32 rt_bpp(u32 c)
{
	switch (c) {
	case 1: case 2: case 3: case 0xa: return 2;
	case 4: case 5: case 8: case 0xd: case 0xe: case 0xf: case 0x10: return 4;
	case 9: return 1;
	case 0xb: return 8;
	default: return 16;
	}
}

/* zeta (bits 5-7): Z16 2, Z24S8 4, anything else 8 */
static u32 zeta_bpp(u32 z)
{
	return z == 1 ? 2 : z == 2 ? 4 : 8;
}

/* surface 2D and swizzled surface colour formats (nouveau's NV04_SURFACE_*), 0 unknown */
static u32 surf_bpp(u32 c)
{
	switch (c) {
	case 1: return 1;
	case 2: case 3: case 4: case 5: return 2;
	case 6: case 7: case 8: case 9: case 0xa: case 0xb: return 4;
	default: return 0;
	}
}

/* scaled image source formats (NV03_SIFM_COLOR_FORMAT_*, libgcm's TRANSFER_SCALE_FORMAT_*) */
static u32 sifm_bpp(u32 c)
{
	switch (c) {
	case 1: case 2: case 5: case 6: case 7: case 0xa: case 0xb: return 2;
	case 3: case 4: case 0xc: case 0xd: return 4;
	case 8: case 9: return 1;
	default: return 0;
	}
}

#define LO16(v)	((v) & 0xffff)
#define HI16(v)	((v) >> 16)

/* A field's value at its write (6.x of the design): NULL if fine, else why not */
static const char *field_check(int f, u32 v)
{
	switch (f) {
	case P4F_RT_HORIZ:
	case P4F_RT_VERT:	/* x or y, width or height: at most 4096, a size of at least 1 */
		return LO16(v) > 4096 || HI16(v) > 4096 || !HI16(v) ?
			"surface clip coordinate or size out of range" : NULL;
	case P4F_RT_FORMAT: {
		u32 type = (v >> 8) & 0xf, aa = (v >> 12) & 0xf;

		if (type != 1 && type != 2)
			return "surface type other than linear or swizzled";
		if (aa != 0 && aa != 3 && aa != 4 && aa != 5)
			return "surface antialiasing mode unknown";
		if (aa && type == 2)
			return "antialiasing on a swizzled surface";
		if (((v >> 16) & 0xff) > 12 || (v >> 24) > 12)
			return "swizzled surface log2 size above 12";
		return NULL;
	}
	case P4F_WIN_OFF:
		return LO16(v) > 4096 || HI16(v) > 4096 ? "window offset out of range" : NULL;
	case P4F_WIN_MODE:
		return v ? "window clip mode other than 0" : NULL;
	case P4F_WIN_H:
	case P4F_WIN_V:
		return LO16(v) > 4095 || HI16(v) > 4095 ? "window clip out of range" : NULL;
	case P4F_COORD:		/* height 0-11, origin 12-15, centre 16-19 */
		return v >> 20 ? "coordinate conventions: unknown bits" : NULL;
	case P4F_M_PITCH_IN:
	case P4F_M_PITCH_OUT:
		return v >= 0x80000000u ? "M2MF pitch at or above 2^31" : NULL;
	case P4F_M_COUNT:
		return !v || v > 2047 ? "M2MF line count not 1 to 2047" : NULL;
	case P4F_M_FORMAT:
		return v != 0x101 ? "M2MF format other than 1-byte increments" : NULL;
	case P4F_W_FORMAT:
		return (v & 0xff00) || ((v >> 16) & 0xff) > 12 || (v >> 24) > 12 ?
			"swizzled surface format out of range" : NULL;
	case P4F_F_CLIP_PT:
	case P4F_F_OUT_PT:
	case P4F_F_CLIP_SZ:
	case P4F_F_OUT_SZ:
	case P4F_F_SIZE:
		return LO16(v) > 4096 || HI16(v) > 4096 ? "scaled image point or size out of range" :
			NULL;
	case P4F_F_DUDX:
	case P4F_F_DVDY:
		return v >> 31 ? "scaled image step negative" : NULL;
	case P4F_F_IN_FMT: {
		u32 origin = (v >> 16) & 0xff, filter = v >> 24;

		return (origin != 1 && origin != 2) || filter > 1 ?
			"scaled image origin or filter unknown" : NULL;
	}
	}
	return NULL;
}

/* A render target or zeta buffer [lo, hi) of the current surface: false if outside the set */
static bool rt_ok(const struct ps3rsx_vstate *vs, u32 handle, u32 off, u32 pitch, u32 bpp,
		  u64 cols, u64 rows, const char **why)
{
	u32 fmt = vs->f[P4F_RT_FORMAT], aa = (fmt >> 12) & 0xf;
	u64 xs = aa ? 2 : 1, ys = aa == 4 || aa == 5 ? 2 : 1, lo = off & ~63u, len, rb;

	if (aa && aa != 3 && aa != 4 && aa != 5)
		xs = ys = 2;		/* a value counted at its write: the largest scale */
	if (((fmt >> 8) & 0xf) == 2) {		/* swizzled: the whole power-of-two block */
		u32 lw = (fmt >> 16) & 0xff, lh = fmt >> 24;

		if (lw > 12 || lh > 12 || cols > (1ULL << lw) || rows > (1ULL << lh)) {
			*why = "drawing outside a swizzled surface";
			return false;
		}
		len = (1ULL << lw) * (1ULL << lh) * bpp;
	} else {
		if (!pitch) {
			*why = "render target pitch 0";
			return false;
		}
		if (check_mul_overflow(cols * xs, (u64)bpp, &rb) ||
		    !ext2(pitch, rows * ys, rb, &len)) {
			*why = "render target extent overflows";
			return false;
		}
	}
	if (!in_set(vs, handle, lo, len)) {
		*why = "render target outside the file's buffers";
		return false;
	}
	return true;
}

/* CLEAR_BUFFERS (0x1d94): the cleared targets inside the file's buffers */
static int clear_check(const struct ps3rsx_vstate *vs, u32 data, const char **why)
{
	static const u8 need[] = { P4F_RT_HORIZ, P4F_RT_VERT, P4F_RT_FORMAT, P4F_WIN_OFF,
				   P4F_WIN_H, P4F_WIN_V, P4F_COORD };
	static const struct { u8 off, pitch, slot; } t[4] = {
		{ P4F_C0_OFF, P4F_C0_PITCH, P4F_3D_DMA + 5 },	/* 0x194 */
		{ P4F_C1_OFF, P4F_C1_PITCH, P4F_3D_DMA + 3 },	/* 0x18c */
		{ P4F_C2_OFF, P4F_C2_PITCH, P4F_3D_DMA + 13 },	/* 0x1b4 */
		{ P4F_C3_OFF, P4F_C3_PITCH, P4F_3D_DMA + 14 },	/* 0x1b8 */
	};
	u32 h = vs->f[P4F_RT_HORIZ], v = vs->f[P4F_RT_VERT], wo = vs->f[P4F_WIN_OFF];
	u32 fmt = vs->f[P4F_RT_FORMAT];
	u64 cols, rows;
	int i;

	if (data & ~0xf3u) {
		*why = "clear of unknown buffers";
		return P4R_CLEAR;
	}
	if (!data)
		return -1;
	if (!fall(vs, need, ARRAY_SIZE(need))) {
		*why = "clear with a surface field not set by this channel";
		return P4R_UNSET;
	}
	cols = max_t(u64, LO16(h) + HI16(h), LO16(wo) + HI16(vs->f[P4F_WIN_H]) + 1);
	rows = max_t(u64, LO16(v) + HI16(v), HI16(wo) + HI16(vs->f[P4F_WIN_V]) + 1);
	rows = max_t(u64, rows, vs->f[P4F_COORD] & 0xfff);
	if (data & 0xf0) {
		u32 en;

		if (!fvalid(vs, P4F_RT_ENABLE)) {
			*why = "colour clear with RT_ENABLE not set by this channel";
			return P4R_UNSET;
		}
		en = (vs->f[P4F_RT_ENABLE] & 0xf) | 1;		/* target 0 whatever it says */
		for (i = 0; i < 4; i++) {
			if (!(en & (1u << i)))
				continue;
			if (!fvalid(vs, t[i].off) || !fvalid(vs, t[i].pitch) || !fvalid(vs, t[i].slot)) {
				*why = "colour clear of a target not set by this channel";
				return P4R_UNSET;
			}
			if (!rt_ok(vs, vs->f[t[i].slot], vs->f[t[i].off], vs->f[t[i].pitch],
				   rt_bpp(fmt & 0x1f), cols, rows, why))
				return P4R_CLEAR;
		}
	}
	if (data & 3) {
		if (!fvalid(vs, P4F_Z_OFF) || !fvalid(vs, P4F_Z_PITCH) ||
		    !fvalid(vs, P4F_3D_DMA + 6)) {
			*why = "depth clear with zeta not set by this channel";
			return P4R_UNSET;
		}
		if (!rt_ok(vs, vs->f[P4F_3D_DMA + 6], vs->f[P4F_Z_OFF], vs->f[P4F_Z_PITCH],
			   zeta_bpp((fmt >> 5) & 7), cols, rows, why))
			return P4R_CLEAR;
	}
	return -1;
}

/* M2MF BUF_NOTIFY (0x328): the copy's source and destination inside the file's buffers */
static int m2mf_check(const struct ps3rsx_vstate *vs, u32 data, const char **why)
{
	static const u8 need[] = { P4F_M_NOTIFY, P4F_M_IN_DMA, P4F_M_OUT_DMA, P4F_M_OFF_IN,
				   P4F_M_OFF_OUT, P4F_M_PITCH_IN, P4F_M_PITCH_OUT, P4F_M_LEN,
				   P4F_M_COUNT, P4F_M_FORMAT };
	u64 in, out;

	if (data) {
		*why = "M2MF notify other than 0";
		return P4R_M2MF;
	}
	if (!fall(vs, need, ARRAY_SIZE(need))) {
		*why = "M2MF copy with a field not set by this channel";
		return P4R_UNSET;
	}
	if (vs->f[P4F_M_FORMAT] != 0x101 || vs->f[P4F_M_COUNT] - 1 > 2046 ||
	    (vs->f[P4F_M_PITCH_IN] | vs->f[P4F_M_PITCH_OUT]) >> 31) {
		*why = "M2MF format, line count or pitch out of range";
		return P4R_M2MF;
	}
	if (!ext2(vs->f[P4F_M_PITCH_IN], vs->f[P4F_M_COUNT], vs->f[P4F_M_LEN], &in) ||
	    !ext2(vs->f[P4F_M_PITCH_OUT], vs->f[P4F_M_COUNT], vs->f[P4F_M_LEN], &out)) {
		*why = "M2MF line length 0";
		return P4R_M2MF;
	}
	if (!in_set(vs, vs->f[P4F_M_IN_DMA], vs->f[P4F_M_OFF_IN], in)) {
		*why = "M2MF source outside the file's buffers";
		return P4R_M2MF;
	}
	if (!in_set(vs, vs->f[P4F_M_OUT_DMA], vs->f[P4F_M_OFF_OUT], out)) {
		*why = "M2MF destination outside the file's buffers";
		return P4R_M2MF;
	}
	return -1;
}

/* SIFM POINT (0x40c): the scaled image's source and its destination surface */
static int sifm_check(const struct ps3rsx_vstate *vs, u32 point, const char **why)
{
	static const u8 need[] = { P4F_F_DMA, P4F_F_SURFACE, P4F_F_FORMAT, P4F_F_CLIP_PT,
				   P4F_F_CLIP_SZ, P4F_F_OUT_PT, P4F_F_OUT_SZ, P4F_F_DUDX,
				   P4F_F_DVDY, P4F_F_SIZE, P4F_F_IN_FMT, P4F_F_OFF };
	static const u8 need2d[] = { P4F_S_DST_DMA, P4F_S_FORMAT, P4F_S_PITCH, P4F_S_OFF_DST };
	static const u8 needswz[] = { P4F_W_DMA, P4F_W_FORMAT, P4F_W_OFF };
	u32 size = vs->f[P4F_F_SIZE], osz = vs->f[P4F_F_OUT_SZ], csz = vs->f[P4F_F_CLIP_SZ];
	u32 opt = vs->f[P4F_F_OUT_PT], cpt = vs->f[P4F_F_CLIP_PT];
	u32 bpp = sifm_bpp(vs->f[P4F_F_FORMAT]), pitch = LO16(vs->f[P4F_F_IN_FMT]);
	u64 uend, vend, cols, rows, xmax, ymax, len, rb;

	if (!fall(vs, need, ARRAY_SIZE(need))) {
		*why = "scaled image with a field not set by this channel";
		return P4R_UNSET;
	}
	if (!bpp || !pitch || !LO16(size) || !HI16(size) || !LO16(osz) || !HI16(osz) ||
	    !LO16(csz) || !HI16(csz) || field_check(P4F_F_SIZE, size) ||
	    field_check(P4F_F_OUT_SZ, osz) || field_check(P4F_F_CLIP_SZ, csz) ||
	    field_check(P4F_F_OUT_PT, opt) || field_check(P4F_F_CLIP_PT, cpt) ||
	    field_check(P4F_F_DUDX, vs->f[P4F_F_DUDX]) || field_check(P4F_F_DVDY, vs->f[P4F_F_DVDY])) {
		*why = "scaled image format, pitch, size or point out of range";
		return P4R_SIFM;
	}
	/* the source: the larger of the declared size and the span the steps sample, plus one
	 * texel for filtering (u, v in 12.4, the steps in 12.20) */
	uend = (LO16(point) >> 4) + (((u64)vs->f[P4F_F_DUDX] * LO16(osz)) >> 20) + 1;
	vend = (HI16(point) >> 4) + (((u64)vs->f[P4F_F_DVDY] * HI16(osz)) >> 20) + 1;
	cols = max_t(u64, LO16(size), uend) + 1;
	rows = max_t(u64, HI16(size), vend) + 1;
	if (!ext2(pitch, rows, cols * bpp, &len) ||
	    !in_set(vs, vs->f[P4F_F_DMA], vs->f[P4F_F_OFF], len)) {
		*why = "scaled image source outside the file's buffers";
		return P4R_SIFM;
	}
	/* the destination: the output and clip rectangles' union, in the bound surface */
	xmax = max_t(u64, LO16(opt) + LO16(osz), LO16(cpt) + LO16(csz));
	ymax = max_t(u64, HI16(opt) + HI16(osz), HI16(cpt) + HI16(csz));
	if (vs->f[P4F_F_SURFACE] == P4_OBJ_SURF2D) {
		u32 dbpp = surf_bpp(vs->f[P4F_S_FORMAT]), dpitch = HI16(vs->f[P4F_S_PITCH]);

		if (!fall(vs, need2d, ARRAY_SIZE(need2d))) {
			*why = "scaled image into a surface not set by this channel";
			return P4R_UNSET;
		}
		if (!dbpp || !dpitch || !ext2(dpitch, ymax, xmax * dbpp, &len) ||
		    !in_set(vs, vs->f[P4F_S_DST_DMA], vs->f[P4F_S_OFF_DST], len)) {
			*why = "scaled image destination outside the file's buffers";
			return P4R_SIFM;
		}
	} else {
		u32 wf = vs->f[P4F_W_FORMAT], dbpp = surf_bpp(LO16(wf));
		u32 lu = (wf >> 16) & 0xff, lv = wf >> 24;

		if (!fall(vs, needswz, ARRAY_SIZE(needswz))) {
			*why = "scaled image into a swizzled surface not set by this channel";
			return P4R_UNSET;
		}
		if (!dbpp || lu > 12 || lv > 12 || xmax > (1ULL << lu) || ymax > (1ULL << lv)) {
			*why = "scaled image outside its swizzled surface";
			return P4R_SIFM;
		}
		rb = (1ULL << lu) * (1ULL << lv) * dbpp;	/* at most 2^26 */
		if (!in_set(vs, vs->f[P4F_W_DMA], vs->f[P4F_W_OFF], rb)) {
			*why = "scaled image destination outside the file's buffers";
			return P4R_SIFM;
		}
	}
	return -1;
}

/*
 * The address checks of one method write (called by check_method() after its own checks): the
 * field's value rule, the triggers, then the write recorded. In the count-only build a failed
 * rule counts and the write is taken as if it passed (state included), so the build measures
 * what the release will do.
 */
static int p4_method(struct ps3rsx_vstate *vs, int c, u32 m, u32 data, const char **why)
{
	u8 f = field_of[c][m / 4];
	const char *bad = NULL;
	int rule = -1, ret;

	if (f) {
		bad = field_check(f - 1, data);
		if (bad)
			rule = P4R_FIELD;
		if (f - 1 == P4F_FENCE) {
			u32 lo = 0x20 + 32 * vs->chan_id;

			if (data < lo || (u64)data + 4 > lo + 32 || (data & 3)) {
				bad = "semaphore offset outside the channel's fence label";
				rule = P4R_FENCE;
			}
		}
	} else if (c == VC_3D) {
		u32 lo = 0x1000 * vs->chan_id, off = data & 0xffffff;
		bool slice = off >= lo && off + 16 <= lo + 0x1000 && !(off & 15);

		switch (m) {
		case 0x1d70:
		case 0x1d74:
			if (!fvalid(vs, P4F_FENCE) || !fvalid(vs, P4F_3D_DMA + 9)) {
				bad = "semaphore release with its offset or DMA not set by this channel";
				rule = P4R_UNSET;
			}
			break;
		case 0x1800:
			if ((data >> 24) < 1 || (data >> 24) > 5 || !slice) {
				bad = "report outside the channel's query slice or of an unknown type";
				rule = P4R_QUERY;
			} else if (!fvalid(vs, P4F_3D_DMA + 10)) {
				bad = "report with its DMA not set by this channel";
				rule = P4R_UNSET;
			}
			break;
		case 0x1e98:
			if (data == 0 || data == 0x01000000)
				break;
			if ((data >> 24) != 2 || !slice) {
				bad = "render enable reading outside the channel's query slice";
				rule = P4R_RENDER;
			} else if (!fvalid(vs, P4F_3D_DMA + 10)) {
				bad = "render enable with its DMA not set by this channel";
				rule = P4R_UNSET;
			}
			break;
		case 0x1d94:
			rule = clear_check(vs, data, &bad);
			break;
		case 0x224:
		case 0x228:
			if (data) {
				bad = "a surface method no source names, other than 0";
				rule = P4R_FIELD;
			}
			break;
		}
	} else if (c == VC_M2MF && m == 0x328) {
		rule = m2mf_check(vs, data, &bad);
	} else if (c == VC_SIFM && m == 0x40c) {
		rule = sifm_check(vs, data, &bad);
	}
	if (rule >= 0) {
		ret = p4_fail(rule, m, data, bad, why);
		if (ret)
			return ret;
	}
	if (f)
		fset(vs, f - 1, data);
	return 0;
}

#ifdef PS3RSX_P4_COUNT
#define PS3RSX_P7	/* P7 is compiled into the count-only build until slice c1 */
#endif
#ifdef PS3RSX_P7
/* P7: inside an open primitive only vertex data, immediate attributes, END and NOP */
static bool p7_ok(int c, u32 m)
{
	return c == VC_3D && (m == 0x100 || m == 0x1808 || (m >= 0x180c && m <= 0x1818) ||
				   m == 0x1824 || (m >= 0x1500 && m <= 0x15fc) ||
				   (m >= 0x1880 && m <= 0x19fc) || (m >= 0x1c00 && m <= 0x1cfc) ||
				   (m >= 0x1e40 && m <= 0x1e7c));
}
#endif

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
static unsigned long allowed[VC_N][BITS_TO_LONGS(0x2000 / 4)];

/*
 * The validator's fast path, fast[class][method / 4]: F_OK when check_method() accepts that
 * method on that class whatever the data, and changes nothing (allowed, not a bind or host
 * method, not data-checked: fast_kind()); F_VTXFMT, F_TEXOFS and F_TEXFMT when its only check is
 * the vertex format (vtxfmt_ok()), the texture offset alignment or the texture format
 * (texfmt_ok()). F_PRIM: vertex data, allowed only inside a primitive (prim_method()); a
 * non-incrementing burst of it inside one is taken as F_OK. Such words, nearly all of a push, cost
 * a table lookup; a non-incrementing burst to an F_OK method (inline vertex and index data) costs
 * one for all its words. Everything else goes through check_method(), so the fast path accepts
 * nothing it wouldn't.
 */
enum { F_SLOW, F_OK, F_VTXFMT, F_TEXOFS, F_TEXFMT, F_PRIM };
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
 * RT_ENABLE (0x220) as Mesa writes it (nv30_state_validate.c): colour targets 0 to n-1, plus MRT
 * when there is more than one, less MRT for a fragment program writing gl_FragColor to every
 * target: 0, 1, 3, 7, 0xf, 0x13, 0x17 or 0x1f. Every other value is refused.
 * rsxdrm-smoke quirks' 0x12 (target 1 and MRT without target 0, its offset and pitch never set)
 * raised graph exception 0x106 on ps3rsx 0.924 after egl-thin's 1x64 target (session M,
 * 2026-11-01, docs/RSX.md): that push passed the validator, and its SYNC returned 0. libgcm's
 * TARGET_1 (2) is nothing Mesa or our tests send.
 */
static __always_inline bool rt_enable_ok(u32 data)
{
	u32 targets = data & 0xf;

#if defined(__KERNEL__) && defined(PS3RSX_TEST_WEDGE)
	/* the wedge generator for regression tests of the recovery (unwedge.c's
	 * test_allow_rt_enable_12: root only, off by default, cleared by every recovery; built
	 * only into the module for the debug kernel, PS3RSX_TEST_WEDGE) */
	if (data == 0x12 && READ_ONCE(ps3rsx_test_allow_rt12))
		return true;
#endif
	return !(data & ~0x1fu) && !(targets & (targets + 1)) && (!(data & 0x10) || targets >= 3);
}

/*
 * A fragment texture format word (TEX_FORMAT, 0x1a04 + 0x20 * unit) the RSX samples without a
 * graph exception: not format 0x14 (nouveau's A16, libgcm's X16), which nothing had sampled on
 * the RSX before mpv's dither texture (GL_LUMINANCE16, Mesa's L16_UNORM): its first draw raised
 * exception 0x105 and wedged the RSX (2026-10-28, docs/RSX.md). Mesa 0052 no longer offers the
 * format on the PS3. The vertex units take far less: vtxtex_ok().
 */
#define TEXFMT_X16	0x14

static __always_inline bool texfmt_ok(u32 data)
{
	return ((data >> 8) & 0x1f) != TEXFMT_X16;
}

/*
 * A word for the vertex texture units (0x900 + 0x20 * unit, units 0-3) as libgcm writes them
 * (cellGcmSetVertexTexture*, RPCS3's vertex_texture): NV40 vertex texture fetch reads 2D fp32
 * textures without filtering. Nothing sends these words yet (Mesa's vertex sampler count is 0;
 * nv40_verttex.c writes only CONTROL0 = 0), and no vertex texture has been sampled on the RSX,
 * so anything else is refused (docs/DRIVER.md, the validator):
 *   0x00 OFFSET     128-byte aligned like the fragment units, below 256 MiB (VRAM's size, and
 *                   the end of the IO window);
 *   0x04 FORMAT     location 1 (VRAM) or 2 (XDR), not cubic, border colour (bit 3, as Mesa and
 *                   libgcm send it), 2D, format X32_FLOAT (0x9c) or W32_Z32_Y32_X32_FLOAT (0x9b),
 *                   swizzled or linear (+0x20) and normalized, 1 to 13 levels (4096 down to 1);
 *   0x08 ADDRESS    wrap S (bits 0-3) and T (bits 8-11) 1 to 8 (WRAP .. MIRROR_ONCE_CLAMP), the
 *                   rest 0;
 *   0x0c CONTROL0   enable (bit 31), min and max LOD (bits 19-30, 7-18); no anisotropy or
 *                   alpha kill (bits 0-6 0);
 *   0x10 CONTROL3   pitch at most 65536 (4096 RGBA32F texels), depth 0 or 1;
 *   0x14 FILTER     the LOD bias (bits 0-12), the rest 0;
 *   0x18 IMAGE_RECT width (bits 16-31) and height (0-15) 1 to 4096;
 *   0x1c BORDER     any colour.
 * Not checked: the offset plus the texture's size against its buffer (no texture unit's is:
 * the bounds checks at trigger methods are not written).
 */
#define VTXTEX_LO	0x0900
#define VTXTEX_HI	0x097c

static const char *const vtxtex_why[8] = {
	"vertex texture offset not 128-byte aligned or past 256 MiB",
	"vertex texture format other than 2D fp32 (X32 or W32Z32Y32X32)",
	"vertex texture address (wrap) out of range",
	"vertex texture control0 with anisotropy or alpha kill",
	"vertex texture pitch or depth out of range",
	"vertex texture filter other than an LOD bias",
	"vertex texture size not 1 to 4096",
	"",
};

static bool vtxtex_ok(u32 m, u32 data, const char **why)
{
	u32 fmt, w, h, levels;

	switch (m & 0x1c) {
	case 0x00:
		if ((data & 127) || data >= 0x10000000)
			goto bad;
		return true;
	case 0x04:
		fmt = (data >> 8) & 0xdf;	/* LN (0x20) either way */
		levels = data >> 16;
		if ((data & 0xff) != 0x29 && (data & 0xff) != 0x2a)
			goto bad;		/* location, cubic, border, dimension */
		if (fmt != 0x9b && fmt != 0x9c)
			goto bad;
		if (levels < 1 || levels > 13)
			goto bad;
		return true;
	case 0x08:
		w = data & 0xf;
		h = (data >> 8) & 0xf;
		if ((data & ~0xf0fu) || w < 1 || w > 8 || h < 1 || h > 8)
			goto bad;
		return true;
	case 0x0c:
		if (data & 0x7f)
			goto bad;
		return true;
	case 0x10:
		if ((data & 0xfffff) > 0x10000 || (data >> 20) > 1)
			goto bad;
		return true;
	case 0x14:
		if (data & ~0x1fffu)
			goto bad;
		return true;
	case 0x18:
		w = data >> 16;
		h = data & 0xffff;
		if (w < 1 || w > 4096 || h < 1 || h > 4096)
			goto bad;
		return true;
	default:
		return true;			/* border colour */
	}
bad:
	*why = vtxtex_why[(m & 0x1c) / 4];
	return false;
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
	for (c = 0; c < P4F_N; c++)
		field_of[p4_fields[c].cls][p4_fields[c].m / 4] = c + 1;
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

/*
 * Vertex data, which the RSX takes only between BEGIN and END: VB_ELEMENT_U16 and _U32 (0x180c,
 * 0x1810), VB_VERTEX_BATCH (0x1814), inline VERTEX_DATA (0x1818) and VB_INDEX_BATCH (0x1824).
 * Not the index buffer's offset and format (0x181c, 0x1820), set before BEGIN. Mesa sends these
 * only inside a primitive; outside one they could come from a client continuing a draw whose
 * BEGIN was in a rejected submit, or whose primitive ps3rsx closed (submit.c).
 */
static bool prim_method(u32 m)
{
	return m == 0x180c || m == 0x1810 || m == 0x1814 || m == 0x1818 || m == 0x1824;
}

/* A fragment texture offset (units 0-15)? */
static bool tex_offset(u32 m)
{
	return (m & ~0x1e0) == 0x1a00;
}

/* A fragment texture format word (units 0-15)? */
static bool tex_format(u32 m)
{
	return (m & ~0x1e0) == 0x1a04;
}

/*
 * How the fast path may take allowed method m of class c, from what check_method() does with
 * it (keep in step: a data check missing here would be skipped).
 */
static u8 fast_kind(int c, u32 m)
{
	if (m >= 0x180 && m <= 0x1bc)
		return F_SLOW;			/* context DMAs; SIFM 0x198 surface */
	if (field_of[c][m / 4] || p4_trigger(c, m))
		return F_SLOW;			/* the address checks' fields and triggers */
	if (c != VC_3D)
		return F_OK;
	if (m == 0x1808 || m == 0x208 || m == 0x220)
		return F_SLOW;			/* BEGIN_END state, RT_FORMAT, RT_ENABLE */
	if (prim_method(m))
		return F_PRIM;			/* vertex data: inside a primitive only */
	if (m >= VTXTEX_LO && m <= VTXTEX_HI)
		return F_SLOW;			/* vertex texture units: vtxtex_ok() */
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
		/* one object per class (the second M2MF; image from CPU has no methods): per-object
		 * and per-class PGRAPH state then agree, whichever the RSX keeps */
		if ((data == 0x3137c0de || data == 0x31337808) &&
		    p4_fail(P4R_BIND, 0, data, "bind of an object no client uses", why))
			return -EINVAL;
		vs->bound[subch] = data;
		vs->cls[subch] = vclass(data);
		return 0;
	}
	if (mthd < 0x100) {
		/* host methods: none. SET_REFERENCE (0x50) is the kernel's sequence number, which
		 * its fences and copies wait on, and the FIFO semaphores (0x60-0x6c) are the
		 * kernel's too; Mesa sends neither (the acquire, 0x68, can also block forever:
		 * docs/RSX.md) */
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
	if (vs->bound[subch] == 0x31337000 && mthd == 0x1808) {
		/* BEGIN_END: Mesa splits draws across pushes and submits, so the state carries
		 * over; a BEGIN inside a primitive or an END outside one is nothing Mesa sends */
		if ((data != 0) == vs->prim_open) {
			*why = data ? "BEGIN with a primitive open" : "END with no primitive open";
			return -EINVAL;
		}
		vs->prim_open = data != 0;
	}
	if (vs->bound[subch] == 0x31337000 && prim_method(mthd) && !vs->prim_open) {
		*why = "vertex data outside BEGIN/END";
		return -EINVAL;
	}
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
	if (vs->bound[subch] == 0x31337000 && mthd == 0x220 && !rt_enable_ok(data)) {
		/* RT_ENABLE 0x12 raised graph exception 0x106 (rt_enable_ok()) */
		*why = "RT_ENABLE other than Mesa's (targets 0 to n-1, MRT only with more than one)";
		return -EINVAL;
	}
	if (vs->bound[subch] == 0x31337000 && tex_offset(mthd) && (data & 127)) {
		/* a texture sampled at an offset only 32-byte aligned raised graph exception
		 * 0x105 (Neverball, +0xaa0); libgcm aligns texture offsets to 128 bytes, and Mesa
		 * 0014 never sends anything else (docs/GAME-PLAN.md, driver bugs) */
		*why = "texture offset not 128-byte aligned";
		return -EINVAL;
	}
	if (vs->bound[subch] == 0x31337000 && mthd >= VTXTEX_LO && mthd <= VTXTEX_HI &&
	    !vtxtex_ok(mthd, data, why))
		return -EINVAL;
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
		return p4_method(vs, VC_SIFM, mthd, data, why);
	}
	if (mthd >= 0x180 && mthd <= 0x1bc) {
		int d = dma_index(data);

		if (d < 0 || !known_dma(data)) {
			*why = "unknown context DMA handle";
			return -EINVAL;
		}
		if ((d < 0 || !(slot_allow(vs->cls[subch], mthd) & DM(d))) &&
		    p4_fail(P4R_SLOT, mthd, data, "context DMA not taken by this slot", why))
			return -EINVAL;
	}
	return p4_method(vs, vs->cls[subch], mthd, data, why);
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
#ifdef PS3RSX_P7
		/* a non-incrementing burst: one method, and only BEGIN_END (allowed) changes the
		 * primitive state, so one check covers its words */
		if (noninc && count && vs->prim_open && !p7_ok(vs->cls[subch], mthd) &&
		    p4_fail(P4R_P7, mthd, data[0], "method or bind inside an open primitive", &why))
			goto bad;
#endif
		if (noninc) {
			/* the same method for every word: one lookup when its data isn't checked */
			u8 f = ok[mthd / 4];

			if (f == F_PRIM && vs->prim_open)
				f = F_OK;	/* vertex data inside a primitive (it can't close it) */

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
#ifdef PS3RSX_P7
				if (vs->prim_open && !p7_ok(vs->cls[subch], m) &&
				    p4_fail(P4R_P7, m, data[k], "method or bind inside an open primitive",
					    &why))
					goto bad;
#endif
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
	pr_warn_ratelimited("ps3rsx: rejected push: %s at word %u (%08x)\n", why, i, w[i]);
	return -EINVAL;
}
