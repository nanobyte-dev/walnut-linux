/* SPDX-License-Identifier: GPL-2.0 */
/* spe.h: ps3rsx's SPE client (spe.c): one SPE reserved from spufs (kernel local/0058) that seals and
 * opens 4 KiB pages for the VRAM swap with spu/vseal.c. */
#ifndef PS3RSX_SPE_H
#define PS3RSX_SPE_H

#include "spe_core.h"

struct ps3rsx_device;
struct ps3rsx_spe;

#ifdef PS3RSX_HAVE_SPU_KERNEL
void ps3rsx_spe_init(struct ps3rsx_device *rsx);
void ps3rsx_spe_fini(struct ps3rsx_device *rsx);
/* the windows a descriptor's addresses must lie in (kernel RAM, the VRAM pool, the bounce) */
int ps3rsx_spe_set_windows(struct ps3rsx_device *rsx, const struct spe_window *w, unsigned int n);
/* a job of npages pages (op VSEAL_OP_SEAL or VSEAL_OP_OPEN), results (tags, status) back into pages:
 * 0, -EIO (refused by the check or the SPU), -ETIMEDOUT (restarted: nothing of it may be trusted, the
 * caller resubmits under fresh counters), -ENODEV (no SPE: the caller uses the disk) */
int ps3rsx_spe_job(struct ps3rsx_device *rsx, unsigned int op, unsigned int npages,
		   struct vseal_page *pages, enum spe_path path);
bool ps3rsx_spe_ready(struct ps3rsx_device *rsx);
#else
static inline void ps3rsx_spe_init(struct ps3rsx_device *rsx) {}
static inline void ps3rsx_spe_fini(struct ps3rsx_device *rsx) {}
static inline bool ps3rsx_spe_ready(struct ps3rsx_device *rsx) { return false; }
#endif
#endif
