// SPDX-License-Identifier: GPL-2.0
/*
 * ps3rsx's validator (src/validate.c, built with ../inc/'s kernel stand-ins) for wrap.c: the
 * shim checks every submit as ps3rsx does and answers like it (submit.c: -ECANCELED to a client
 * that asked for PS3_RECOVER, else -EINVAL and -EIO from then on). run.sh copies validate.c and
 * inc/ next to this file.
 */
#include "shim.h"
#define PS3RSX_H	/* the driver header: not in userspace */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static char last_warn[256];
#define pr_warn(...) snprintf(last_warn, sizeof(last_warn), __VA_ARGS__)
#define pr_warn_ratelimited pr_warn

struct ps3rsx_vstate {
	u32 bound[8];
	u8 cls[8];
	bool prim_open;
};
bool ps3rsx_known_object(u32 handle);
int ps3rsx_validate(struct ps3rsx_vstate *vs, const u32 *w, u32 nwords);
void ps3rsx_validate_init(void);

#ifndef VALIDATE_C
#define VALIDATE_C "validate.c"
#endif
#include VALIDATE_C

/* The channel's validator state (wrap.c's one channel), and a copy for the submit being checked */
static struct ps3rsx_vstate chan_vs, sub_vs;
static bool inited;

void ps3val_begin(void)
{
	if (!inited) {
		ps3rsx_validate_init();
		inited = true;
	}
	sub_vs = chan_vs;
}

/* One push of the submit (host-order words): 0, or -EINVAL with *why set */
int ps3val_push(const uint32_t *w, uint32_t n, const char **why)
{
	last_warn[0] = 0;
	if (ps3rsx_validate(&sub_vs, w, n)) {
		*why = last_warn;
		return -EINVAL;
	}
	return 0;
}

/* Every push passed: the channel takes the submit's state */
void ps3val_commit(void)
{
	chan_vs = sub_vs;
}

/* A push was rejected: the subchannel of the 3D object and whether the channel's last
 * accepted submit left a primitive open (submit.c then ends it); the channel has none after */
bool ps3val_cancel(uint32_t *subch3d)
{
	bool open = chan_vs.prim_open;
	u32 sc;

	for (sc = 0; sc < 8 && chan_vs.bound[sc] != 0x31337000; sc++)
		;
	*subch3d = sc & 7;
	chan_vs.prim_open = false;
	return open;
}
