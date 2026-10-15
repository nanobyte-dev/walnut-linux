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
 */
#include <linux/bitmap.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "ps3rsx.h"

#define NREG		(0x2000 / 4)
#define NREG_2D		(0x410 / 4)
#define VP_SLOTS	512
#define CONST_SLOTS	468

enum { OBJ_3D, OBJ_M2MF_IN, OBJ_M2MF_OUT, OBJ_SURF2D, OBJ_SWZ, OBJ_SIFM, NOBJ };

static const u32 obj_handle[NOBJ] = {
	0x31337000, 0x31337303, 0x3137c0de, 0x313371c3, 0x31337a73, 0x3137af00,
};

struct ps3rsx_shadow {
	u32 bound[8];
	s8 bound_obj[8];			/* index of bound[] in obj_handle + 1, 0 if none */
	u32 reg3d[NREG];
	DECLARE_BITMAP(reg3d_set, NREG);
	u32 reg2d[NOBJ][NREG_2D];		/* [OBJ_3D] unused */
	DECLARE_BITMAP(reg2d_set, NOBJ * NREG_2D);
	u32 vp[VP_SLOTS * 4];
	DECLARE_BITMAP(vp_set, VP_SLOTS);
	u32 vp_ptr, vp_word;
	u32 cst[CONST_SLOTS * 4];
	DECLARE_BITMAP(cst_set, CONST_SLOTS);
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

static int obj_index(u32 handle)
{
	int i;

	for (i = 0; i < NOBJ; i++)
		if (obj_handle[i] == handle)
			return i;
	return -1;
}

/* Is this 3D method a command rather than state? Looked up per word from side_3d (below). */
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

static unsigned long side_3d[BITS_TO_LONGS(NREG)];

void ps3rsx_shadow_init(void)
{
	u32 m;

	for (m = 0; m < 0x2000; m += 4)
		if (side_effect_3d_slow(m))
			__set_bit(m / 4, side_3d);
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

/* One method write into the shadow (the validator calls this for every word it accepts) */
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
	if (obj != OBJ_3D) {
		if (m < 0x410 && !side_effect_2d(obj, m)) {
			sh->reg2d[obj][m / 4] = v;
			__set_bit(obj * NREG_2D + m / 4, sh->reg2d_set);
		}
		return;
	}
	if (m == 0x1e9c) {			/* vertex program load pointer */
		sh->vp_ptr = v;
		sh->vp_word = 0;
		sh->vp_ptr_set = true;
		return;
	}
	if (m >= 0x0b80 && m <= 0x0bfc) {	/* vertex program words, in write order */
		if (sh->vp_ptr < VP_SLOTS) {
			sh->vp[sh->vp_ptr * 4 + sh->vp_word] = v;
			__set_bit(sh->vp_ptr, sh->vp_set);
		}
		if (++sh->vp_word == 4) {
			sh->vp_word = 0;
			sh->vp_ptr++;
		}
		return;
	}
	if (m == 0x1efc) {			/* constant load pointer */
		sh->cst_ptr = v;
		sh->cst_word = 0;
		sh->cst_ptr_set = true;
		return;
	}
	if (m >= 0x1f00 && m <= 0x1f7c) {	/* constant words */
		if (sh->cst_ptr < CONST_SLOTS) {
			sh->cst[sh->cst_ptr * 4 + sh->cst_word] = v;
			__set_bit(sh->cst_ptr, sh->cst_set);
		}
		if (++sh->cst_word == 4) {
			sh->cst_word = 0;
			sh->cst_ptr++;
		}
		return;
	}
	if (!test_bit(m / 4, side_3d)) {
		sh->reg3d[m / 4] = v;
		__set_bit(m / 4, sh->reg3d_set);
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

/*
 * Write into buf the commands that make the GPU's state (hw) match the channel's (ch).
 * Returns the number of words, or -ENOSPC. Nothing changes until ps3rsx_shadow_take().
 */
int ps3rsx_shadow_restore(const struct ps3rsx_shadow *ch, const struct ps3rsx_shadow *hw,
			  u32 *buf, u32 max_words)
{
	struct emitter e = { .p = buf, .end = buf + max_words };
	int s, obj, sc3d;
	u32 r, slot;

	/* object binds first: everything else goes through them */
	for (s = 0; s < 8; s++)
		if (ch->bound[s] && ch->bound[s] != hw->bound[s])
			emit(&e, MTH(1, s, 0), &ch->bound[s], 1);

	for (obj = OBJ_M2MF_IN; obj < NOBJ; obj++) {
		s = subch_of(ch, obj_handle[obj]);
		if (s < 0)
			continue;
		for (r = 0x180 / 4; r < NREG_2D; r++) {
			u32 bit = obj * NREG_2D + r;

			if (test_bit(bit, ch->reg2d_set) &&
			    (!test_bit(bit, hw->reg2d_set) || ch->reg2d[obj][r] != hw->reg2d[obj][r]))
				emit(&e, MTH(1, s, r * 4), &ch->reg2d[obj][r], 1);
		}
	}

	sc3d = subch_of(ch, obj_handle[OBJ_3D]);
	if (sc3d >= 0) {
		for (r = 0x180 / 4; r < NREG; r++)
			if (test_bit(r, ch->reg3d_set) &&
			    (!test_bit(r, hw->reg3d_set) || ch->reg3d[r] != hw->reg3d[r]))
				emit(&e, MTH(1, sc3d, r * 4), &ch->reg3d[r], 1);

		for (slot = 0; slot < VP_SLOTS; slot++) {
			if (!test_bit(slot, ch->vp_set) ||
			    (test_bit(slot, hw->vp_set) &&
			     !memcmp(&ch->vp[slot * 4], &hw->vp[slot * 4], 16)))
				continue;
			emit(&e, MTH(1, sc3d, 0x1e9c), &slot, 1);
			emit(&e, MTH(4, sc3d, 0x0b80), &ch->vp[slot * 4], 4);
		}
		if (ch->vp_ptr_set)
			emit(&e, MTH(1, sc3d, 0x1e9c), &ch->vp_ptr, 1);

		for (slot = 0; slot < CONST_SLOTS; slot++) {
			if (!test_bit(slot, ch->cst_set) ||
			    (test_bit(slot, hw->cst_set) &&
			     !memcmp(&ch->cst[slot * 4], &hw->cst[slot * 4], 16)))
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

/* The restore was submitted: the GPU now holds the channel's state (plus what it never set) */
void ps3rsx_shadow_take(const struct ps3rsx_shadow *ch, struct ps3rsx_shadow *hw)
{
	u32 r, slot;

	memcpy(hw->bound, ch->bound, sizeof(hw->bound));
	memcpy(hw->bound_obj, ch->bound_obj, sizeof(hw->bound_obj));
	for (r = 0; r < NREG; r++)
		if (test_bit(r, ch->reg3d_set)) {
			hw->reg3d[r] = ch->reg3d[r];
			__set_bit(r, hw->reg3d_set);
		}
	for (r = 0; r < NOBJ * NREG_2D; r++)
		if (test_bit(r, ch->reg2d_set)) {
			hw->reg2d[r / NREG_2D][r % NREG_2D] = ch->reg2d[r / NREG_2D][r % NREG_2D];
			__set_bit(r, hw->reg2d_set);
		}
	for (slot = 0; slot < VP_SLOTS; slot++)
		if (test_bit(slot, ch->vp_set)) {
			memcpy(&hw->vp[slot * 4], &ch->vp[slot * 4], 16);
			__set_bit(slot, hw->vp_set);
		}
	for (slot = 0; slot < CONST_SLOTS; slot++)
		if (test_bit(slot, ch->cst_set)) {
			memcpy(&hw->cst[slot * 4], &ch->cst[slot * 4], 16);
			__set_bit(slot, hw->cst_set);
		}
	hw->vp_ptr = ch->vp_ptr;
	hw->vp_word = ch->vp_word;
	hw->cst_ptr = ch->cst_ptr;
	hw->cst_word = ch->cst_word;
}
