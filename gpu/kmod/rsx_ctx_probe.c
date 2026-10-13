// SPDX-License-Identifier: GPL-2.0
/*
 * rsx_ctx_probe: allocates a second RSX context on ps3fb's GPU memory handle (a second memory
 * allocation is refused while ps3fb holds all VRAM), checks what it comes with, then frees
 * everything again before init returns (the module stays loaded but holds nothing).
 * Needs local/0003-ps3fb-export-gpu-handles-for-experiments.patch.
 *
 *   insmod rsx_ctx_probe.ko [flags=0x820] [fault=1]
 *
 * Steps (results in dmesg, prefix "rsxprobe:"):
 *   1. lv1_gpu_context_allocate on ps3fb's memory handle: is a second context possible?
 *   2. lv1_gpu_context_iomap at several IO addresses and sizes, on both contexts (on ps3fb's only
 *      above its window): how big is the GPU's XDR window?
 *   3. A FIFO in our own XDR buffer: do the usual object handles exist in a fresh context?
 *   4. (fault=1) an invalid method, then recovery: FIFO pause/setup/resume, else free + reallocate.
 */
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/module.h>
#include <asm/cell-regs.h>
#include <asm/lv1call.h>
#include <asm/ps3.h>
#include <asm/ps3gpu.h>

static ulong flags = 0x820;
module_param(flags, ulong, 0444);
static bool fault;
module_param(fault, bool, 0444);

#define P(fmt, ...) pr_info("rsxprobe: " fmt "\n", ##__VA_ARGS__)
#define IOPTE_RW (CBE_IOPTE_PP_W | CBE_IOPTE_PP_R | CBE_IOPTE_M)
#define MTH(count, subch, method) (((count) << 18) | ((subch) << 13) | (method))

#define XDR_ORDER 8			/* 1 MB: the default IO page size */
#define XDR_SIZE (PAGE_SIZE << XDR_ORDER)
#define FIFO_IOIF 0x0			/* FIFO at the start of our XDR buffer */
#define SCRATCH 0x80000			/* XDR scratch, half way into the buffer */
#define VRAM_SCRATCH 0x4000000		/* well above the visible frame */

void ps3fb_gpu_handles(u64 *memory_handle, u64 *context_handle, u64 *ddr_lpar,
		       void **xdr, unsigned long *xdr_size);

struct ctx {
	u64 mem, flags, handle, ddr_lpar, ctrl_lpar, dinfo_lpar, reports_lpar, reports_size;
	void __iomem *ctrl, *dinfo, *vram;
	u32 *xdr;
	u64 xdr_lpar;
	u32 *p, token;
};

static int ctx_alloc(struct ctx *c)
{
	int s = lv1_gpu_context_allocate(c->mem, c->flags, &c->handle, &c->ctrl_lpar, &c->dinfo_lpar,
					 &c->reports_lpar, &c->reports_size);
	P("context_allocate(flags %#llx): %d handle %#llx ctrl %#llx dinfo %#llx reports %#llx+%#llx",
	  c->flags, s, c->handle, c->ctrl_lpar, c->dinfo_lpar, c->reports_lpar, c->reports_size);
	if (s)
		return s;
	c->ctrl = ioremap(c->ctrl_lpar, PAGE_SIZE);
	c->dinfo = ioremap(c->dinfo_lpar, 128 * 1024);
	if (!c->ctrl || !c->dinfo)
		return -ENOMEM;
	P("driver info: version %#x gpu %#x memory_size %#x hw_channel %u",
	  in_be32(c->dinfo), in_be32(c->dinfo + 4), in_be32(c->dinfo + 8), in_be32(c->dinfo + 12));
	s = lv1_gpu_context_iomap(c->handle, FIFO_IOIF, c->xdr_lpar, XDR_SIZE, IOPTE_RW);
	P("iomap our 1 MB at IOIF %#x: %d", FIFO_IOIF, s);
	return s;
}

static void ctx_free(struct ctx *c)
{
	if (c->ctrl)
		iounmap(c->ctrl);
	if (c->dinfo)
		iounmap(c->dinfo);
	c->ctrl = c->dinfo = NULL;
	if (c->handle) {
		lv1_gpu_context_iomap(c->handle, FIFO_IOIF, c->xdr_lpar, XDR_SIZE, CBE_IOPTE_M);
		P("context_free: %d", lv1_gpu_context_free(c->handle));
		c->handle = 0;
	}
}

#define GET(c) in_be32((c)->ctrl + 0x44)
#define REF(c) in_be32((c)->ctrl + 0x48)

static void begin(struct ctx *c)
{
	c->p = c->xdr + 0x1000 / 4;		/* commands always start at 0x1000 */
}

static void out(struct ctx *c, u32 subch, u32 mth, u32 val)
{
	*c->p++ = MTH(1, subch, mth);
	*c->p++ = val;
}

/* Point get and put at 0x1000, append SET_REFERENCE, kick, wait for the ref token. */
static int run(struct ctx *c, const char *what, int ms)
{
	int s, i;
	u32 put;

	out(c, 0, 0x50, ++c->token);
	put = FIFO_IOIF + (u32)((c->p - c->xdr) * 4);
	wmb();
	s = lv1_gpu_context_attribute(c->handle, L1GPU_CONTEXT_ATTRIBUTE_FIFO_SETUP,
				      FIFO_IOIF + 0x1000, FIFO_IOIF + 0x1000, 0, 0);
	if (s) {
		P("%s: FIFO_SETUP failed: %d", what, s);
		return s;
	}
	out_be32(c->ctrl + 0x40, put);
	for (i = 0; i < ms * 10; i++) {
		if (REF(c) == c->token)
			return 0;
		udelay(100);
	}
	P("%s: timeout, get %#x put %#x ref %#x (want %#x)", what, GET(c), put, REF(c), c->token);
	return -ETIMEDOUT;
}

static const u32 handles[] = { 0x31337000, 0x31337303, 0x3137c0de, 0x313371c3,
			       0x31337a73, 0x31337808, 0x3137af00 };

static int fifo_tests(struct ctx *c)
{
	int i, bad = 0;
	u32 *src = c->xdr + SCRATCH / 4;

	begin(c);
	if (run(c, "empty FIFO with SET_REFERENCE", 100))
		return -EIO;
	P("FIFO runs in the new context: ref %#x", REF(c));

	/* Bind each known object on its usual subchannel, one submission per object */
	for (i = 0; i < ARRAY_SIZE(handles); i++) {
		begin(c);
		out(c, i, 0, handles[i]);
		P("bind %#x on subch %d: %s", handles[i], i, run(c, "bind", 100) ? "FAILED" : "ok");
	}

	/* M2MF (subch 1) copy XDR -> VRAM offset 0, then look at our VRAM allocation */
	for (i = 0; i < 1024; i++)
		src[i] = 0x5a000000 | i;
	memset_io(c->vram, 0, 4096);
	wmb();
	begin(c);
	out(c, 1, 0x184, 0xfeed0001);
	out(c, 1, 0x188, 0xfeed0000);
	*c->p++ = MTH(8, 1, 0x30c);
	*c->p++ = FIFO_IOIF + SCRATCH;
	*c->p++ = VRAM_SCRATCH;
	*c->p++ = 4096;
	*c->p++ = 4096;
	*c->p++ = 4096;
	*c->p++ = 1;
	*c->p++ = 0x101;
	*c->p++ = 0;
	if (run(c, "m2mf", 200))
		return -EIO;
	for (i = 0; i < 1024; i++)
		bad += in_be32(c->vram + i * 4) != src[i];
	P("M2MF XDR -> VRAM in the second context: %d of 1024 words differ", bad);
	return 0;
}

/* Map our 1 MB buffer at various IO addresses of a context (never below min_io), unmap again */
static void iomap_tests(const char *who, u64 handle, u64 xdr_lpar, u64 min_io)
{
	static const u64 at[] = { 0x100000, 0x1000000, 0x2000000, 0x8000000, 0xff00000,
				  0x10000000, 0x1ff00000, 0x20000000, 0x80000000 };
	struct page *big;
	int i, s;

	for (i = 0; i < ARRAY_SIZE(at); i++) {
		if (at[i] < min_io)
			continue;
		s = lv1_gpu_context_iomap(handle, at[i], xdr_lpar, XDR_SIZE, IOPTE_RW);
		P("%s: iomap 1 MB at IOIF %#llx: %d", who, at[i], s);
		if (!s)
			lv1_gpu_context_iomap(handle, at[i], xdr_lpar, XDR_SIZE, CBE_IOPTE_M);
	}
	s = lv1_gpu_context_iomap(handle, 0x2000000, xdr_lpar, 0x10000, IOPTE_RW);
	P("%s: iomap 64 KB at IOIF 0x2000000: %d", who, s);
	if (!s)
		lv1_gpu_context_iomap(handle, 0x2000000, xdr_lpar, 0x10000, CBE_IOPTE_M);

	big = alloc_pages(GFP_KERNEL | __GFP_NOWARN, 12);
	if (!big) {
		P("%s: no contiguous 16 MB block for the large iomap test", who);
		return;
	}
	s = lv1_gpu_context_iomap(handle, 0x4000000, ps3_mm_phys_to_lpar(page_to_phys(big)),
				  16 << 20, IOPTE_RW);
	P("%s: iomap 16 MB in one call at IOIF 0x4000000: %d", who, s);
	if (!s)
		lv1_gpu_context_iomap(handle, 0x4000000, ps3_mm_phys_to_lpar(page_to_phys(big)),
				      16 << 20, CBE_IOPTE_M);
	__free_pages(big, 12);
}

static void fault_test(struct ctx *c)
{
	int s;

	begin(c);
	out(c, 3, 0x30c, 0);			/* M2MF method on the surface-2D object: faults */
	run(c, "fault", 200);
	P("after fault: get %#x ref %#x", GET(c), REF(c));

	s = lv1_gpu_context_attribute(c->handle, L1GPU_CONTEXT_ATTRIBUTE_FIFO_PAUSE, 0, 0, 0, 0);
	P("FIFO_PAUSE: %d", s);
	s = lv1_gpu_context_attribute(c->handle, L1GPU_CONTEXT_ATTRIBUTE_FIFO_RESUME, 0, 0, 0, 0);
	P("FIFO_RESUME: %d", s);
	begin(c);
	if (!run(c, "after pause/setup/resume", 100)) {
		P("RECOVERED by pause/setup/resume");
		return;
	}
	ctx_free(c);
	if (ctx_alloc(c))
		return;
	begin(c);
	P("%s by context free + reallocate",
	  run(c, "after reallocate", 100) ? "NOT recovered" : "RECOVERED");
}

static int __init probe_init(void)
{
	struct ctx c = { .token = 0x7e000000 };
	u64 fb_ctx;
	unsigned long fb_xdr_size;
	void *fb_xdr;
	struct page *pg;
	int s;

	pg = alloc_pages(GFP_KERNEL | __GFP_ZERO, XDR_ORDER);
	if (!pg)
		return -ENOMEM;
	c.xdr = page_address(pg);
	c.xdr_lpar = ps3_mm_phys_to_lpar(page_to_phys(pg));
	ps3fb_gpu_handles(&c.mem, &fb_ctx, &c.ddr_lpar, &fb_xdr, &fb_xdr_size);
	P("ps3fb: memory handle %#llx context %#llx ddr_lpar %#llx xdr %#lx bytes", c.mem, fb_ctx,
	  c.ddr_lpar, fb_xdr_size);
	P("our XDR buffer: phys %#llx lpar %#llx", (u64)page_to_phys(pg), c.xdr_lpar);

	/* ps3fb's context, strictly above its own window */
	iomap_tests("ps3fb ctx", fb_ctx, c.xdr_lpar, ALIGN(fb_xdr_size, 1 << 20));

	c.vram = ioremap(c.ddr_lpar + VRAM_SCRATCH, 1 << 20);
	c.flags = flags;
	s = ctx_alloc(&c);
	if (s && c.flags) {
		ctx_free(&c);
		c.flags = 0;
		s = ctx_alloc(&c);
	}
	if (!s) {
		iomap_tests("second ctx", c.handle, c.xdr_lpar, 0x100000);
		if (!fifo_tests(&c) && fault)
			fault_test(&c);
	}
	ctx_free(&c);
	if (c.vram)
		iounmap(c.vram);
	__free_pages(pg, XDR_ORDER);
	P("done");
	return 0;
}

static void __exit probe_exit(void) { }

module_init(probe_init);
module_exit(probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Probe a second RSX context");
