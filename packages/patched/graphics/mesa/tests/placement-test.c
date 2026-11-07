/* placement-test: Mesa patch 0059 (NV30_PS3_IDX_GART, NV30_PS3_FP_GART: where index buffers and fragment
 * programs live, PS3 only; on by default on the PS3 since 0069 and 0070). nouveau_buffer.c and
 * nv30_resource.c are compiled in whole against a fake device that records where buffers go:
 *   default (both switches off): an index buffer is created exactly as before (VRAM for static and
 *     dynamic usage, GART for stream usage), cpu_only never set, a busy buffer takes the staging route;
 *   NV30_PS3_IDX_GART: an index buffer (alone) is created in GART with cpu_only, a vertex buffer, a
 *     buffer that is both, and a non-PS3 screen are not; mapping a busy cpu_only buffer for writing waits for
 *     the GPU and returns the direct map, never a staging area (no GPU copy into it), whatever the
 *     usage flags (discard range included), and DONTBLOCK returns NULL;
 *   "default" (0069, 0070): both switches default on, on the PS3, off elsewhere;
 *   "migrate" (0070): an index buffer in VRAM (a vertex buffer too, or one with no bind) moves to GART at
 *     its first indexed draw (nv30_idxbuf_to_gart): one GPU copy, cpu_only from then on, the push buffer
 *     kicked right after the copy (the kernel's index copy waits for a write still in flight but refuses
 *     one earlier in the same submission), the arrays validated again; a GART buffer is left alone;
 *   "dump": the placement of a list of buffers as lines, for the golden diff in host-tests.sh: with a
 *     switch on only the domain of the index buffers differs from the default's list.
 * (tools/run-tests.sh --only mesa-placement; skipped without build/work/mesa) */
#include "nouveau_buffer.c"
#include "nouveau_mm.c"
#include "nv30/nv30_resource.c"

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

/* --- the fake device ---------------------------------------------------------------------- */
static int gpu_busy, fence_waits, copies, kicks, gart_fail, idle_waits, cancel_kick;
static struct nouveau_device dev;
/* a kick: counted; with cancel_kick, the kernel cancels the submission (0061 counts it) */
int nouveau_pushbuf_kick(struct nouveau_pushbuf *push) { (void)push; kicks++; if (cancel_kick) dev.cancelled++; return 0; }
int nouveau_pushbuf_space(struct nouveau_pushbuf *push, uint32_t dwords, uint32_t relocs, uint32_t pushes)
{ (void)push; (void)dwords; (void)relocs; (void)pushes; return 0; }
int64_t os_time_get_nano(void) { return 0; }
static struct nouveau_fence the_fence;

int nouveau_bo_new(struct nouveau_device *dev, uint32_t flags, uint32_t align, uint64_t size,
		   union nouveau_bo_config *config, struct nouveau_bo **pbo)
{
	(void)dev; (void)align; (void)config;
	if ((flags & NOUVEAU_BO_GART) && gart_fail > 0) {	/* GART full, the next gart_fail times */
		gart_fail--;
		*pbo = NULL;
		return -ENOMEM;
	}
	struct nouveau_bo *bo = calloc(1, sizeof(*bo));
	bo->size = size;
	bo->flags = flags;
	bo->handle = 1;
	bo->map = calloc(1, size);
	*pbo = bo;
	return 0;
}
void nouveau_bo_ref(struct nouveau_bo *bo, struct nouveau_bo **ref)
{
	if (bo)
		bo->handle++;
	if (*ref && --(*ref)->handle == 0) {
		free((*ref)->map);
		free(*ref);
	}
	*ref = bo;
}
int nouveau_bo_map(struct nouveau_bo *bo, uint32_t access, struct nouveau_client *client) { (void)bo; (void)access; (void)client; return 0; }
int nouveau_bo_wait(struct nouveau_bo *bo, uint32_t access, struct nouveau_client *client) { (void)bo; (void)access; (void)client; return 0; }
void nouveau_fence_ref(struct nouveau_fence *f, struct nouveau_fence **r, struct nouveau_screen *s) { (void)s; *r = f; }
bool nouveau_fence_signalled(struct nouveau_fence *f) { (void)f; return !gpu_busy; }
bool nouveau_fence_wait(struct nouveau_fence *f, struct util_debug_callback *d) { (void)f; (void)d; fence_waits++; gpu_busy = 0; return true; }
bool nouveau_fence_wait_idle(struct nouveau_context *nv) { (void)nv; gpu_busy = 0; idle_waits++; return true; }
bool nouveau_fence_work(struct nouveau_fence *f, void (*func)(void *), void *data) { (void)f; func(data); return true; }
void nouveau_fence_unref_bo(void *data) { struct nouveau_bo *bo = data; nouveau_bo_ref(NULL, &bo); }
int nouveau_mesa_debug;
void simple_mtx_init(simple_mtx_t *m, int type) { (void)type; memset(m, 0, sizeof(*m)); }
void simple_mtx_destroy(simple_mtx_t *m) { (void)m; }
int mtx_lock(mtx_t *m) { (void)m; return 0; }
int mtx_trylock(mtx_t *m) { (void)m; return thrd_busy; }	/* simple_mtx_assert_locked wants a held lock */
int mtx_unlock(mtx_t *m) { (void)m; return 0; }
void _simple_mtx_plain_init_once(simple_mtx_t *m) { (void)m; }
void util_call_once_data_slow(once_flag *once, util_call_once_data_func func, const void *val) { (void)once; func(val); }
void u_default_buffer_subdata(struct pipe_context *p, struct pipe_resource *r, unsigned u, unsigned o, unsigned s, const void *d)
{ (void)p; (void)r; (void)u; (void)o; (void)s; (void)d; }
void u_default_texture_subdata(struct pipe_context *p, struct pipe_resource *r, unsigned l, unsigned u, const struct pipe_box *b,
			       const void *d, unsigned s, uintptr_t ls) { (void)p; (void)r; (void)l; (void)u; (void)b; (void)d; (void)s; (void)ls; }
const struct util_format_description *util_format_description(enum pipe_format f) { (void)f; return NULL; }
/* the texture side of nv30_resource.c: not used here */
struct pipe_resource *nv30_miptree_create(struct pipe_screen *s, const struct pipe_resource *t) { (void)s; (void)t; return NULL; }
void nv30_miptree_destroy(struct pipe_screen *s, struct pipe_resource *r) { (void)s; (void)r; }
struct pipe_resource *nv30_miptree_from_handle(struct pipe_screen *s, const struct pipe_resource *t, struct winsys_handle *w)
{ (void)s; (void)t; (void)w; return NULL; }
bool nv30_miptree_get_handle(struct pipe_screen *s, struct pipe_context *c, struct pipe_resource *r, struct winsys_handle *w, unsigned u)
{ (void)s; (void)c; (void)r; (void)w; (void)u; return false; }
void *nv30_miptree_transfer_map(struct pipe_context *p, struct pipe_resource *r, unsigned l, unsigned u, const struct pipe_box *b, struct pipe_transfer **t)
{ (void)p; (void)r; (void)l; (void)u; (void)b; (void)t; return NULL; }
void nv30_miptree_transfer_unmap(struct pipe_context *p, struct pipe_transfer *t) { (void)p; (void)t; }
void nv30_blit(struct pipe_context *p, const struct pipe_blit_info *i) { (void)p; (void)i; }
void nv30_flush_resource(struct pipe_context *p, struct pipe_resource *r) { (void)p; (void)r; }
void nv30_resource_copy_region(struct pipe_context *p, struct pipe_resource *d, unsigned dl, unsigned x, unsigned y, unsigned z,
			       struct pipe_resource *s, unsigned sl, const struct pipe_box *b)
{ (void)p; (void)d; (void)dl; (void)x; (void)y; (void)z; (void)s; (void)sl; (void)b; }
void util_resource_copy_region(struct pipe_context *p, struct pipe_resource *d, unsigned dl, unsigned x, unsigned y, unsigned z,
			       struct pipe_resource *s, unsigned sl, const struct pipe_box *b)
{ (void)p; (void)d; (void)dl; (void)x; (void)y; (void)z; (void)s; (void)sl; (void)b; }

static void copy_data(struct nouveau_context *nv, struct nouveau_bo *dst, unsigned doff, unsigned ddom,
		      struct nouveau_bo *src, unsigned soff, unsigned sdom, unsigned size)
{ (void)nv; (void)dst; (void)doff; (void)ddom; (void)src; (void)soff; (void)sdom; (void)size; copies++; }
static void push_data(struct nouveau_context *nv, struct nouveau_bo *bo, unsigned off, unsigned dom, unsigned size, const void *data)
{ (void)nv; (void)bo; (void)off; (void)dom; (void)size; (void)data; copies++; }

static struct nv30_screen screen;
static struct nouveau_context ctx;

static struct pipe_resource *mk(unsigned bind, unsigned usage, unsigned size)
{
	struct pipe_resource t = { .target = PIPE_BUFFER, .format = PIPE_FORMAT_R8_UNORM, .width0 = size, .height0 = 1,
				   .depth0 = 1, .array_size = 1, .bind = bind, .usage = usage };
	return nv30_resource_create(&screen.base.base, &t);
}

static const char *dom(struct pipe_resource *r)
{
	unsigned d = nv04_resource(r)->domain;
	return d == NOUVEAU_BO_VRAM ? "VRAM" : d == NOUVEAU_BO_GART ? "GART" : "none";
}

static void setup(bool ps3, bool idx, bool fp)
{
	union nouveau_bo_config cfg = {};
	memset(&screen, 0, sizeof(screen));
	screen.base.device = &dev;
	screen.base.is_ps3 = ps3;
	screen.base.vram_domain = NOUVEAU_BO_VRAM;
	screen.idx_gart = ps3 && idx;
	screen.fp_gart = ps3 && fp;
	screen.base.vidmem_bindings = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER;
	screen.base.sysmem_bindings = PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER;
	screen.base.mm_VRAM = nouveau_mm_create(&dev, NOUVEAU_BO_VRAM, &cfg);
	screen.base.mm_GART = nouveau_mm_create(&dev, NOUVEAU_BO_GART, &cfg);
	memset(&ctx, 0, sizeof(ctx));
	ctx.screen = &screen.base;
	ctx.pipe.screen = &screen.base.base;
	ctx.copy_data = copy_data;
	ctx.push_data = push_data;
}

/* the placement of a list of buffers, for the golden diff */
static void dump_list(void)
{
	static const struct { const char *name; unsigned bind, usage; } l[] = {
		{ "index static", PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT },
		{ "index dynamic", PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DYNAMIC },
		{ "index stream", PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_STREAM },
		{ "vertex static", PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_DEFAULT },
		{ "vertex+index static", PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT },
		{ "constant static", PIPE_BIND_CONSTANT_BUFFER, PIPE_USAGE_DEFAULT },
		{ "no bind stream (a stream copy)", 0, PIPE_USAGE_STREAM },
	};
	for (unsigned i = 0; i < ARRAY_SIZE(l); i++) {
		struct pipe_resource *r = mk(l[i].bind, l[i].usage, 4096);
		printf("%s: %s\n", l[i].name, dom(r));
	}
	/* a fragment program's buffer, as nv30_fragprog_upload asks for it */
	printf("fragment program: %s\n", dom(mk(0, nv30_fp_usage(&screen), 64)));
}

int main(int argc, char **argv)
{
	struct pipe_resource *r;
	struct nv04_resource *b;

#ifdef NV30_HAVE_GART_DEFAULT
	if (argc > 1 && !strcmp(argv[1], "default")) {
		CHECK(nv30_ps3_fp_gart_default(true) && !nv30_ps3_fp_gart_default(false),
		      "NV30_PS3_FP_GART default: PS3 %d, other %d, want 1 and 0", nv30_ps3_fp_gart_default(true),
		      nv30_ps3_fp_gart_default(false));
#ifdef NV30_HAVE_IDX_MIGRATE
		CHECK(nv30_ps3_idx_gart_default(true) && !nv30_ps3_idx_gart_default(false),
		      "NV30_PS3_IDX_GART default: PS3 %d, other %d, want 1 and 0", nv30_ps3_idx_gart_default(true),
		      nv30_ps3_idx_gart_default(false));
#endif
		printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
		return fails != 0;
	}
#endif
#ifdef NV30_HAVE_IDX_MIGRATE
	if (argc > 1 && !strcmp(argv[1], "migrate")) {
		static const unsigned binds[] = { PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER, 0 };
		struct nv30_context nv;
		struct nouveau_pushbuf push = { 0 };
		struct nouveau_pushbuf_priv ppriv = { 0 };

		push.user_priv = &ppriv;	/* PUSH_KICK takes the screen's fence lock */

		for (unsigned i = 0; i < ARRAY_SIZE(binds); i++) {
			setup(true, true, false);
			memset(&nv, 0, sizeof(nv));
			nv.base = ctx;
			nv.base.pushbuf = &push;
			nv.screen = &screen;
			ppriv.screen = &screen.base;
			r = mk(binds[i], PIPE_USAGE_DEFAULT, 4096);
			b = nv04_resource(r);
			CHECK(b->domain == NOUVEAU_BO_VRAM, "migrate: bind %#x starts in %s", binds[i], dom(r));
			util_range_add(&b->base, &b->valid_buffer_range, 0, 4096);
			copies = kicks = 0;
			CHECK(nv30_idxbuf_to_gart(&nv, b), "migrate: bind %#x not moved", binds[i]);
			CHECK(b->domain == NOUVEAU_BO_GART && b->cpu_only, "migrate: bind %#x is %s, cpu_only %d", binds[i],
			      dom(r), b->cpu_only);
			CHECK(copies == 1, "migrate: bind %#x moved with %d GPU copies, want 1", binds[i], copies);
			CHECK(kicks == 1, "migrate: the copy not kicked on its own (%d kicks)", kicks);
			CHECK((nv.dirty & NV30_NEW_ARRAYS) && nv.base.vbo_dirty, "migrate: the arrays not validated again");
			copies = kicks = 0;
			CHECK(nv30_idxbuf_to_gart(&nv, b) && copies == 0 && kicks == 0,
			      "migrate: a GART buffer copied again (%d copies, %d kicks)", copies, kicks);
		}
#ifdef NV30_HAVE_IDX_MOVE2
		/* 0071: the state after a GPU move: the whole range valid, the copy's submission as the
		 * fences, GPU-writing; a CPU write right after it waits for the copy */
		{
			struct pipe_transfer *t = NULL;
			struct pipe_box box = { .x = 0, .width = 64, .height = 1, .depth = 1 };

			setup(true, true, false);
			memset(&nv, 0, sizeof(nv));
			nv.base = ctx;
			nv.base.pushbuf = &push;
			nv.base.fence = &the_fence;
			nv.screen = &screen;
			ppriv.screen = &screen.base;
			b = nv04_resource(mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_DEFAULT, 4096));
			util_range_add(&b->base, &b->valid_buffer_range, 0, 1024);	/* part written */
			CHECK(nv30_idxbuf_to_gart(&nv, b), "move: not moved");
			CHECK(b->valid_buffer_range.start == 0 && b->valid_buffer_range.end == 4096,
			      "move: valid range [%u, %u) after the GPU copy, want all of it",
			      b->valid_buffer_range.start, b->valid_buffer_range.end);
			CHECK(b->fence == &the_fence && b->fence_wr == &the_fence &&
			      (b->status & NOUVEAU_BUFFER_STATUS_GPU_WRITING),
			      "move: no write fence or GPU_WRITING after the GPU copy");
			gpu_busy = 1;
			fence_waits = 0;
			ctx.pipe.screen = &screen.base.base;
			CHECK(nouveau_buffer_transfer_map(&nv.base.pipe, &b->base, 0, PIPE_MAP_WRITE, &box, &t) &&
			      fence_waits == 1, "move: a write right after the GPU copy didn't wait (%d waits)", fence_waits);
			if (t)
				nouveau_buffer_transfer_unmap(&nv.base.pipe, t);
			gpu_busy = 0;
		}
		/* 0071: a current system memory copy: the CPU fills GART, no GPU copy, no kick, the copy freed */
		{
			uint8_t pat[4096];

			setup(true, true, false);
			memset(&nv, 0, sizeof(nv));
			nv.base = ctx;
			nv.base.pushbuf = &push;
			nv.screen = &screen;
			ppriv.screen = &screen.base;
			b = nv04_resource(mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_DEFAULT, 4096));
			for (unsigned k = 0; k < 4096; k++)
				pat[k] = (uint8_t)(k * 7 + 1);
			b->data = align_malloc(4096, 64);
			memcpy(b->data, pat, 4096);
			util_range_add(&b->base, &b->valid_buffer_range, 0, 4096);
			copies = kicks = 0;
			CHECK(nv30_idxbuf_to_gart(&nv, b) && b->domain == NOUVEAU_BO_GART && b->cpu_only,
			      "move by the CPU: not moved");
			CHECK(copies == 0 && kicks == 0, "move by the CPU: %d GPU copies, %d kicks, want none", copies, kicks);
			CHECK(!memcmp((uint8_t *)b->bo->map + b->offset, pat, 4096), "move by the CPU: GART isn't the copy");
			CHECK(!b->data && !b->fence_wr && !(b->status & NOUVEAU_BUFFER_STATUS_GPU_WRITING),
			      "move by the CPU: the system memory copy kept or the buffer still GPU-written");
			CHECK(b->valid_buffer_range.end == 4096, "move by the CPU: the valid range lost");
			CHECK(screen.idx_moves_cpu == 1, "move by the CPU: not counted");
			/* a stale copy (GPU_WRITING): the GPU copies, the stale copy is freed */
			b = nv04_resource(mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_DEFAULT, 4096));
			b->data = align_malloc(4096, 64);
			b->status |= NOUVEAU_BUFFER_STATUS_GPU_WRITING;
			copies = kicks = 0;
			CHECK(nv30_idxbuf_to_gart(&nv, b) && copies == 1 && kicks == 1 && !b->data,
			      "move with a stale copy: %d copies, %d kicks, copy %s", copies, kicks, b->data ? "kept" : "freed");
		}
		/* 0071: a GART buffer that isn't cpu_only becomes cpu_only; a GPU write into it queued in this
		 * submission is kicked first, an earlier one not */
		{
			setup(true, true, false);
			memset(&nv, 0, sizeof(nv));
			nv.base = ctx;
			nv.base.pushbuf = &push;
			nv.base.fence = &the_fence;
			nv.screen = &screen;
			ppriv.screen = &screen.base;
			b = nv04_resource(mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_STREAM, 4096));
			CHECK(b->domain == NOUVEAU_BO_GART && !b->cpu_only, "stream: not a plain GART buffer");
			kicks = 0;
			CHECK(nv30_idxbuf_to_gart(&nv, b) && b->cpu_only && kicks == 0,
			      "stream: not cpu_only or kicked with nothing queued (%d kicks)", kicks);
			b->fence_wr = &the_fence;	/* a staging copy or CopyBufferSubData into it, queued */
			CHECK(nv30_idxbuf_to_gart(&nv, b) && kicks == 1, "stream: a queued GPU write not kicked (%d kicks)", kicks);
			{
				static struct nouveau_fence older;

				b->fence_wr = &older;	/* submitted already: the kernel waits for it */
				CHECK(nv30_idxbuf_to_gart(&nv, b) && kicks == 1, "stream: an earlier GPU write kicked again");
			}
		}
		/* 0071: GART full: one wait for the GPU, then again; still full: not moved (the draw dropped) */
		{
			setup(true, true, false);
			memset(&nv, 0, sizeof(nv));
			nv.base = ctx;
			nv.base.pushbuf = &push;
			nv.screen = &screen;
			ppriv.screen = &screen.base;
			b = nv04_resource(mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_DEFAULT, 8u << 20));	/* its own BO */
			gart_fail = 1;
			idle_waits = 0;
			CHECK(nv30_idxbuf_to_gart(&nv, b) && idle_waits == 1 && b->domain == NOUVEAU_BO_GART,
			      "GART full once: not moved after a wait (%d waits)", idle_waits);
			b = nv04_resource(mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_DEFAULT, 8u << 20));
			gart_fail = 2;
			idle_waits = 0;
			CHECK(!nv30_idxbuf_to_gart(&nv, b) && idle_waits == 1 && b->domain == NOUVEAU_BO_VRAM,
			      "GART full: moved anyway or waited %d times", idle_waits);
			gart_fail = 0;
		}
		/* 0071: the copy's submission cancelled: the buffer keeps its VRAM storage */
		{
			struct nouveau_bo *old;

			setup(true, true, false);
			memset(&nv, 0, sizeof(nv));
			nv.base = ctx;
			nv.base.pushbuf = &push;
			nv.screen = &screen;
			ppriv.screen = &screen.base;
			b = nv04_resource(mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_DEFAULT, 4096));
			old = b->bo;
			cancel_kick = 1;
			CHECK(!nv30_idxbuf_to_gart(&nv, b) && b->domain == NOUVEAU_BO_VRAM && b->bo == old && !b->cpu_only,
			      "a lost copy: the buffer moved to GART (%s)", dom(&b->base));
			cancel_kick = 0;
			CHECK(nv30_idxbuf_to_gart(&nv, b) && b->domain == NOUVEAU_BO_GART, "a lost copy: not moved the next time");
		}
		/* 0071: a shared buffer stays (another process may hold its VRAM address) */
		setup(true, true, false);
		memset(&nv, 0, sizeof(nv));
		nv.base = ctx;
		nv.base.pushbuf = &push;
		nv.screen = &screen;
		ppriv.screen = &screen.base;
		b = nv04_resource(mk(PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_SHARED, PIPE_USAGE_DEFAULT, 4096));
		CHECK(!nv30_idxbuf_to_gart(&nv, b) && b->domain == NOUVEAU_BO_VRAM, "a shared buffer moved");
#endif
		/* an index buffer made in GART (0059): nothing to do */
		setup(true, true, false);
		memset(&nv, 0, sizeof(nv));
		nv.base = ctx;
		nv.base.pushbuf = &push;
		nv.screen = &screen;
		ppriv.screen = &screen.base;
		b = nv04_resource(mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, 4096));
		copies = kicks = 0;
		CHECK(nv30_idxbuf_to_gart(&nv, b) && copies == 0 && kicks == 0 && b->domain == NOUVEAU_BO_GART,
		      "migrate: an index buffer in GART touched");
		printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
		return fails != 0;
	}
#endif
	if (argc > 1 && !strcmp(argv[1], "dump")) {
		setup(true, argc > 2 && strchr(argv[2], 'i'), argc > 2 && strchr(argv[2], 'f'));
		dump_list();
		return 0;
	}

	/* off: as before */
	setup(true, false, false);
	r = mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, 4096);
	CHECK(nv04_resource(r)->domain == NOUVEAU_BO_VRAM && !nv04_resource(r)->cpu_only, "default: a static index buffer is %s", dom(r));
	r = mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_STREAM, 4096);
	CHECK(nv04_resource(r)->domain == NOUVEAU_BO_GART && !nv04_resource(r)->cpu_only, "default: a stream index buffer is %s", dom(r));

	/* NV30_PS3_FP_GART */
	setup(true, false, false);
	CHECK(nv30_fp_usage(&screen) == PIPE_USAGE_DEFAULT && nv04_resource(mk(0, nv30_fp_usage(&screen), 64))->domain == NOUVEAU_BO_VRAM,
	      "default: a fragment program is not in VRAM");
	setup(true, false, true);
	CHECK(nv04_resource(mk(0, nv30_fp_usage(&screen), 64))->domain == NOUVEAU_BO_GART, "FP_GART: a fragment program is not in GART");
	setup(false, false, true);
	CHECK(nv04_resource(mk(0, nv30_fp_usage(&screen), 64))->domain == NOUVEAU_BO_VRAM, "not the PS3: FP_GART moved a program");

	/* NV30_PS3_IDX_GART */
	setup(true, true, false);
	r = mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, 4096);
	CHECK(nv04_resource(r)->domain == NOUVEAU_BO_GART && nv04_resource(r)->cpu_only, "IDX_GART: an index buffer is %s, cpu_only %d", dom(r), nv04_resource(r)->cpu_only);
	r = mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_DEFAULT, 4096);
	CHECK(nv04_resource(r)->domain == NOUVEAU_BO_VRAM && !nv04_resource(r)->cpu_only, "IDX_GART: a vertex buffer is %s", dom(r));
	r = mk(PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, 4096);
	CHECK(nv04_resource(r)->domain == NOUVEAU_BO_VRAM && !nv04_resource(r)->cpu_only, "IDX_GART: a buffer that is also a vertex buffer is %s", dom(r));
	setup(false, true, false);	/* not the PS3: the switch does nothing */
	r = mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, 4096);
	CHECK(nv04_resource(r)->domain == NOUVEAU_BO_VRAM, "not the PS3: an index buffer is %s", dom(r));

	/* a busy buffer: waited for when cpu_only, staged and GPU-copied otherwise */
	for (int cpu_only = 1; cpu_only >= 0; cpu_only--) {
		struct pipe_transfer *t = NULL;
		struct pipe_box box = { .x = 0, .width = 1024, .height = 1, .depth = 1 };
		void *p;

		setup(true, true, false);
		r = mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, 4096);
		b = nv04_resource(r);
		b->cpu_only = cpu_only;
		util_range_add(&b->base, &b->valid_buffer_range, 0, 4096);	/* written before */
		b->fence = &the_fence;
		gpu_busy = 1; fence_waits = 0; copies = 0;
		p = nouveau_buffer_transfer_map(&ctx.pipe, r, 0, PIPE_MAP_WRITE | PIPE_MAP_DISCARD_RANGE, &box, &t);
		CHECK(p != NULL, "cpu_only %d: no map", cpu_only);
		if (cpu_only) {
			CHECK(p == (uint8_t *)b->bo->map + b->offset, "cpu_only: the map is not the buffer's own");
			CHECK(fence_waits == 1, "cpu_only: %d waits for the GPU, want 1", fence_waits);
		} else {
			CHECK(p != (uint8_t *)b->bo->map + b->offset && fence_waits == 0, "control: a busy GART buffer is staged (%d waits)", fence_waits);
		}
		if (t)
			nouveau_buffer_transfer_unmap(&ctx.pipe, t);
		CHECK(cpu_only ? copies == 0 : copies >= 1, "cpu_only %d: %d GPU copies after the unmap", cpu_only, copies);
	}
	/* DONTBLOCK */
	{
		struct pipe_transfer *t = NULL;
		struct pipe_box box = { .x = 0, .width = 64, .height = 1, .depth = 1 };

		setup(true, true, false);
		r = mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, 4096);
		b = nv04_resource(r);
		util_range_add(&b->base, &b->valid_buffer_range, 0, 4096);
		b->fence = &the_fence;
		gpu_busy = 1; fence_waits = 0;
		CHECK(nouveau_buffer_transfer_map(&ctx.pipe, r, 0, PIPE_MAP_WRITE | PIPE_MAP_DONTBLOCK, &box, &t) == NULL && fence_waits == 0,
		      "DONTBLOCK on a busy cpu_only buffer waited or mapped");
	}
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
