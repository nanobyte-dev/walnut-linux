/* SPDX-License-Identifier: GPL-2.0 */
/*
 * spe.h: ps3rsx's SPE client (spe.c): one SPE reserved from spufs (kernel local/0058) that seals and opens
 * 4 KiB pages for the VRAM swap with spu/vseal.c. The interface vswap.c will use (S4):
 *
 *  - ps3rsx_spe_set_bounds() once the VRAM pool and the bounce exist, and again whenever they change: the
 *    windows a job's pages may lie in (kernel RAM for plaintext; the VRAM pool, the bounce or kernel RAM for
 *    ciphertext), the ranges no job may ever name (every GART-mapped chunk, the bounce where plaintext would
 *    go, the VRAM pool as a destination for plaintext; the ring and the status line are added by spe.c), and
 *    optionally a per-page predicate for kernel RAM ("not mapped for the GPU"). Refused ranges win over
 *    windows. The test hook (test builds) sets its own bounds; S4 and the hook don't run together.
 *  - ps3rsx_spe_job(): returns
 *      0             every page done, res[] holds the tags and statuses;
 *      -EBADMSG      done, but some pages were refused (an open's tag mismatch): see res[i].status; nothing
 *                    was written for those pages;
 *      -EINVAL       the descriptor check refused the job (a page outside the bounds); nothing ran;
 *      -EIO          the SPU refused the descriptor, or the known answer after a restart was wrong;
 *      -ETIMEDOUT    the SPE was restarted after a reset that proved it quiet: nothing of the job may be
 *                    trusted, its pages may be freed or reused, resubmit under fresh counters;
 *      -ENODEV       no SPE (never reserved, or given up quiet): use the disk; its pages may be freed;
 *      -EHWPOISON    given up after a reset that could NOT prove the SPE stopped: every page any job ever
 *                    named (and the job's own) must be kept for the rest of the boot.
 *  - ps3rsx_spe_must_keep(): true from the first -EHWPOISON on, for the boot.
 *
 * Removal order (drv.c): vswap first (nothing submits any more), then ps3rsx_spe_fini_debugfs() before
 * drm_dev_unregister(), then ps3rsx_spe_fini().
 */
#ifndef PS3RSX_SPE_H
#define PS3RSX_SPE_H

#include "spe_core.h"

struct ps3rsx_device;
struct ps3rsx_spe;

#ifdef PS3RSX_HAVE_SPU_KERNEL
void ps3rsx_spe_init(struct ps3rsx_device *rsx);
void ps3rsx_spe_fini_debugfs(struct ps3rsx_device *rsx);
void ps3rsx_spe_fini(struct ps3rsx_device *rsx);
int ps3rsx_spe_set_bounds(struct ps3rsx_device *rsx, const struct spe_window *w, unsigned int nw,
			  const struct spe_window *refused, unsigned int nr,
			  int (*ram_ok)(void *arg, u64 ea), void *arg);
int ps3rsx_spe_job(struct ps3rsx_device *rsx, unsigned int op, unsigned int npages,
		   const struct vseal_page *pages, struct vseal_result *res, enum spe_path path);
bool ps3rsx_spe_ready(struct ps3rsx_device *rsx);
bool ps3rsx_spe_must_keep(struct ps3rsx_device *rsx);
#else
static inline void ps3rsx_spe_init(struct ps3rsx_device *rsx) {}
static inline void ps3rsx_spe_fini_debugfs(struct ps3rsx_device *rsx) {}
static inline void ps3rsx_spe_fini(struct ps3rsx_device *rsx) {}
static inline bool ps3rsx_spe_ready(struct ps3rsx_device *rsx) { return false; }
static inline bool ps3rsx_spe_must_keep(struct ps3rsx_device *rsx) { return false; }
#endif
#endif
