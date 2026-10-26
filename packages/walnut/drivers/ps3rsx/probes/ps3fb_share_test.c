// SPDX-License-Identifier: GPL-2.0
/*
 * ps3fb_share_test: Phase 1 check of the ps3fb GPU sharing API (local/0005) and the GART
 * reservation (local/0004). Load prints the info and the driver context (allocated on first
 * use) and takes the GPU over; unload hands it back. While loaded, ps3fb's direct-GPU
 * interface must refuse with -EBUSY and the console must not be redrawn.
 *
 *   insmod ps3fb_share_test.ko [own=0]    (own=0: only print, no takeover)
 */
#include <linux/module.h>
#include <asm/ps3.h>
#include <asm/ps3gpu.h>

static bool own = true;
module_param(own, bool, 0444);

#define P(fmt, ...) pr_info("ps3fb_share_test: " fmt "\n", ##__VA_ARGS__)

static int __init share_init(void)
{
	struct ps3fb_gpu_info gi;
	struct ps3fb_gpu_ctx ctx;
	int s;

	P("GART reservation: %lu bytes at %p", ps3rsx_gart.size, ps3rsx_gart.address);
	s = ps3fb_gpu_get_info(&gi);
	P("get_info %d: mem %#llx ddr_lpar %#llx memory_size %#llx reserved %#llx %ux%u pitch %u frame0 %#x",
	  s, gi.memory_handle, gi.ddr_lpar, gi.memory_size, gi.reserved, gi.xres, gi.yres,
	  gi.line_length, gi.full_offset);
	s = ps3fb_gpu_ctx_get(0x820, &ctx);
	P("ctx_get %d: handle %#llx ctrl %#llx dinfo %#llx reports %#llx+%#llx", s, ctx.handle,
	  ctx.ctrl_lpar, ctx.dinfo_lpar, ctx.reports_lpar, ctx.reports_size);
	P("exceptions so far: %u", ps3fb_gpu_exception_seen());
	if (own) {
		s = ps3fb_gpu_takeover(true);
		P("takeover: %d", s);
		if (s)
			return s;
	}
	return 0;
}

static void __exit share_exit(void)
{
	if (own)
		P("release: %d", ps3fb_gpu_takeover(false));
}

module_init(share_init);
module_exit(share_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ps3fb GPU sharing API test");
