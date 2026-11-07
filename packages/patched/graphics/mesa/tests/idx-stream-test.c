/* idx-stream-test: Mesa 0072/0073 (nv30_vbo.c's indexed draws on the PS3) as ps3rsx sees the stream.
 * nv30_vbo.c is compiled in whole against a fake push buffer that keeps the words and ends a
 * submission wherever the winsys would (a buffer of `room` words runs out, or a kick); the stream is
 * decoded and run through a model of ps3rsx's index rules (validate.c ib_close, idxsnap.h):
 *   - a binding's batches hold at most 1 MiB with the copy's skew (the cap);
 *   - a submission's copies, with the one the kernel keeps for a primitive the previous submission
 *     left open (up to 1 MiB), stay within the file's share (1.25 MiB); every draw counted as a
 *     fresh copy, as Mesa counts it, which is also what the kernel scans;
 *   - no submission starts with batches through a binding it doesn't hold (a flush between the
 *     binding and BEGIN), and no piece of a split draw ends a submission inside its primitive.
 * Parts:
 *   share: a split draw, then 300 KiB of plain draws, in one context; and a kick inside a plain
 *     draw's primitive followed by more plain draws (the review's M1);
 *   space: split and plain draws through small push buffers, every flush point tried (M2);
 *   cancel: a cancelled kick before a piece gives up the rest of the draw (L5);
 *   golden ("dump"): the words of draws under the cap, for host-tests.sh's diff against nv30_vbo.c
 *     before 0072: identical.
 *   idx-stream-test [share|space|cancel|dump] */
#include "nv30/nv30_vbo.c"
#include "nv30/nv30_idx_split.h"	/* (the cap's constants, also for nv30_vbo.c before 0072) */

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { if (++fails < 20) { printf("FAIL %s:%d: ", __FILE__, \
	__LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* --- the fake push buffer: submissions recorded as word ranges ----------------------------- */
static uint32_t mem[1 << 22];
static size_t used, room = 1 << 20;
static size_t sub_start[1 << 16];
static unsigned nsub, cancel_next;
static struct nv30_context *ctx;
static struct nouveau_device dev;

static void flush(struct nouveau_pushbuf *push)
{
	used = push->cur - mem;
	sub_start[++nsub] = used;		/* a submission ends here */
	/* nv30_context_kick_notify's count (nv30_context.c) */
	ctx->ps3_idx_bytes = ctx->ps3_idx_open ? NV30_PS3_IDX_BINDING_MAX : 0;
	if (cancel_next) {
		cancel_next--;
		dev.cancelled++;
	}
	push->end = push->cur + room;
}

int nouveau_pushbuf_space(struct nouveau_pushbuf *push, uint32_t dwords, uint32_t relocs, uint32_t pushes)
{
	(void)relocs; (void)pushes;
	if (!push->cur) {
		push->cur = mem;
		push->end = mem + room;
	} else if (push->cur + dwords >= push->end) {
		flush(push);
	}
	return 0;
}
int nouveau_pushbuf_kick(struct nouveau_pushbuf *p) { flush(p); return 0; }
struct nouveau_bufref *nouveau_bufctx_mthd(struct nouveau_bufctx *c, int bin, uint32_t packet, struct nouveau_bo *bo,
					   uint64_t data, uint32_t flags, uint32_t vor, uint32_t tor)
{
	static struct nouveau_bufref ref;

	(void)c; (void)bin; (void)packet; (void)bo; (void)data; (void)flags; (void)vor; (void)tor;
	return &ref;
}
void nouveau_bufctx_reset(struct nouveau_bufctx *c, int bin) { (void)c; (void)bin; }
int mtx_lock(mtx_t *m) { (void)m; return 0; }
int mtx_unlock(mtx_t *m) { (void)m; return 0; }
void _simple_mtx_plain_init_once(simple_mtx_t *m) { (void)m; }
void util_call_once_data_slow(once_flag *once, util_call_once_data_func func, const void *val) { (void)once; func(val); }
void mesa_log(enum mesa_log_level l, const char *tag, const char *f, ...) { (void)l; (void)tag; (void)f; }
int64_t os_time_get_nano(void) { return 0; }

/* --- the kernel's model -------------------------------------------------------------------- */
struct kstat { unsigned bindings, over_cap, over_share, half, split_open; uint64_t most; };

/* the stream mem[0, used) in its submissions; pieces: binding offsets that must not end a
 * submission inside their primitive (every binding counts, when strict) */
static struct kstat kernel(bool strict)
{
	struct kstat st = { 0 };
	bool have_bind = false, open = false;
	uint64_t share = 0, lo = UINT64_MAX, hi = 0;
	uint32_t off = 0, fmt = 0;

	sub_start[nsub + 1] = used;
	for (unsigned s = 0; s <= nsub; s++) {
		size_t w = sub_start[s], end = sub_start[s + 1];

		share = open ? NV30_PS3_IDX_BINDING_MAX : 0;	/* the tail copy the kernel keeps */
		have_bind = false;
		while (w < end) {
			uint32_t h = mem[w++], n = (h >> 18) & 0x7ff, m = h & 0x1ffc;
			bool ni = h & 0x40000000;

			for (uint32_t i = 0; i < n && w < end; i++, w++) {
				uint32_t mm = ni ? m : m + 4 * i, v = mem[w];

				if (mm == 0x181c || mm == 0x1820) {
					if (hi > lo) {	/* the binding before closes */
						uint32_t isz = fmt & 0x10 ? 2 : 4;
						uint64_t len = (lo * isz & 255) + (hi - lo) * isz;

						st.over_cap += len > NV30_PS3_IDX_BINDING_MAX;
						share += (len + 255) & ~255ull;
					}
					lo = UINT64_MAX; hi = 0;
					if (mm == 0x181c) off = v; else fmt = v;
					have_bind = true;
					st.bindings += mm == 0x181c;
				} else if (mm == 0x1808) {
					if (v && !have_bind && !open)
						st.half++;	/* BEGIN through a binding of another submission */
					open = v != 0;
				} else if (mm == 0x1824) {
					uint64_t a = v & 0xffffff, b = a + (v >> 24) + 1;

					if (!have_bind && !open)
						st.half++;
					if (have_bind) {	/* (a continuation reads the kept copy) */
						lo = MIN2(lo, a);
						hi = MAX2(hi, b);
					}
				}
			}
		}
		if (hi > lo) {		/* the submission's end closes the binding */
			uint32_t isz = fmt & 0x10 ? 2 : 4;
			uint64_t len = (lo * isz & 255) + (hi - lo) * isz;

			st.over_cap += len > NV30_PS3_IDX_BINDING_MAX;
			share += (len + 255) & ~255ull;
		}
		lo = UINT64_MAX; hi = 0;
		st.over_share += share > (1280u << 10);
		st.most = MAX2(st.most, share);
		st.split_open += strict && open && s < nsub;
		(void)off;
	}
	return st;
}

/* --- the context and the draws ------------------------------------------------------------- */
static struct nv30_screen screen;
static struct nouveau_object eng3d;
static struct nouveau_pushbuf_priv ppush;
static struct nouveau_bo ibo = { .offset = 0x100000, .flags = NOUVEAU_BO_GART };
static struct nv04_resource ires = { .bo = &ibo, .domain = NOUVEAU_BO_GART };

static void reset(size_t r)
{
	struct nouveau_pushbuf *push = ctx->base.pushbuf;

	room = r;
	used = 0;
	nsub = 0;
	sub_start[0] = 0;
	push->cur = push->end = NULL;
	nouveau_pushbuf_space(push, 0, 0, 0);
	ctx->ps3_idx_bytes = 0;
	ctx->ps3_idx_open = false;
}

static void setup(void)
{
	struct nouveau_pushbuf *push = calloc(1, sizeof(*push));

	ctx = calloc(1, sizeof(*ctx));
	ppush.screen = &screen.base;
	ppush.context = &ctx->base;
	push->user_priv = &ppush;
	eng3d.oclass = NV40_3D_CLASS;
	screen.eng3d = &eng3d;
	screen.base.is_ps3 = true;
	screen.base.device = &dev;
	screen.idx_split = true;
	ctx->screen = &screen;
	ctx->base.pushbuf = push;
	ctx->base.pipe.screen = &screen.base.base;
}

/* one indexed draw of count indices (isz bytes, mode) from position start of the index buffer */
static void draw(unsigned mode, unsigned start, unsigned count, unsigned isz)
{
	struct pipe_draw_info info = { .index_size = isz, .mode = mode };

	info.index.resource = &ires.base;
#ifdef IDX_STREAM_BEFORE_0072
	nv30_draw_elements(ctx, false, &info, mode, start, count, 1, 0, isz, NULL, 0);
#else
	nv30_draw_elements(ctx, false, &info, mode, start, count, 1, 0, isz, NULL, 0, NULL, isz, false);
#endif
}

static void finish(void)
{
	used = ctx->base.pushbuf->cur - mem;
}

#ifndef IDX_STREAM_BEFORE_0072
static void part_share(void)
{
	struct kstat st;

	/* a 2 MiB split draw, then 300 KiB of plain U16 draws (the review's M1 a) */
	reset(1 << 20);
	draw(MESA_PRIM_TRIANGLES, 0, 524031, 4);
	for (int i = 0; i < 30; i++)
		draw(MESA_PRIM_TRIANGLES, 0, 5121, 2);	/* 10 KiB each */
	finish();
	st = kernel(false);
	CHECK(!st.over_cap && !st.over_share && !st.half, "share a: %u over the cap, %u submissions over the "
	      "share (most %llu), %u half bindings", st.over_cap, st.over_share, (unsigned long long)st.most, st.half);
	/* plain draws after a flush inside one's primitive (M1 b): a push buffer just short of a 400 KiB
	 * draw's batches */
	reset(5 + 2 + 400 + 2 + 600);
	draw(MESA_PRIM_TRIANGLES, 0, 102399, 4);	/* 400 batch words */
	for (int i = 0; i < 40; i++)
		draw(MESA_PRIM_TRIANGLES, 0, 15360, 2);	/* 30 KiB each */
	finish();
	st = kernel(false);
	CHECK(!st.over_cap && !st.over_share && !st.half, "share b: %u over the cap, %u submissions over the "
	      "share (most %llu), %u half bindings", st.over_cap, st.over_share, (unsigned long long)st.most, st.half);
}

static void part_space(void)
{
	/* every flush point: push buffers from 2100 words (a U16 piece takes 2048) to 6200, a split
	 * draw and plain draws (the PS3's push buffers are 64 Ki words) */
	unsigned bad = 0, tried = 0, splits = 0;

	for (size_t r = 2100; r <= 6200; r += 13) {
		struct kstat st;

		reset(r);
		draw(MESA_PRIM_TRIANGLES, 0, 600000, 2);	/* U16: two pieces of 1020 batch words */
		for (int i = 0; i < 20; i++)
			draw(MESA_PRIM_TRIANGLE_STRIP, 0, 1000 + 97 * i, 2);
		draw(MESA_PRIM_TRIANGLES, 3, 300000, 4);	/* U32: two pieces */
		finish();
		st = kernel(false);
		tried++;
		splits += nsub;
		if (st.over_cap || st.over_share || st.half) {
			if (bad++ < 3)
				printf("  room %zu: %u over the cap, %u over the share, %u half bindings\n", r,
				       st.over_cap, st.over_share, st.half);
		}
	}
	CHECK(!bad, "space: %u of %u push buffer sizes end a submission where the kernel refuses", bad, tried);
	CHECK(splits > tried, "space: the push buffers never filled (%u submissions in %u runs)", splits, tried);
	/* pieces never end a submission inside their primitive */
	{
		unsigned open_ends = 0;

		for (size_t r = 2100; r <= 6200; r += 13) {
			reset(r);
			draw(MESA_PRIM_TRIANGLES, 0, 600000, 2);
			finish();
			open_ends += kernel(true).split_open;
		}
		CHECK(!open_ends, "space: %u submissions end inside a piece's primitive", open_ends);
	}
}

static void part_cancel(void)
{
	unsigned b;

	reset(1 << 20);
	draw(MESA_PRIM_TRIANGLES, 0, 5121, 2);		/* something in the submission */
	ctx->ps3_idx_bytes = NV30_PS3_IDX_SUBMIT_BYTES;	/* so the first piece kicks */
	cancel_next = 1;
	draw(MESA_PRIM_TRIANGLES, 0, 524031, 4);
	finish();
	b = kernel(false).bindings;
	CHECK(b == 1, "cancel: %u bindings, want the plain draw's only (the split draw given up after its "
	      "cancelled kick)", b);
	CHECK(screen.idx_split_dropped == 1, "cancel: dropped count %u", screen.idx_split_dropped);
}
#endif

/* the words of draws under the cap, one per line, for the golden diff (no flush: a big buffer) */
static void part_dump(void)
{
	reset(1 << 22);
	draw(MESA_PRIM_TRIANGLES, 0, 999, 2);
	draw(MESA_PRIM_TRIANGLE_STRIP, 17, 5000, 2);
	draw(MESA_PRIM_TRIANGLES, 100, 200001, 4);
	draw(MESA_PRIM_TRIANGLE_FAN, 0, 300, 4);
	draw(MESA_PRIM_LINE_LOOP, 5, 70000, 2);
	draw(MESA_PRIM_POINTS, 0, 1, 2);
	finish();
	for (size_t i = 0; i < used; i++)
		printf("%08x\n", mem[i]);
}

int main(int argc, char **argv)
{
	const char *p = argc > 1 ? argv[1] : "all";

	setup();
	if (!strcmp(p, "dump")) {
		part_dump();
		return 0;
	}
#ifndef IDX_STREAM_BEFORE_0072
	if (!strcmp(p, "share") || !strcmp(p, "all"))
		part_share();
	if (!strcmp(p, "space") || !strcmp(p, "all"))
		part_space();
	if (!strcmp(p, "cancel") || !strcmp(p, "all"))
		part_cancel();
#endif
	printf("idx-stream-test %s: %d checks, %d failed\n", p, checks, fails);
	return fails != 0;
}
