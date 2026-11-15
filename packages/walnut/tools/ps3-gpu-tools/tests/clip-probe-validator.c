// SPDX-License-Identifier: GPL-2.0
/*
 * clip-probe-check's validator half: ps3rsx's validate.c (the shipping one, from the tree) built
 * on the host with the driver tests' kernel stand-ins, as p4-fuzz builds it. vcheck() runs one push
 * on a fresh channel against a file set holding the probe's BOs.
 */
#include "shim.h"
#define PS3RSX_H
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static char last_warn[256];
#define pr_warn(...) snprintf(last_warn, sizeof(last_warn), __VA_ARGS__)
#define pr_warn_ratelimited pr_warn
#include "p4.h"
bool ps3rsx_known_object(u32 handle);
int ps3rsx_validate(struct ps3rsx_vstate *vs, const u32 *w, u32 nwords);
void ps3rsx_validate_init(void);
#include "validate.c"	/* ps3rsx's, through -I drivers/ps3rsx/src */

#include "clip-probe-check.h"

static struct p4set set;

void vinit(const struct vbo *bos, int n)
{
	ps3rsx_validate_init();
	p4set_free(&set);
	for (int i = 0; i < n; i++)
		if (p4set_insert(&set, bos[i].vram ? P4_VRAM : P4_GART, bos[i].lo, bos[i].size))
			printf("FAIL: set insert %d\n", i);
}

int vcheck(const uint32_t *w, unsigned n, const char **why)
{
	struct ps3rsx_vstate vs;

	memset(&vs, 0, sizeof(vs));
	vs.chan_id = 2;
	vs.set = &set;
	last_warn[0] = 0;
	int rc = ps3rsx_validate(&vs, w, n);
	*why = last_warn;
	return rc;
}
