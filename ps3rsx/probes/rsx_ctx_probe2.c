// SPDX-License-Identifier: GPL-2.0
/*
 * rsx_ctx_probe2: Phase 0 hardware tests for the ps3rsx driver design (docs/DRIVER-PLAN.md).
 * One test per load; everything is freed again before init returns.
 *
 *   insmod rsx_ctx_probe2.ko test=<name>
 *
 * Every test context is allocated on ps3fb's memory handle and first runs ps3fb's own reset
 * program (copied from ps3fb's XDR area and CALLed, exactly as ps3fb does on channel 0), so it
 * starts with the object/DMA setup known to work on this console.
 *
 * Can't wedge the GPU (as far as we know):
 *   ctxcount  E1  allocate up to n contexts (until it fails), free them. NB: allocating up to
 *                 the limit (2 extra, then -2) and freeing stalled every FIFO, ps3fb's too
 *   iomap     E2  scattered 1 MB iomaps, unmap one, remap elsewhere
 *   irq       E7  does the context's own IRQ outlet deliver vsync?
 *   wc        E8  VRAM write speed, guarded vs write-combined mapping (kernel side)
 * Could wedge (new kinds of GPU work), one per boot when unsure:
 *   reset         run ps3fb's reset program in a fresh context (prerequisite for the rest)
 *   gartreach E9  M2MF to/from chunks iomapped late at IO 64 MB and 255 MB
 *   vram224   E10 M2MF to VRAM just below 224 MB
 *   vram254   E10 M2MF to VRAM between 224 and 254 MB (beyond what ps3fb assumes)
 *   refsync   E11 is the 3D clear finished when SET_REFERENCE lands?
 *   callret   E3  CALL into another chunk at an odd offset, RETURN
 *   swizzle   E4  3D back-end release of a pre-swizzled value
 *   twoctx    E5  two contexts, alternating clears: is 3D state per channel?
 *   flip      E6  lv1 display flip from a second context (stop ps3fb's flip task first)
 *   notify    E14 where does the 0x66604200 notifier write?
 */
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/cell-regs.h>
#include <asm/lv1call.h>
#include <asm/ps3.h>
#include <asm/ps3gpu.h>

static char *test = "";
module_param(test, charp, 0444);
static int n = 8;
module_param(n, int, 0444);		/* ctxcount: how many contexts to try */

#define P(fmt, ...) pr_info("rsxprobe2: " fmt "\n", ##__VA_ARGS__)
#define IOPTE_RW (CBE_IOPTE_PP_W | CBE_IOPTE_PP_R | CBE_IOPTE_M)
#define MTH(count, subch, method) (((count) << 18) | ((subch) << 13) | (method))
#define CHUNK_ORDER 8			/* 1 MB, the IO page size */
#define CHUNK_SIZE (PAGE_SIZE << CHUNK_ORDER)
#define RESET_SIZE 0x9b8		/* ps3fb's reset program, ends with RETURN */
#define RING 0x1000			/* commands start here in chunk 0 (IO 0) */
#define VRAM_SCRATCH 0x8000000		/* 128 MB: far above ps3fb's frames */
#define CTRL_SIZE 0x1000

/* From ps3fb.c, verbatim, so the offsets match */
struct display_buffer {
	u64 beTime_stamp;
	u32 status, offset, res1, res2, field, reserved1;
	u64 res3;
	u32 raster;
	u64 vblank_count;
	u32 fieldVsync, reserved2;
};
struct gpu_graph_exception_info {
	u32 channel_id, cause, res1[8], dma_put, dma_get, call, jump, res2;
	u32 fifo_put, fifo_get, fifo_ref, fifo_cache[512], graph_fifo[512];
};
struct gpu_irq {
	u32 irq_outlet, status, mask, video_cause, graph_cause, user_cause, res[8];
	struct gpu_graph_exception_info graph_exception_info;
};
struct gpu_driver_info {
	u32 version_driver, version_gpu, memory_size, hardware_channel;
	u32 nvcore_frequency, memory_frequency, reserved[6], labelArea, reportDataArea;
	u32 reserved2[1056];
	struct display_buffer head[8];
	struct gpu_irq irq;
};

void ps3fb_gpu_handles(u64 *memory_handle, u64 *context_handle, u64 *ddr_lpar,
		       void **xdr, unsigned long *xdr_size);

static u64 fb_mem, fb_ctx, ddr_lpar;
static void *fb_xdr;
static unsigned long fb_xdr_size;

struct chunk {
	struct page *pg;
	u32 *va;
	u64 lpar;
};

struct ctx {
	u64 handle, ctrl_lpar, dinfo_lpar, reports_lpar, reports_size;
	void __iomem *ctrl;
	volatile struct gpu_driver_info __iomem *dinfo;
	void __iomem *reports;
	struct chunk fifo;		/* chunk 0, iomapped at IO 0 */
	u32 *p, token;
};

static int chunk_alloc(struct chunk *ch)
{
	ch->pg = alloc_pages(GFP_KERNEL | __GFP_ZERO | __GFP_NOWARN, CHUNK_ORDER);
	if (!ch->pg)
		return -ENOMEM;
	ch->va = page_address(ch->pg);
	ch->lpar = ps3_mm_phys_to_lpar(page_to_phys(ch->pg));
	return 0;
}

static void chunk_free(struct chunk *ch)
{
	if (ch->pg)
		__free_pages(ch->pg, CHUNK_ORDER);
	ch->pg = NULL;
}

static int iomap(struct ctx *c, u64 io, struct chunk *ch)
{
	return lv1_gpu_context_iomap(c->handle, io, ch->lpar, CHUNK_SIZE, IOPTE_RW);
}

static void iounmap_io(struct ctx *c, u64 io, struct chunk *ch)
{
	lv1_gpu_context_iomap(c->handle, io, ch->lpar, CHUNK_SIZE, CBE_IOPTE_M);
}

static void ctx_free(struct ctx *c)
{
	if (c->ctrl)
		iounmap(c->ctrl);
	if (c->dinfo)
		iounmap((void __iomem *)c->dinfo);
	if (c->reports)
		iounmap(c->reports);
	if (c->handle) {
		iounmap_io(c, 0, &c->fifo);
		P("context %#llx free: %d", c->handle, lv1_gpu_context_free(c->handle));
	}
	chunk_free(&c->fifo);
	memset(c, 0, sizeof(*c));
}

/* Allocate a context on ps3fb's memory handle, map its areas, put our FIFO chunk at IO 0 */
static int ctx_alloc(struct ctx *c)
{
	int s;

	memset(c, 0, sizeof(*c));
	c->token = 0x7e000000;
	s = lv1_gpu_context_allocate(fb_mem, 0x820, &c->handle, &c->ctrl_lpar, &c->dinfo_lpar,
				     &c->reports_lpar, &c->reports_size);
	if (s) {
		P("context_allocate: %d", s);
		c->handle = 0;
		return s;
	}
	c->ctrl = ioremap(c->ctrl_lpar, CTRL_SIZE);
	c->dinfo = (void __iomem *)ioremap(c->dinfo_lpar, sizeof(struct gpu_driver_info));
	c->reports = ioremap(c->reports_lpar, c->reports_size);
	if (!c->ctrl || !c->dinfo || !c->reports || chunk_alloc(&c->fifo)) {
		ctx_free(c);
		return -ENOMEM;
	}
	s = iomap(c, 0, &c->fifo);
	if (s) {
		P("iomap FIFO chunk: %d", s);
		ctx_free(c);
		return s;
	}
	P("context %#llx: hw channel %u", c->handle, c->dinfo->hardware_channel);
	return 0;
}

#define GET(c) in_be32((c)->ctrl + 0x44)
#define REF(c) in_be32((c)->ctrl + 0x48)
#define LABEL(c, o) in_be32((c)->reports + (o))

static void begin(struct ctx *c)
{
	c->p = c->fifo.va + RING / 4;
}

static void out(struct ctx *c, u32 subch, u32 mth, u32 val)
{
	*c->p++ = MTH(1, subch, mth);
	*c->p++ = val;
}

/* Start the commands at RING (get = put = RING), kick, return once ref == token */
static int kick(struct ctx *c)
{
	u32 put = (u32)((c->p - c->fifo.va) * 4);
	int s;

	wmb();
	s = lv1_gpu_context_attribute(c->handle, L1GPU_CONTEXT_ATTRIBUTE_FIFO_SETUP,
				      RING, RING, 0, 0);
	if (s) {
		P("FIFO_SETUP: %d", s);
		return s;
	}
	out_be32(c->ctrl + 0x40, put);
	return 0;
}

static int wait_ref(struct ctx *c, u32 token, int ms, const char *what)
{
	int i;

	for (i = 0; i < ms * 10; i++) {
		if (REF(c) == token)
			return 0;
		udelay(100);
	}
	P("%s: TIMEOUT get %#x ref %#x (want %#x) graph_cause %#x", what, GET(c), REF(c), token,
	  c->dinfo->irq.graph_cause);
	return -ETIMEDOUT;
}

static int run(struct ctx *c, const char *what, int ms)
{
	out(c, 0, 0x50, ++c->token);
	return kick(c) ?: wait_ref(c, c->token, ms, what);
}

/* ps3fb's reset program, copied into our chunk at IO 0 and CALLed like ps3fb does */
static int reset_program(struct ctx *c)
{
	memcpy(c->fifo.va, fb_xdr, RESET_SIZE);
	begin(c);
	*c->p++ = 0 | 2;		/* CALL IO 0 */
	if (run(c, "reset program", 200))
		return -EIO;
	P("reset program ran in context %#llx", c->handle);
	return 0;
}

static int ctx_ready(struct ctx *c)
{
	return ctx_alloc(c) ?: reset_program(c);
}

/* M2MF (subch 1, set up by the reset program) linear copy, one line per 4 KB */
static void m2mf(struct ctx *c, u32 dma_in, u32 in, u32 dma_out, u32 o, u32 len)
{
	u32 line = min(len, 4096u);

	out(c, 1, 0x184, dma_in);
	out(c, 1, 0x188, dma_out);
	*c->p++ = MTH(8, 1, 0x30c);
	*c->p++ = in;
	*c->p++ = o;
	*c->p++ = line;
	*c->p++ = line;
	*c->p++ = line;
	*c->p++ = len / line;
	*c->p++ = 0x101;
	*c->p++ = 0;
}

static void fill(u32 *p, u32 words, u32 seed)
{
	u32 i;

	for (i = 0; i < words; i++)
		p[i] = seed ^ (i * 2654435761u);
}

static int check(const u32 *p, u32 words, u32 seed)
{
	u32 i, bad = 0;

	for (i = 0; i < words; i++)
		bad += p[i] != (seed ^ (i * 2654435761u));
	return bad;
}

/* --- tests ----------------------------------------------------------------------------- */

static void t_ctxcount(void)
{
	struct ctx c[8];
	int i;

	for (i = 0; i < min_t(int, n, ARRAY_SIZE(c)); i++)
		if (ctx_alloc(&c[i]))
			break;
	P("E1: allocated %d contexts besides ps3fb's (asked for %d)", i, n);
	while (i--)
		ctx_free(&c[i]);
}

static void t_iomap(void)
{
	static const u64 io[] = { 0x100000, 0x1300000, 0x2600000, 0x3900000, 0x4c00000,
				  0x7f00000, 0xa000000, 0xff00000 };
	struct chunk ch[ARRAY_SIZE(io)] = {};
	struct ctx c = {};
	int i, s;

	if (ctx_alloc(&c))
		return;
	for (i = 0; i < ARRAY_SIZE(io); i++) {
		if (chunk_alloc(&ch[i])) {
			P("E2: chunk %d: no contiguous 1 MB", i);
			break;
		}
		s = iomap(&c, io[i], &ch[i]);
		P("E2: chunk %d lpar %#llx -> IO %#llx: %d", i, ch[i].lpar, io[i], s);
	}
	iounmap_io(&c, io[3], &ch[3]);
	P("E2: remap chunk 3 at IO 0x5000000: %d", iomap(&c, 0x5000000, &ch[3]));
	P("E2: map chunk 4 again at a second IO 0x6000000 (alias): %d", iomap(&c, 0x6000000, &ch[4]));
	iounmap_io(&c, 0x5000000, &ch[3]);
	iounmap_io(&c, 0x6000000, &ch[4]);
	for (i = 0; i < ARRAY_SIZE(io); i++) {
		if (ch[i].pg && i != 3)
			iounmap_io(&c, io[i], &ch[i]);
		chunk_free(&ch[i]);
	}
	ctx_free(&c);
}

static irqreturn_t irq_handler(int irq, void *data)
{
	struct ctx *c = data;
	u64 v = 0;

	lv1_gpu_context_intr(c->handle, &v);
	if (v & 1)
		c->token++;		/* vsync on head A */
	return IRQ_HANDLED;
}

static void t_irq(void)
{
	unsigned int virq;
	struct ctx c = {};
	int s;

	if (ctx_alloc(&c))
		return;
	P("E7: irq outlet %#x", c.dinfo->irq.irq_outlet);
	s = ps3_irq_plug_setup(PS3_BINDING_CPU_ANY, c.dinfo->irq.irq_outlet, &virq);
	if (s) {
		P("E7: irq plug setup: %d", s);
		goto out;
	}
	c.token = 0;
	s = request_irq(virq, irq_handler, 0, "rsxprobe2", &c);
	if (!s) {
		c.dinfo->irq.mask = (1 << 0) | (1 << 2) | (1 << 3);	/* vsync A, exception, flip A */
		msleep(1000);
		c.dinfo->irq.mask = 0;
		P("E7: %u vsync interrupts in 1 s on the second context's outlet", c.token);
		free_irq(virq, &c);
	} else {
		P("E7: request_irq: %d", s);
	}
	ps3_irq_plug_destroy(virq);
out:
	ctx_free(&c);
}

static void t_wc(void)
{
	const size_t len = 4 << 20;
	void __iomem *g = ioremap(ddr_lpar + VRAM_SCRATCH, len);
	void __iomem *w = ioremap_wc(ddr_lpar + VRAM_SCRATCH, len);
	ktime_t t;
	size_t i;

	if (g && w) {
		t = ktime_get();
		for (i = 0; i < len; i += 8)
			__raw_writeq(i, g + i);
		mb();
		P("E8: guarded 8-byte stores: %lld MB/s",
		  div64_s64(4000000LL, ktime_us_delta(ktime_get(), t) ?: 1));
		t = ktime_get();
		for (i = 0; i < len; i += 8)
			__raw_writeq(i, w + i);
		mb();
		P("E8: write-combined 8-byte stores: %lld MB/s",
		  div64_s64(4000000LL, ktime_us_delta(ktime_get(), t) ?: 1));
		P("E8: readback check %s", __raw_readq(g + len - 8) == len - 8 ? "ok" : "MISMATCH");
	}
	if (g)
		iounmap(g);
	if (w)
		iounmap(w);
}

static void t_reset(void)
{
	struct ctx c = {};

	if (!ctx_ready(&c))
		P("reset: OK, ref %#x", REF(&c));
	ctx_free(&c);
}

/* M2MF from/to chunks that were iomapped after the context was created, at high IO */
static void t_gartreach(void)
{
	static const u64 io[] = { 0x4000000, 0xff00000 };
	struct chunk ch[2] = {};
	struct ctx c = {};
	int i;

	if (ctx_ready(&c))
		goto out;
	for (i = 0; i < 2; i++) {
		if (chunk_alloc(&ch[i]) || iomap(&c, io[i], &ch[i])) {
			P("E9: chunk %d setup failed", i);
			goto out;
		}
	}
	fill(ch[0].va, 1024, 0x0e900000);
	memset(ch[1].va, 0, 4096);
	begin(&c);
	m2mf(&c, 0xfeed0001, io[0], 0xfeed0000, VRAM_SCRATCH, 4096);
	m2mf(&c, 0xfeed0000, VRAM_SCRATCH, 0xfeed0001, io[1], 4096);
	if (!run(&c, "gartreach", 200))
		P("E9: IO 64 MB -> VRAM -> IO 255 MB: %d of 1024 words differ",
		  check(ch[1].va, 1024, 0x0e900000));
out:
	for (i = 0; i < 2; i++) {
		if (ch[i].pg)
			iounmap_io(&c, io[i], &ch[i]);
		chunk_free(&ch[i]);
	}
	ctx_free(&c);
}

/* M2MF XDR -> VRAM offset -> XDR; the CPU never touches VRAM beyond 224 MB */
static void vram_reach(u32 off)
{
	u32 *src, *dst;
	struct ctx c = {};

	if (ctx_ready(&c))
		goto out;
	src = c.fifo.va + 0x80000 / 4;
	dst = c.fifo.va + 0xc0000 / 4;
	fill(src, 1024, off);
	memset(dst, 0, 4096);
	begin(&c);
	m2mf(&c, 0xfeed0001, 0x80000, 0xfeed0000, off, 4096);
	m2mf(&c, 0xfeed0000, off, 0xfeed0001, 0xc0000, 4096);
	if (!run(&c, "vram reach", 200))
		P("E10: VRAM offset %#x round trip: %d of 1024 words differ", off,
		  check(dst, 1024, off));
out:
	ctx_free(&c);
}

/* 3D surface at a VRAM offset, as rsx-clear sets it up */
static void surface(struct ctx *c, u32 off, u32 w, u32 h)
{
	out(c, 0, 0x194, 0xfeed0000);
	out(c, 0, 0x200, w << 16);
	out(c, 0, 0x204, h << 16);
	out(c, 0, 0x208, 0x148);
	out(c, 0, 0x20c, w * 4);
	out(c, 0, 0x210, off);
	out(c, 0, 0x220, 1);
	out(c, 0, 0x2b8, 0);
	out(c, 0, 0x8c0, w << 16);
	out(c, 0, 0x8c4, h << 16);
	out(c, 0, 0x324, 0x01010101);
}

static void clear(struct ctx *c, u32 colour)
{
	out(c, 0, 0x1d90, colour);
	out(c, 0, 0x1d94, 0xf0);
}

/* Does ref arrive only after the 3D clear has hit memory? Poll ref, read the last pixel */
static void t_refsync(void)
{
	const u32 w = 4096, h = 2048, last = VRAM_SCRATCH + (w * h - 1) * 4;
	void __iomem *px = ioremap(ddr_lpar + (last & ~0xfffu), 0x1000);
	struct ctx c = {};
	int i, stale = 0;
	ktime_t t;

	if (!px || ctx_ready(&c))
		goto out;
	for (i = 0; i < 8; i++) {
		u32 colour = 0xff000000 | (0x10101 * (i + 1)), v;

		begin(&c);
		surface(&c, VRAM_SCRATCH, w, h);
		clear(&c, colour);
		out(&c, 0, 0x50, ++c.token);
		if (kick(&c))
			goto out;
		t = ktime_add_ms(ktime_get(), 500);
		while (REF(&c) != c.token)
			if (ktime_after(ktime_get(), t)) {
				P("E11: ref timeout");
				goto out;
			}
		v = in_be32(px + (last & 0xfff));
		stale += v != colour;
		P("E11: clear %d (32 MB): last pixel %08x when ref landed (want %08x)", i, v,
		  colour);
	}
	P("E11: %d of 8 clears not finished when ref landed", stale);
out:
	if (px)
		iounmap(px);
	ctx_free(&c);
}

static void t_callret(void)
{
	struct chunk sub = {};
	const u64 io = 0x3000000, off = 0x124;	/* odd (but word aligned) offset */
	struct ctx c = {};

	if (ctx_ready(&c) || chunk_alloc(&sub) || iomap(&c, io, &sub))
		goto out;
	sub.va[off / 4 + 0] = MTH(1, 0, 0x50);
	sub.va[off / 4 + 1] = 0xca110001;
	sub.va[off / 4 + 2] = 0x00020000;	/* RETURN */
	begin(&c);
	*c.p++ = (u32)(io + off) | 2;		/* CALL */
	if (!run(&c, "call/return", 200))
		P("E3: CALL into IO %#llx + RETURN: OK (ref %#x)", io + off, REF(&c));
	iounmap_io(&c, io, &sub);
out:
	chunk_free(&sub);
	ctx_free(&c);
}

static u32 swz02(u32 v)
{
	return (v & 0xff00ff00) | ((v & 0xff) << 16) | ((v >> 16) & 0xff);
}

static void t_swizzle(void)
{
	struct ctx c = {};

	if (ctx_ready(&c))
		goto out;
	begin(&c);
	out(&c, 0, 0x1d6c, 0x40);
	out(&c, 0, 0x1d70, swz02(0x12345678));
	if (!run(&c, "swizzle", 100))
		P("E4: back-end release of swz02(0x12345678): label 0x40 = %08x", LABEL(&c, 0x40));
out:
	ctx_free(&c);
}

/* Two contexts; A sets its surface once, then B sets another and clears, then A clears
 * again without re-setting: does A's clear still go to A's surface? */
static void t_twoctx(void)
{
	const u32 w = 64, h = 64, sa = VRAM_SCRATCH, sb = VRAM_SCRATCH + 0x100000;
	void __iomem *v = ioremap(ddr_lpar + VRAM_SCRATCH, 0x200000);
	struct ctx a = {}, b = {};

	if (!v || ctx_ready(&a) || ctx_ready(&b))
		goto out;
	memset_io(v, 0, 0x200000);
	begin(&a);
	surface(&a, sa, w, h);
	clear(&a, 0xffaa0000);
	run(&a, "A clear 1", 100);
	begin(&b);
	surface(&b, sb, w, h);
	clear(&b, 0xff00bb00);
	run(&b, "B clear", 100);
	begin(&a);
	clear(&a, 0xff0000cc);
	run(&a, "A clear 2", 100);
	P("E5: A's surface %08x (want ff0000cc), B's surface %08x (want ff00bb00)",
	  in_be32(v), in_be32(v + 0x100000));
	P("E5: %s", in_be32(v) == 0xff0000cc && in_be32(v + 0x100000) == 0xff00bb00 ?
	  "3D state is per channel" : "3D state is SHARED between channels");
out:
	if (v)
		iounmap(v);
	ctx_free(&b);
	ctx_free(&a);
}

/* Fill a 1080p frame at VRAM_SCRATCH red from a second context and flip to it there */
static void t_flip(void)
{
	struct ctx c = {};
	int s;

	if (ctx_ready(&c))
		goto out;
	begin(&c);
	surface(&c, VRAM_SCRATCH, 1920, 1080);
	clear(&c, 0xffff0000);
	if (run(&c, "fill red", 200))
		goto out;
	s = lv1_gpu_display_flip(c.handle, 0, VRAM_SCRATCH);
	P("E6: flip head 0 from the second context: %d", s);
	s = lv1_gpu_display_flip(c.handle, 1, VRAM_SCRATCH);
	P("E6: flip head 1 from the second context: %d", s);
	msleep(3000);
	P("E6: (screen should have been red for 3 s if the flip took)");
out:
	ctx_free(&c);
}

/* Arm the M2MF notifier (NOTIFY 0x104) and look for the write in our areas */
static void t_notify(void)
{
	u32 *snap = kmalloc(0x10000, GFP_KERNEL);
	struct ctx c = {};
	int i, n = 0;

	if (!snap || ctx_ready(&c))
		goto out;
	memcpy_fromio(snap, c.reports, 0x10000);
	memset(c.fifo.va + 0x80000 / 4, 0, 0x80000);
	begin(&c);
	out(&c, 1, 0x104, 0);			/* NOTIFY: write notifier on completion */
	m2mf(&c, 0xfeed0001, 0x10000, 0xfeed0000, VRAM_SCRATCH, 4096);
	out(&c, 1, 0x100, 0);			/* NOP, completes the notify */
	if (run(&c, "notify", 200))
		goto out;
	for (i = 0; i < 0x10000 / 4; i++)
		if (in_be32(c.reports + i * 4) != snap[i] && n++ < 8)
			P("E14: reports+%#x: %08x -> %08x", i * 4, snap[i],
			  in_be32(c.reports + i * 4));
	for (i = 0x80000 / 4; i < 0x100000 / 4; i++)
		if (c.fifo.va[i] && n++ < 16)
			P("E14: our chunk IO %#x: %08x", i * 4, c.fifo.va[i]);
	P("E14: %d changed words found in reports area and our chunk's upper half", n);
out:
	kfree(snap);
	ctx_free(&c);
}

static const struct { const char *name; void (*fn)(void); } tests[] = {
	{ "ctxcount", t_ctxcount }, { "iomap", t_iomap }, { "irq", t_irq }, { "wc", t_wc },
	{ "reset", t_reset }, { "gartreach", t_gartreach }, { "refsync", t_refsync },
	{ "callret", t_callret }, { "swizzle", t_swizzle }, { "twoctx", t_twoctx },
	{ "flip", t_flip }, { "notify", t_notify },
};

static int __init probe_init(void)
{
	int i;

	ps3fb_gpu_handles(&fb_mem, &fb_ctx, &ddr_lpar, &fb_xdr, &fb_xdr_size);
	if (!strcmp(test, "vram224")) {
		vram_reach(0xdf00000);
		goto done;
	}
	if (!strcmp(test, "vram254")) {
		vram_reach(0xfd00000);
		goto done;
	}
	for (i = 0; i < ARRAY_SIZE(tests); i++)
		if (!strcmp(test, tests[i].name)) {
			tests[i].fn();
			goto done;
		}
	P("unknown test '%s'", test);
done:
	P("done (%s)", test);
	return 0;
}

static void __exit probe_exit(void) { }

module_init(probe_init);
module_exit(probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Phase 0 RSX hardware tests");
