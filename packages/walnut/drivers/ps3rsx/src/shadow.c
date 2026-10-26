// SPDX-License-Identifier: GPL-2.0
/*
 * Software context switch. The RSX keeps one set of 3D (and 2D) state for every hardware
 * channel (docs/RSX.md E5), and all our channels share one hardware context anyway, so the
 * kernel does what a GPU context switch would: each channel's accepted pushes are replayed into
 * a shadow of the state they set, the device tracks what the GPU currently holds, and when the
 * GPU moves to another channel the differing state is written first.
 *
 * Shadowed: object binds per subchannel; every 3D method that is state; the state methods of
 * the copy/2D objects; vertex program memory (512 slots, uploaded through 0xb80.. from the load
 * pointer 0x1e9c) and vertex constants (468 slots, 0x1f00.. from 0x1efc).
 * Never shadowed (they do something rather than set something): draws and inline vertex data,
 * attribute 0 writes (emit a vertex), clears, report/semaphore/cache commands, copy triggers.
 *
 * Costs (this runs for every word and at every channel switch): each shadow also marks what
 * changed since it was last taken into the GPU's shadow (the dirty bitmaps), so taking it costs
 * what changed rather than everything it holds; at a switch the GPU's shadow takes only what the
 * restore wrote; the bitmaps are walked a word at a time.
 */
#include <linux/bitmap.h>
#include <linux/build_bug.h>
#include <linux/moduleparam.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "ps3rsx.h"

#define NREG		(0x2000 / 4)
#define NREG_2D		(0x410 / 4)
#define VP_SLOTS	512
#define CONST_SLOTS	468

/*
 * The fast replay (burst_fast(), restore_fast()): the same shadow and the same restore stream,
 * in runs instead of words. Off by default; flip it at run time for a same-boot A/B.
 */
static bool shadow_fast;
module_param(shadow_fast, bool, 0644);
MODULE_PARM_DESC(shadow_fast, "Replay pushes into the state shadow by runs of methods "
		 "(same result, fewer stores on the PPE)");

enum { OBJ_3D, OBJ_M2MF_IN, OBJ_M2MF_OUT, OBJ_SURF2D, OBJ_SWZ, OBJ_SIFM, NOBJ };

static const u32 obj_handle[NOBJ] = {
	0x31337000, 0x31337303, 0x3137c0de, 0x313371c3, 0x31337a73, 0x3137af00,
};

struct ps3rsx_shadow {
	u32 bound[8];
	s8 bound_obj[8];			/* index of bound[] in obj_handle + 1, 0 if none */
	/* *_set: state the channel set; *_dirty: of that, what changed since ps3rsx_shadow_take() */
	u32 reg3d[NREG];
	DECLARE_BITMAP(reg3d_set, NREG);
	DECLARE_BITMAP(reg3d_dirty, NREG);
	u32 reg2d[NOBJ][NREG_2D];		/* [OBJ_3D] unused */
	DECLARE_BITMAP(reg2d_set, NOBJ * NREG_2D);
	DECLARE_BITMAP(reg2d_dirty, NOBJ * NREG_2D);
	u32 vp[VP_SLOTS * 4];
	DECLARE_BITMAP(vp_set, VP_SLOTS);
	DECLARE_BITMAP(vp_dirty, VP_SLOTS);
	u32 vp_ptr, vp_word;
	u32 cst[CONST_SLOTS * 4];
	DECLARE_BITMAP(cst_set, CONST_SLOTS);
	DECLARE_BITMAP(cst_dirty, CONST_SLOTS);
	u32 cst_ptr, cst_word;
	bool vp_ptr_set, cst_ptr_set;
};

struct ps3rsx_shadow *ps3rsx_shadow_new(void)
{
	return kvzalloc(sizeof(struct ps3rsx_shadow), GFP_KERNEL);
}

void ps3rsx_shadow_free(struct ps3rsx_shadow *sh)
{
	kvfree(sh);
}

/* Nothing known (PGRAPH was changed behind our back): the next restore emits everything */
void ps3rsx_shadow_clear(struct ps3rsx_shadow *sh)
{
	memset(sh, 0, sizeof(*sh));
}

/* debugfs: the binds and 3D state a shadow holds, one "method value" line each */
void ps3rsx_shadow_show(struct seq_file *m, const struct ps3rsx_shadow *sh)
{
	unsigned int i;

	for (i = 0; i < 8; i++)
		if (sh->bound[i])
			seq_printf(m, "subch %u: %08x\n", i, sh->bound[i]);
	for_each_set_bit(i, sh->reg3d_set, NREG)
		seq_printf(m, "3d %04x %08x\n", i * 4, sh->reg3d[i]);
	seq_printf(m, "vp slots %u, constants %u\n", bitmap_weight(sh->vp_set, VP_SLOTS),
		   bitmap_weight(sh->cst_set, CONST_SLOTS));
}

static int obj_index(u32 handle)
{
	int i;

	for (i = 0; i < NOBJ; i++)
		if (obj_handle[i] == handle)
			return i;
	return -1;
}

/* Is this 3D method a command rather than state? Looked up per word from kind3d (below). */
static bool side_effect_3d_slow(u32 m)
{
	switch (m) {
	case 0x0100: case 0x0110:			/* NOP, wait for idle */
	case 0x1710: case 0x1714: case 0x1718:		/* vertex cache invalidate, pipe nop */
	case 0x17c8: case 0x1800:			/* report reset / get */
	case 0x1d70: case 0x1d74:			/* semaphore releases */
	case 0x1d94:					/* clear */
	case 0x1fd8:					/* texture cache invalidate */
		return true;
	}
	if (m >= 0x1808 && m <= 0x1824)			/* begin/end, elements, inline data, index draws */
		return true;
	/* immediate attribute 0 writes emit a vertex: 1F/2F/3F/4F/2S/4UB/4S formats */
	if (m == 0x1e40 || (m >= 0x1880 && m <= 0x1884) || (m >= 0x1500 && m <= 0x1508) ||
	    (m >= 0x1c00 && m <= 0x1c0c) || m == 0x1900 || m == 0x1940 || (m >= 0x1980 && m <= 0x1984))
		return true;
	return false;
}

/* What a 3D method write does to the shadow, one byte per method (built at module load) */
enum { K_STATE, K_NONE, K_VP_PTR, K_VP, K_CST_PTR, K_CST };
static u8 kind3d[NREG];
/* For the fast replay: kind << 12 | how many methods from this one on have the same kind */
static u16 run3d[NREG];
#define RUN_KIND(t)	((t) >> 12)
#define RUN_LEN(t)	((t) & 0xfff)

void ps3rsx_shadow_init(void)
{
	u32 m;

	for (m = 0; m < 0x2000; m += 4) {
		u8 k = K_STATE;

		if (m < 0x100 || side_effect_3d_slow(m))
			k = K_NONE;		/* host methods (binds never get here), commands */
		else if (m == 0x1e9c)
			k = K_VP_PTR;		/* vertex program load pointer */
		else if (m >= 0x0b80 && m <= 0x0bfc)
			k = K_VP;		/* vertex program words, in write order */
		else if (m == 0x1efc)
			k = K_CST_PTR;		/* constant load pointer */
		else if (m >= 0x1f00 && m <= 0x1f7c)
			k = K_CST;		/* constant words */
		kind3d[m / 4] = k;
	}
	for (m = NREG; m-- > 0; ) {
		u32 len = m + 1 < NREG && kind3d[m + 1] == kind3d[m] ? RUN_LEN(run3d[m + 1]) + 1 : 1;

		run3d[m] = kind3d[m] << 12 | len;
	}
}

static bool side_effect_2d(int obj, u32 m)
{
	if (m == 0x100)
		return true;			/* NOP */
	if ((obj == OBJ_M2MF_IN || obj == OBJ_M2MF_OUT) && m == 0x328)
		return true;			/* BUFFER_NOTIFY launches the copy */
	if (obj == OBJ_SIFM && m == 0x40c)
		return true;			/* POINT launches the scaled image */
	return false;
}

static __always_inline void put3d(struct ps3rsx_shadow *sh, u32 r, u32 v)
{
	sh->reg3d[r] = v;
	__set_bit(r, sh->reg3d_set);
	__set_bit(r, sh->reg3d_dirty);
}

/* One 3D method write (m >= 0x100) */
static __always_inline void write3d(struct ps3rsx_shadow *sh, u32 m, u32 v)
{
	switch (kind3d[m / 4]) {
	case K_STATE:
		put3d(sh, m / 4, v);
		break;
	case K_NONE:
		break;
	case K_VP_PTR:
		sh->vp_ptr = v;
		sh->vp_word = 0;
		sh->vp_ptr_set = true;
		break;
	case K_VP:
		if (sh->vp_ptr < VP_SLOTS) {
			sh->vp[sh->vp_ptr * 4 + sh->vp_word] = v;
			__set_bit(sh->vp_ptr, sh->vp_set);
			__set_bit(sh->vp_ptr, sh->vp_dirty);
		}
		if (++sh->vp_word == 4) {
			sh->vp_word = 0;
			sh->vp_ptr++;
		}
		break;
	case K_CST_PTR:
		sh->cst_ptr = v;
		sh->cst_word = 0;
		sh->cst_ptr_set = true;
		break;
	case K_CST:
		if (sh->cst_ptr < CONST_SLOTS) {
			sh->cst[sh->cst_ptr * 4 + sh->cst_word] = v;
			__set_bit(sh->cst_ptr, sh->cst_set);
			__set_bit(sh->cst_ptr, sh->cst_dirty);
		}
		if (++sh->cst_word == 4) {
			sh->cst_word = 0;
			sh->cst_ptr++;
		}
		break;
	}
}

/* One method write (m >= 0x100) to a copy/2D object */
static __always_inline void write2d(struct ps3rsx_shadow *sh, int obj, u32 m, u32 v)
{
	if (m < 0x410 && !side_effect_2d(obj, m)) {
		sh->reg2d[obj][m / 4] = v;
		__set_bit(obj * NREG_2D + m / 4, sh->reg2d_set);
		__set_bit(obj * NREG_2D + m / 4, sh->reg2d_dirty);
	}
}

/* One method write into the shadow (kernel copies; the validator writes whole bursts) */
void ps3rsx_shadow_write(struct ps3rsx_shadow *sh, u32 subch, u32 m, u32 v)
{
	int obj;

	if (m == 0) {
		sh->bound[subch] = v;
		sh->bound_obj[subch] = obj_index(v) + 1;
		return;
	}
	if (m < 0x100)
		return;				/* host methods: reference/semaphores, not state */
	obj = sh->bound_obj[subch] - 1;
	if (obj < 0)
		return;
	if (obj != OBJ_3D)
		write2d(sh, obj, m, v);
	else
		write3d(sh, m, v);
}

/* --- fast replay ----------------------------------------------------------------------- */

/*
 * The same as the word-by-word replay below, in runs: a run of state methods is one copy and
 * one OR per bitmap word, an upload window keeps its pointer in registers and marks each slot
 * once. On the PPE every store followed by a load of the same doubleword (the bitmap word of
 * the next __set_bit, sh->vp_ptr of the next word) stalls; the word-by-word path does that per
 * word.
 */

static noinline void set_bits2_long(unsigned long *a, unsigned long *b, u32 start, u32 len)
{
	bitmap_set(a, start, len);
	bitmap_set(b, start, len);
}

/* Mark bits start..start+len-1 (len >= 1) in both bitmaps */
static __always_inline void set_bits2(unsigned long *a, unsigned long *b, u32 start, u32 len)
{
	u32 i = BIT_WORD(start), off = start % BITS_PER_LONG;

	if (likely(off + len <= BITS_PER_LONG)) {
		unsigned long mask = (~0UL >> (BITS_PER_LONG - len)) << off;

		a[i] |= mask;
		b[i] |= mask;
	} else {
		set_bits2_long(a, b, start, len);
	}
}

/*
 * len words into an upload window (vertex program or constants), as write3d() does them one
 * at a time: word *word of slot *ptr onwards, only slots below nslots kept; *ptr counts on
 * past the end (and wraps at 2^32, as the u32 does word by word).
 */
static void upload_fast(u32 *mem, unsigned long *set, unsigned long *dirty, u32 nslots,
			u32 *pptr, u32 *pword, const u32 *v, u32 len)
{
	u32 ptr = *pptr, word = *pword;

	while (len) {
		if (ptr < nslots) {
			u32 idx = ptr * 4 + word, w = min(len, nslots * 4 - idx), i;

			for (i = 0; i < w; i++)
				mem[idx + i] = v[i];
			set_bits2(set, dirty, ptr, (idx + w - 1) / 4 - ptr + 1);
			idx += w;
			v += w;
			len -= w;
			ptr = idx / 4;
			word = idx % 4;
		} else {
			/* words until the pointer wraps to slot 0 (nothing is kept until then) */
			u64 skip = ((0x100000000ULL - ptr) << 2) - word;

			if (skip > len) {
				word += len;
				ptr += word / 4;
				word %= 4;
				len = 0;
			} else {
				v += skip;
				len -= skip;
				ptr = 0;
				word = 0;
			}
		}
	}
	*pptr = ptr;
	*pword = word;
}

/* n words (n >= 1) of 3D state methods from r on */
static __always_inline void state3d_fast(struct ps3rsx_shadow *sh, u32 r, const u32 *v, u32 n)
{
	u32 *dst = &sh->reg3d[r];
	u32 i;

	for (i = 0; i < n; i++)
		dst[i] = v[i];
	set_bits2(sh->reg3d_set, sh->reg3d_dirty, r, n);
}

/*
 * n words (n >= 1) to the upload windows and their pointers (k of all of them). Out of line:
 * inline, its addresses and the window code made the compiler save a dozen registers around
 * every burst's run loop.
 */
static noinline void upload3d_fast(struct ps3rsx_shadow *sh, u32 k, const u32 *v, u32 n)
{
	switch (k) {
	case K_VP_PTR:
		sh->vp_ptr = v[n - 1];
		sh->vp_word = 0;
		sh->vp_ptr_set = true;
		break;
	case K_VP:
		upload_fast(sh->vp, sh->vp_set, sh->vp_dirty, VP_SLOTS, &sh->vp_ptr, &sh->vp_word,
			    v, n);
		break;
	case K_CST_PTR:
		sh->cst_ptr = v[n - 1];
		sh->cst_word = 0;
		sh->cst_ptr_set = true;
		break;
	case K_CST:
		upload_fast(sh->cst, sh->cst_set, sh->cst_dirty, CONST_SLOTS, &sh->cst_ptr,
			    &sh->cst_word, v, n);
		break;
	}
}

/* The first method index from r on (r >= 0x100 / 4) that write2d() leaves alone */
static __always_inline u32 next_side_effect_2d(int obj, u32 r)
{
	if (r <= 0x100 / 4)
		return 0x100 / 4;
	if ((obj == OBJ_M2MF_IN || obj == OBJ_M2MF_OUT) && r <= 0x328 / 4)
		return 0x328 / 4;
	if (obj == OBJ_SIFM && r <= 0x40c / 4)
		return 0x40c / 4;
	return NREG_2D;
}

/* A burst (mthd >= 0x100) to a copy/2D object */
static void burst2d_fast(struct ps3rsx_shadow *sh, int obj, u32 mthd, bool noninc,
			 const u32 *v, u32 n)
{
	u32 *row = sh->reg2d[obj], base = obj * NREG_2D, r, end;

	if (mthd >= 0x410)
		return;
	if (noninc) {
		if (!side_effect_2d(obj, mthd)) {
			row[mthd / 4] = v[n - 1];
			set_bits2(sh->reg2d_set, sh->reg2d_dirty, base + mthd / 4, 1);
		}
		return;
	}
	r = mthd / 4;
	end = min(r + n, (u32)NREG_2D);
	while (r < end) {
		u32 stop = min(next_side_effect_2d(obj, r), end), i;

		if (stop > r) {
			for (i = r; i < stop; i++)
				row[i] = *v++;
			set_bits2(sh->reg2d_set, sh->reg2d_dirty, base + r, stop - r);
		}
		if (stop < end) {	/* the side effect method: skip its word */
			v++;
			stop++;
		}
		r = stop;
	}
}

static __always_inline void burst_fast(struct ps3rsx_shadow *sh, u32 subch, u32 mthd,
				       bool noninc, const u32 *v, u32 n)
{
	u32 r, end;
	int obj;

	if (!n)
		return;
	if (mthd < 0x100) {
		u32 skip;

		if (noninc) {			/* rare: the word-by-word way */
			for (r = 0; r < n; r++)
				ps3rsx_shadow_write(sh, subch, mthd, v[r]);
			return;
		}
		/* a bind (the rest goes to the new object), host methods (nothing) up to 0x100 */
		if (mthd == 0)
			ps3rsx_shadow_write(sh, subch, 0, v[0]);
		skip = (0x100 - mthd) / 4;
		if (n <= skip)
			return;
		v += skip;
		n -= skip;
		mthd = 0x100;
	}
	obj = sh->bound_obj[subch] - 1;
	if (obj < 0)
		return;
	if (obj != OBJ_3D) {
		burst2d_fast(sh, obj, mthd, noninc, v, n);
		return;
	}
	r = mthd / 4;
	if (noninc) {
		/* every word to the same method: state keeps the last, the windows take them all */
		switch (kind3d[r]) {
		case K_NONE:
			return;
		case K_STATE:
			put3d(sh, r, v[n - 1]);
			return;
		default:
			upload3d_fast(sh, kind3d[r], v, n);
			return;
		}
	}
	end = min(r + n, (u32)NREG);		/* the validator keeps bursts below 0x2000 */
	while (r < end) {
		u16 t = run3d[r];
		u32 len = min((u32)RUN_LEN(t), end - r);

		if (likely(RUN_KIND(t) == K_STATE))
			state3d_fast(sh, r, v, len);
		else if (RUN_KIND(t) != K_NONE)
			upload3d_fast(sh, RUN_KIND(t), v, len);
		r += len;
		v += len;
	}
}

/*
 * One burst of n accepted words into the shadow, the same as ps3rsx_shadow_write() word by word
 * (methods at most 0x1ffc: the validator checked them). Non-incrementing bursts to a command
 * (inline vertex and index data) cost nothing, to a state method only their last word.
 */
void ps3rsx_shadow_burst(struct ps3rsx_shadow *sh, u32 subch, u32 mthd, bool noninc,
			 const u32 *v, u32 n)
{
	u32 k;
	int obj;

	if (READ_ONCE(shadow_fast)) {
		burst_fast(sh, subch, mthd, noninc, v, n);
		return;
	}
	if (!n)
		return;
	if (mthd < 0x100) {
		/* binds (the rest of the burst goes to the new object) and host methods */
		for (k = 0; k < n; k++)
			ps3rsx_shadow_write(sh, subch, noninc ? mthd : mthd + 4 * k, v[k]);
		return;
	}
	obj = sh->bound_obj[subch] - 1;
	if (obj < 0)
		return;
	if (obj != OBJ_3D) {
		for (k = 0; k < n; k++)
			write2d(sh, obj, noninc ? mthd : mthd + 4 * k, v[k]);
		return;
	}
	if (noninc) {
		switch (kind3d[mthd / 4]) {
		case K_NONE:
			return;
		case K_STATE:
			put3d(sh, mthd / 4, v[n - 1]);
			return;
		default:			/* upload windows and pointers: every word counts */
			for (k = 0; k < n; k++)
				write3d(sh, mthd, v[k]);
			return;
		}
	}
	for (k = 0; k < n; k++)
		write3d(sh, mthd + 4 * k, v[k]);
}

/* A validated push, as the GPU ran it: every burst into the shadow */
void ps3rsx_shadow_push(struct ps3rsx_shadow *sh, const u32 *w, u32 nwords)
{
	u32 i = 0;

	if (READ_ONCE(shadow_fast)) {
		while (i < nwords) {
			u32 h = w[i], n = (h >> 18) & 0x7ff;

			if (h)
				burst_fast(sh, (h >> 13) & 7, h & 0x1ffc, h & 0x40000000,
					   &w[i + 1], n);
			i += 1 + n;
		}
		return;
	}
	while (i < nwords) {
		u32 h = w[i], n = (h >> 18) & 0x7ff;

		if (h)
			ps3rsx_shadow_burst(sh, (h >> 13) & 7, h & 0x1ffc, h & 0x40000000,
					    &w[i + 1], n);
		i += 1 + n;
	}
}

/* --- restore stream ---------------------------------------------------------------------- */

#define MTH(count, subch, m) (((count) << 18) | ((subch) << 13) | (m))

struct emitter {
	u32 *p, *end;
	bool overflow;
};

static void emit(struct emitter *e, u32 h, const u32 *data, u32 n)
{
	if (e->p + 1 + n > e->end) {
		e->overflow = true;
		return;
	}
	*e->p++ = h;
	memcpy(e->p, data, n * 4);
	e->p += n;
}

/* subchannel the channel bound an object on (the restore writes through the same one) */
static int subch_of(const struct ps3rsx_shadow *sh, u32 handle)
{
	int s;

	for (s = 0; s < 8; s++)
		if (sh->bound[s] == handle)
			return s;
	return -1;
}

static bool slot_eq(const u32 *a, const u32 *b)
{
	return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

/*
 * The fast restore walks the bitmaps a word at a time inline (the word-by-word one calls
 * find_next_bit() and test_bit() per bit: hundreds of 3D registers, up to 980 slots) and
 * compares slots as two doublewords. Bits in ascending order: the same stream.
 */
/* Word i of map, limited to bits start..end-1 */
static __always_inline unsigned long range_word(const unsigned long *map, u32 i, u32 start,
						u32 end)
{
	unsigned long w = map[i];

	if (i == BIT_WORD(start))
		w &= ~0UL << (start % BITS_PER_LONG);
	if (i == BIT_WORD(end - 1))
		w &= ~0UL >> (BITS_PER_LONG - 1 - (end - 1) % BITS_PER_LONG);
	return w;
}

/* The 16 bytes of two slots (8-byte aligned: see restore_fast()) */
static __always_inline bool slot_eq_fast(const u32 *a, const u32 *b)
{
	const u64 *x = (const u64 *)a, *y = (const u64 *)b;

	return !((x[0] ^ y[0]) | (x[1] ^ y[1]));
}

/* Registers start..end-1 set in ch's set and not in hw's, or differing: one write each */
static __always_inline void restore_regs(struct emitter *e, const unsigned long *ch_set,
					 const unsigned long *hw_set, const u32 *ch_reg,
					 const u32 *hw_reg, u32 start, u32 end, u32 base, int s)
{
	u32 i;

	for (i = BIT_WORD(start); i <= BIT_WORD(end - 1); i++) {
		unsigned long w = range_word(ch_set, i, start, end), hs = hw_set[i];

		while (w) {
			u32 b = __ffs(w), r = i * BITS_PER_LONG + b - base;

			w &= w - 1;
			if (!((hs >> b) & 1) || ch_reg[r] != hw_reg[r])
				emit(e, MTH(1, s, r * 4), &ch_reg[r], 1);
		}
	}
}

/* Slots (vertex program or constants) the GPU doesn't hold as ch does: pointer, 4 words */
static __always_inline void restore_slots(struct emitter *e, const unsigned long *ch_set,
					  const unsigned long *hw_set, const u32 *ch_mem,
					  const u32 *hw_mem, u32 nslots, int s, u32 ptr_m,
					  u32 data_m)
{
	u32 i;

	for (i = 0; i <= BIT_WORD(nslots - 1); i++) {
		unsigned long w = range_word(ch_set, i, 0, nslots), hs = hw_set[i];

		while (w) {
			u32 b = __ffs(w), slot = i * BITS_PER_LONG + b;

			w &= w - 1;
			if (((hs >> b) & 1) && slot_eq_fast(&ch_mem[slot * 4], &hw_mem[slot * 4]))
				continue;
			emit(e, MTH(1, s, ptr_m), &slot, 1);
			emit(e, MTH(4, s, data_m), &ch_mem[slot * 4], 4);
		}
	}
}

/* ps3rsx_shadow_restore(), the same stream */
static int restore_fast(const struct ps3rsx_shadow *ch, const struct ps3rsx_shadow *hw,
			u32 *buf, u32 max_words)
{
	struct emitter e = { .p = buf, .end = buf + max_words };
	int s, obj, sc3d;

	BUILD_BUG_ON(offsetof(struct ps3rsx_shadow, vp) % 8 ||
		     offsetof(struct ps3rsx_shadow, cst) % 8);
	for (s = 0; s < 8; s++)
		if (ch->bound[s] && ch->bound[s] != hw->bound[s])
			emit(&e, MTH(1, s, 0), &ch->bound[s], 1);

	for (obj = OBJ_M2MF_IN; obj < NOBJ; obj++) {
		u32 base = obj * NREG_2D;

		s = subch_of(ch, obj_handle[obj]);
		if (s >= 0)
			restore_regs(&e, ch->reg2d_set, hw->reg2d_set, ch->reg2d[obj],
				     hw->reg2d[obj], base + 0x180 / 4, base + NREG_2D, base, s);
	}

	sc3d = subch_of(ch, obj_handle[OBJ_3D]);
	if (sc3d >= 0) {
		static const u32 one = 1, zero = 0;

		restore_regs(&e, ch->reg3d_set, hw->reg3d_set, ch->reg3d, hw->reg3d, 0x180 / 4,
			     NREG, 0, sc3d);
		restore_slots(&e, ch->vp_set, hw->vp_set, ch->vp, hw->vp, VP_SLOTS, sc3d, 0x1e9c,
			      0x0b80);
		if (ch->vp_ptr_set)
			emit(&e, MTH(1, sc3d, 0x1e9c), &ch->vp_ptr, 1);
		restore_slots(&e, ch->cst_set, hw->cst_set, ch->cst, hw->cst, CONST_SLOTS, sc3d,
			      0x1efc, 0x1f00);
		if (ch->cst_ptr_set)
			emit(&e, MTH(1, sc3d, 0x1efc), &ch->cst_ptr, 1);
		emit(&e, MTH(1, sc3d, 0x1fd8), &one, 1);	/* texture cache */
		emit(&e, MTH(1, sc3d, 0x1710), &zero, 1);	/* vertex cache */
	}
	return e.overflow ? -ENOSPC : e.p - buf;
}

/*
 * Write into buf the commands that make the GPU's state (hw) match the channel's (ch).
 * Returns the number of words, or -ENOSPC. Nothing changes until ps3rsx_shadow_take_restored().
 * (3D and 2D state starts at 0x180: below it the classes have only commands, never shadowed.)
 */
int ps3rsx_shadow_restore(const struct ps3rsx_shadow *ch, const struct ps3rsx_shadow *hw,
			  u32 *buf, u32 max_words)
{
	struct emitter e = { .p = buf, .end = buf + max_words };
	int s, obj, sc3d;
	u32 r, slot;

	if (READ_ONCE(shadow_fast))
		return restore_fast(ch, hw, buf, max_words);
	/* object binds first: everything else goes through them */
	for (s = 0; s < 8; s++)
		if (ch->bound[s] && ch->bound[s] != hw->bound[s])
			emit(&e, MTH(1, s, 0), &ch->bound[s], 1);

	for (obj = OBJ_M2MF_IN; obj < NOBJ; obj++) {
		u32 base = obj * NREG_2D, end = base + NREG_2D, bit;

		s = subch_of(ch, obj_handle[obj]);
		if (s < 0)
			continue;
		for (bit = find_next_bit(ch->reg2d_set, end, base + 0x180 / 4); bit < end;
		     bit = find_next_bit(ch->reg2d_set, end, bit + 1)) {
			r = bit - base;
			if (!test_bit(bit, hw->reg2d_set) || ch->reg2d[obj][r] != hw->reg2d[obj][r])
				emit(&e, MTH(1, s, r * 4), &ch->reg2d[obj][r], 1);
		}
	}

	sc3d = subch_of(ch, obj_handle[OBJ_3D]);
	if (sc3d >= 0) {
		for (r = find_next_bit(ch->reg3d_set, NREG, 0x180 / 4); r < NREG;
		     r = find_next_bit(ch->reg3d_set, NREG, r + 1))
			if (!test_bit(r, hw->reg3d_set) || ch->reg3d[r] != hw->reg3d[r])
				emit(&e, MTH(1, sc3d, r * 4), &ch->reg3d[r], 1);

		for_each_set_bit(slot, ch->vp_set, VP_SLOTS) {
			if (test_bit(slot, hw->vp_set) &&
			    slot_eq(&ch->vp[slot * 4], &hw->vp[slot * 4]))
				continue;
			emit(&e, MTH(1, sc3d, 0x1e9c), &slot, 1);
			emit(&e, MTH(4, sc3d, 0x0b80), &ch->vp[slot * 4], 4);
		}
		if (ch->vp_ptr_set)
			emit(&e, MTH(1, sc3d, 0x1e9c), &ch->vp_ptr, 1);

		for_each_set_bit(slot, ch->cst_set, CONST_SLOTS) {
			if (test_bit(slot, hw->cst_set) &&
			    slot_eq(&ch->cst[slot * 4], &hw->cst[slot * 4]))
				continue;
			emit(&e, MTH(1, sc3d, 0x1efc), &slot, 1);
			emit(&e, MTH(4, sc3d, 0x1f00), &ch->cst[slot * 4], 4);
		}
		if (ch->cst_ptr_set)
			emit(&e, MTH(1, sc3d, 0x1efc), &ch->cst_ptr, 1);

		/* textures and vertex data may have changed under the other channel */
		{
			static const u32 one = 1, zero = 0;

			emit(&e, MTH(1, sc3d, 0x1fd8), &one, 1);	/* texture cache */
			emit(&e, MTH(1, sc3d, 0x1710), &zero, 1);	/* vertex cache */
		}
	}
	return e.overflow ? -ENOSPC : e.p - buf;
}

static void clear_dirty(struct ps3rsx_shadow *sh)
{
	bitmap_zero(sh->reg3d_dirty, NREG);
	bitmap_zero(sh->reg2d_dirty, NOBJ * NREG_2D);
	bitmap_zero(sh->vp_dirty, VP_SLOTS);
	bitmap_zero(sh->cst_dirty, CONST_SLOTS);
}

/* What every take copies whatever changed: binds and the upload pointers */
static void take_common(const struct ps3rsx_shadow *ch, struct ps3rsx_shadow *hw)
{
	memcpy(hw->bound, ch->bound, sizeof(hw->bound));
	memcpy(hw->bound_obj, ch->bound_obj, sizeof(hw->bound_obj));
	hw->vp_ptr = ch->vp_ptr;
	hw->vp_word = ch->vp_word;
	hw->cst_ptr = ch->cst_ptr;
	hw->cst_word = ch->cst_word;
}

/*
 * The channel's pushes ran: hw takes what they changed in its shadow since it was last taken
 * or its restore was (ps3rsx_shadow_take_restored()). Nothing else wrote to the GPU meanwhile,
 * so that is all the GPU holds beyond what hw knew.
 */
void ps3rsx_shadow_take(struct ps3rsx_shadow *ch, struct ps3rsx_shadow *hw)
{
	u32 r, slot;

	take_common(ch, hw);
	for_each_set_bit(r, ch->reg3d_dirty, NREG) {
		hw->reg3d[r] = ch->reg3d[r];
		__set_bit(r, hw->reg3d_set);
	}
	for_each_set_bit(r, ch->reg2d_dirty, NOBJ * NREG_2D) {
		hw->reg2d[r / NREG_2D][r % NREG_2D] = ch->reg2d[r / NREG_2D][r % NREG_2D];
		__set_bit(r, hw->reg2d_set);
	}
	for_each_set_bit(slot, ch->vp_dirty, VP_SLOTS) {
		memcpy(&hw->vp[slot * 4], &ch->vp[slot * 4], 16);
		__set_bit(slot, hw->vp_set);
	}
	for_each_set_bit(slot, ch->cst_dirty, CONST_SLOTS) {
		memcpy(&hw->cst[slot * 4], &ch->cst[slot * 4], 16);
		__set_bit(slot, hw->cst_set);
	}
	clear_dirty(ch);
}

/*
 * The restore (buf, nwords: ps3rsx_shadow_restore()'s output for ch) was submitted: the GPU
 * holds the channel's state. hw takes exactly what the restore wrote (elsewhere it matched ch
 * already, or the restore doesn't cover it), plus the binds and pointers, and ch starts marking
 * dirty afresh: the submit's pushes, replayed into ch after this, are what the next
 * ps3rsx_shadow_take() copies. Copying all of ch instead, as before, also claimed ch's state for
 * copy/2D objects the restore never wrote (ch had no subchannel for them when it ran, e.g. bound
 * by this submit's pushes), so hw_shadow held values the GPU didn't and a later restore could
 * skip writing them.
 */
void ps3rsx_shadow_take_restored(struct ps3rsx_shadow *ch, struct ps3rsx_shadow *hw,
				 const u32 *buf, u32 nwords)
{
	bool vp_ptr_set = hw->vp_ptr_set, cst_ptr_set = hw->cst_ptr_set;
	u32 i = 0;

	while (i < nwords) {
		u32 h = buf[i], n = (h >> 18) & 0x7ff;

		ps3rsx_shadow_burst(hw, (h >> 13) & 7, h & 0x1ffc, false, &buf[i + 1], n);
		i += 1 + n;
	}
	hw->vp_ptr_set = vp_ptr_set;
	hw->cst_ptr_set = cst_ptr_set;
	take_common(ch, hw);
	clear_dirty(ch);
}
