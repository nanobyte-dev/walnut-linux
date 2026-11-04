/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The validator's state per channel and the address checks' buffer set per file (validate.c,
 * mem.c, submit.c). Pure code apart from the set's allocations, so the host tests (test/) build
 * the same header: the kernel includes it through ps3rsx.h, the tests directly.
 *
 * The set: every buffer the file holds a handle to (GEM .open/.close, mem.c), by domain, as
 * sorted, non-overlapping intervals [offset, offset + size). The reports area's BO is never in
 * it (it maps the context's reports area, reached only through its own context DMAs), nor any
 * BO without a node. Read and written under the file's set_lock; a submit holds that lock from
 * before its validation until its commit, and a handle's close takes it, so no BO a submit was
 * checked against can be freed before that submit is on the ring (docs/DRIVER.md, the validator).
 */
#ifndef PS3RSX_P4_H
#define PS3RSX_P4_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#define P4_ALLOC(n)	kvmalloc_array((n), sizeof(struct p4_ent), GFP_KERNEL)
#define P4_FREE(p)	kvfree(p)
#else
#define P4_ALLOC(n)	malloc((size_t)(n) * sizeof(struct p4_ent))
#define P4_FREE(p)	free(p)
#endif

/* The validator's object classes (validate.c), one object handle each */
enum { VC_NONE, VC_3D, VC_M2MF, VC_SURF2D, VC_SWZ, VC_SIFM, VC_N };

enum { P4_VRAM, P4_GART, P4_NDOM };

struct p4_ent {
	u64 lo, hi;		/* [offset, offset + size) */
	u32 handles;		/* the file's handles to this BO */
};

struct p4_dom {
	struct p4_ent *e;	/* sorted by lo, non-overlapping */
	u32 n, cap;
};

struct p4set {
	struct p4_dom d[P4_NDOM];
#ifndef __KERNEL__
	bool all;		/* host tests only: captures with no set (mesa-shim's), all allowed */
#endif
};

/* The index of the last entry starting at or below lo, or -1 */
static inline int p4set_index(const struct p4_dom *d, u64 lo)
{
	int a = 0, b = (int)d->n - 1, r = -1;

	while (a <= b) {
		int m = a + (b - a) / 2;

		if (d->e[m].lo <= lo) {
			r = m;
			a = m + 1;
		} else {
			b = m - 1;
		}
	}
	return r;
}

/* Is [lo, hi) (not empty) inside one buffer of the domain? */
static inline bool p4set_find(const struct p4set *s, int dom, u64 lo, u64 hi)
{
	const struct p4_dom *d;
	int i;

	if (!s || dom < 0 || dom >= P4_NDOM)
		return false;
#ifndef __KERNEL__
	if (s->all)
		return lo < hi;
#endif
	d = &s->d[dom];
	i = p4set_index(d, lo);
	return lo < hi && i >= 0 && hi <= d->e[i].hi;
}

/*
 * A handle to the BO [lo, lo + size) made: a new entry, or one more handle on its entry. 0,
 * -ENOMEM, or -EINVAL for an interval that overlaps another BO's (never, as BOs don't overlap).
 */
static inline int p4set_insert(struct p4set *s, int dom, u64 lo, u64 size)
{
	struct p4_dom *d = &s->d[dom];
	u64 hi = lo + size;
	int i;

	if (!size || hi < lo)
		return -EINVAL;
	i = p4set_index(d, lo);
	if (i >= 0 && d->e[i].lo == lo) {
		if (d->e[i].hi != hi)
			return -EINVAL;
		d->e[i].handles++;
		return 0;
	}
	if ((i >= 0 && d->e[i].hi > lo) || (i + 1 < (int)d->n && d->e[i + 1].lo < hi))
		return -EINVAL;
	if (d->n == d->cap) {
		u32 cap = d->cap ? 2 * d->cap : 64;
		struct p4_ent *e = P4_ALLOC(cap);

		if (!e)
			return -ENOMEM;
		if (d->n)
			memcpy(e, d->e, d->n * sizeof(*e));
		P4_FREE(d->e);
		d->e = e;
		d->cap = cap;
	}
	memmove(&d->e[i + 2], &d->e[i + 1], (d->n - (i + 1)) * sizeof(*d->e));
	d->e[i + 1] = (struct p4_ent){ lo, hi, 1 };
	d->n++;
	return 0;
}

/* A handle to the BO starting at lo deleted: true when that was the file's last one */
static inline bool p4set_remove(struct p4set *s, int dom, u64 lo)
{
	struct p4_dom *d = &s->d[dom];
	int i = p4set_index(d, lo);

	if (i < 0 || d->e[i].lo != lo)
		return false;
	if (--d->e[i].handles)
		return false;
	memmove(&d->e[i], &d->e[i + 1], (d->n - i - 1) * sizeof(*d->e));
	d->n--;
	return true;
}

static inline void p4set_free(struct p4set *s)
{
	int k;

	for (k = 0; k < P4_NDOM; k++) {
		P4_FREE(s->d[k].e);
		s->d[k].e = NULL;
		s->d[k].n = s->d[k].cap = 0;
	}
}

/*
 * The address-carrying fields the checks use (validate.c's field table names each one's class
 * and method). A field's value is the channel's own last accepted write, its valid bit says
 * there was one; every field here is state the shadow restores at a channel switch (shadow.c),
 * so the GPU holds the channel's value when the channel's words run. A trigger that needs a
 * field without its valid bit is refused: the GPU would use what another channel, the kernel's
 * own copies or the console left there.
 */
enum p4_field {
	P4F_3D_DMA,				/* 0x180 .. 0x1bc: 16 slots */
	P4F_FENCE = P4F_3D_DMA + 16,		/* 0x1d6c */
	P4F_RT_HORIZ, P4F_RT_VERT, P4F_RT_FORMAT, P4F_C0_PITCH, P4F_C0_OFF, P4F_Z_OFF,
	P4F_C1_OFF, P4F_C1_PITCH, P4F_RT_ENABLE, P4F_Z_PITCH, P4F_C2_PITCH, P4F_C3_PITCH,
	P4F_C2_OFF, P4F_C3_OFF, P4F_WIN_OFF, P4F_WIN_MODE, P4F_WIN_H, P4F_WIN_V, P4F_COORD,
	/* M2MF */
	P4F_M_NOTIFY, P4F_M_IN_DMA, P4F_M_OUT_DMA, P4F_M_OFF_IN, P4F_M_OFF_OUT, P4F_M_PITCH_IN,
	P4F_M_PITCH_OUT, P4F_M_LEN, P4F_M_COUNT, P4F_M_FORMAT,
	/* surface 2D */
	P4F_S_NOTIFY, P4F_S_SRC_DMA, P4F_S_DST_DMA, P4F_S_FORMAT, P4F_S_PITCH, P4F_S_OFF_SRC,
	P4F_S_OFF_DST,
	/* swizzled surface */
	P4F_W_NOTIFY, P4F_W_DMA, P4F_W_FORMAT, P4F_W_OFF,
	/* scaled image */
	P4F_F_NOTIFY, P4F_F_DMA, P4F_F_SURFACE, P4F_F_FORMAT, P4F_F_CLIP_PT, P4F_F_CLIP_SZ,
	P4F_F_OUT_PT, P4F_F_OUT_SZ, P4F_F_DUDX, P4F_F_DVDY, P4F_F_SIZE, P4F_F_IN_FMT, P4F_F_OFF,
	P4F_N
};

#define P4_VALID_WORDS	((P4F_N + 63) / 64)

/* Per channel validator state: which object this channel bound on each subchannel, and the
 * address fields it set (only accepted submits change it: submit.c validates a copy) */
struct ps3rsx_vstate {
	u32 bound[8];
	u8 cls[8];		/* validate.c's class of each bound object */
	bool prim_open;		/* the channel's last BEGIN_END opened a primitive */
	u8 chan_id;		/* its fence label and query slice (submit.c) */
	const struct p4set *set;	/* its file's buffers (read under the file's set_lock) */
	u32 f[P4F_N];
	u64 valid[P4_VALID_WORDS];
};

/* The checks' rules, for the count-only build's counters (validate.c) */
enum p4_rule {
	P4R_BIND,		/* a: an object no client binds */
	P4R_SLOT,		/* a: a context DMA its slot doesn't take */
	P4R_FENCE,		/* a: a semaphore offset outside the channel's label */
	P4R_QUERY,		/* a: a report outside the channel's slice, or of an unknown type */
	P4R_RENDER,		/* a: a render-enable read outside the slice */
	P4R_UNSET,		/* b: a trigger using a field the channel never set */
	P4R_FIELD,		/* b: a field value out of range */
	P4R_M2MF,		/* b: a copy outside the file's buffers */
	P4R_SIFM,		/* b: a scaled image outside the file's buffers */
	P4R_CLEAR,		/* b: a clear outside the file's buffers */
	P4R_P7,			/* P7: a state write or bind inside an open primitive */
	P4R_N
};

#endif
