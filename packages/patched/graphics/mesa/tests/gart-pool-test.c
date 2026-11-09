/* gart-pool-test PART: Mesa 0076, the PS3's two GART pools and their slabs (nouveau_mm.c, nouveau_mm_ps3.h,
 * nouveau_buffer.c, nv30_resource.c and u_transfer.c compiled in whole against a fake device)
 *   choice  the pool is chosen once per allocation: stream, staging, persistent, coherent and user-memory
 *           buffers, transfer staging and readback areas, and nv30's move of a stream index buffer go to
 *           mm_GART_stream, everything else to mm_GART. At every size class a stream buffer and an index
 *           buffer never share a slab object, and a persistent or coherent buffer never lands in mm_GART;
 *           NV30_PS3_GART_POOL=0 and other GPUs: one pool, as before
 *   keep    NV30_PS3_MM_KEEP_SLAB (default -1, every slab kept as before): a random load on pools keeping 1, 0, 2 and
 *           every (-1) empty slab, checked after every step: no slab with a live allocation is ever
 *           freed, every empty slab is on its bucket's free list and counted, at most N per bucket, a new
 *           slab only when the bucket has neither a part-used nor an empty one; at the end as many empty
 *           ones per bucket as it had, up to N; the kept slab taken into use stays while another one of
 *           its bucket empties; a slab whose last chunk goes while a pending submission still references its
 *           object stays open until that reference goes; the option's default and values, and the screen's
 *           end frees the kept slabs
 *   stats   NV30_PS3_GART_STATS: the bytes of slab objects held, their peak, the slabs made and freed and
 *           the empty ones kept, per pool, against a model, and the stderr line the screen's end prints
 * (host-tests.sh gart-pool) */
#include <string.h>
#include "nouveau_buffer.c"
#include "nouveau_mm.c"
#include "nouveau_mm_ps3.h"
#include "nv30/nv30_resource.c"
#include "util/u_transfer.c"
#include <stdlib.h>

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

/* --- the fake device -------------------------------------------------------------------------- */
static int gpu_busy, copies;
static struct nouveau_device dev;
int nouveau_pushbuf_kick(struct nouveau_pushbuf *push) { (void)push; return 0; }
int nouveau_pushbuf_space(struct nouveau_pushbuf *push, uint32_t dwords, uint32_t relocs, uint32_t pushes)
{ (void)push; (void)dwords; (void)relocs; (void)pushes; return 0; }
int64_t os_time_get_nano(void) { return 0; }
static struct nouveau_fence the_fence;

/* objects made and alive */
static int bo_made, bo_alive;
int nouveau_bo_new(struct nouveau_device *d, uint32_t flags, uint32_t align, uint64_t size,
		   union nouveau_bo_config *config, struct nouveau_bo **pbo)
{
	(void)align; (void)config;
	struct nouveau_bo *bo = calloc(1, sizeof(*bo));
	bo->device = d;
	bo->size = size;
	bo->flags = flags;
	bo->handle = 1;	/* (the reference count here) */
	bo->map = calloc(1, size);
	bo_made++;
	bo_alive++;
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
		bo_alive--;
	}
	*ref = bo;
}
int nouveau_bo_map(struct nouveau_bo *bo, uint32_t access, struct nouveau_client *client) { (void)bo; (void)access; (void)client; return 0; }
int nouveau_bo_wait(struct nouveau_bo *bo, uint32_t access, struct nouveau_client *client) { (void)bo; (void)access; (void)client; return 0; }
void nouveau_fence_ref(struct nouveau_fence *f, struct nouveau_fence **r, struct nouveau_screen *s) { (void)s; *r = f; }
bool nouveau_fence_signalled(struct nouveau_fence *f) { (void)f; return !gpu_busy; }
bool nouveau_fence_wait(struct nouveau_fence *f, struct util_debug_callback *d) { (void)f; (void)d; gpu_busy = 0; return true; }
bool nouveau_fence_wait_idle(struct nouveau_context *nv) { (void)nv; gpu_busy = 0; return true; }
bool nouveau_fence_work(struct nouveau_fence *f, void (*func)(void *), void *data) { (void)f; func(data); return true; }
void nouveau_fence_unref_bo(void *data) { struct nouveau_bo *bo = data; nouveau_bo_ref(NULL, &bo); }
int nouveau_mesa_debug;
void simple_mtx_init(simple_mtx_t *m, int type) { (void)type; memset(m, 0, sizeof(*m)); }
void simple_mtx_destroy(simple_mtx_t *m) { (void)m; }
int mtx_lock(mtx_t *m) { (void)m; return 0; }
int mtx_trylock(mtx_t *m) { (void)m; return thrd_busy; }
int mtx_unlock(mtx_t *m) { (void)m; return 0; }
void _simple_mtx_plain_init_once(simple_mtx_t *m) { (void)m; }
void util_call_once_data_slow(once_flag *once, util_call_once_data_func func, const void *val) { (void)once; func(val); }
const struct util_format_description *util_format_description(enum pipe_format f) { (void)f; return NULL; }
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

/* the environment, as util/u_debug.c reads it */
bool debug_get_bool_option(const char *name, bool dfault)
{
	const char *s = getenv(name);
	if (!s)
		return dfault;
	return !(!strcmp(s, "0") || !strcasecmp(s, "n") || !strcasecmp(s, "no") || !strcasecmp(s, "f") ||
		 !strcasecmp(s, "false"));
}
int64_t debug_get_num_option(const char *name, int64_t dfault)
{
	const char *s = getenv(name);
	char *end;
	if (!s)
		return dfault;
	long long v = strtoll(s, &end, 0);
	return end != s ? v : dfault;
}

static void copy_data(struct nouveau_context *nv, struct nouveau_bo *dst, unsigned doff, unsigned ddom,
		      struct nouveau_bo *src, unsigned soff, unsigned sdom, unsigned size)
{ (void)nv; (void)dst; (void)doff; (void)ddom; (void)src; (void)soff; (void)sdom; (void)size; copies++; }
static void push_data(struct nouveau_context *nv, struct nouveau_bo *bo, unsigned off, unsigned dom, unsigned size, const void *data)
{ (void)nv; (void)bo; (void)off; (void)dom; (void)size; (void)data; copies++; }

static struct nv30_screen screen;
static struct nv30_context nv;
static struct nouveau_pushbuf push;
static struct nouveau_pushbuf_priv ppriv;
#define ctx (nv.base)
#define GART (screen.base.mm_GART)
#define STREAM (screen.base.mm_GART_stream)

/* a PS3 screen's pools as nouveau_screen_init makes them, with the environment as it is now */
static void setup(bool ps3)
{
	union nouveau_bo_config cfg = {};
	memset(&dev, 0, sizeof(dev));
	dev.ps3 = ps3;
	memset(&screen, 0, sizeof(screen));
	screen.base.device = &dev;
	screen.base.is_ps3 = ps3;
	screen.base.vram_domain = NOUVEAU_BO_VRAM;
	screen.idx_gart = ps3;
	screen.base.vidmem_bindings = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER;
	screen.base.sysmem_bindings = PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER;
	screen.base.mm_GART = nouveau_mm_create(&dev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP, &cfg);
	screen.base.mm_VRAM = nouveau_mm_create(&dev, NOUVEAU_BO_VRAM, &cfg);
	if (ps3)
		nouveau_mm_ps3_init(&screen.base, &cfg);
	memset(&nv, 0, sizeof(nv));
	nv.screen = &screen;
	nv30_resource_screen_init(&screen.base.base);	/* (resource_create, resource_destroy) */
	ctx.screen = &screen.base;
	ctx.pipe.screen = &screen.base.base;
	ctx.copy_data = copy_data;
	ctx.push_data = push_data;
	ctx.pushbuf = &push;
	ctx.fence = &the_fence;
	push.user_priv = &ppriv;
	ppriv.screen = &screen.base;
	ctx.pipe.buffer_map = nouveau_buffer_transfer_map;
	ctx.pipe.buffer_unmap = nouveau_buffer_transfer_unmap;
	ctx.pipe.buffer_subdata = u_default_buffer_subdata;
	gpu_busy = 0;
}
/* the screen's end as nouveau_screen_fini does it */
static void teardown(void)
{
	nouveau_mm_ps3_fini(&screen.base);
	nouveau_mm_destroy(screen.base.mm_GART);
	nouveau_mm_destroy(screen.base.mm_VRAM);
	screen.base.mm_GART = screen.base.mm_VRAM = NULL;
}

static struct pipe_resource *mk_flags(unsigned bind, unsigned usage, unsigned size, unsigned flags)
{
	struct pipe_resource t = { .target = PIPE_BUFFER, .format = PIPE_FORMAT_R8_UNORM, .width0 = size, .height0 = 1,
				   .depth0 = 1, .array_size = 1, .bind = bind, .usage = usage, .flags = flags };
	return nv30_resource_create(&screen.base.base, &t);
}
static struct nv04_resource *mk(unsigned bind, unsigned usage, unsigned size, unsigned flags)
{
	return nv04_resource(mk_flags(bind, usage, size, flags));
}
static void unref(struct nv04_resource *r) { struct pipe_resource *p = &r->base; pipe_resource_reference(&p, NULL); }

/* the pool an allocation came from (NULL: an object of its own, past the slab sizes) */
static struct nouveau_mman *pool_of(struct nouveau_mm_allocation *mm)
{
	return mm ? ((struct mm_slab *)mm->priv)->cache : NULL;
}
static const char *pool_name(struct nouveau_mman *c)
{
	return !c ? "no pool" : c == GART ? "mm_GART" : c == STREAM ? "mm_GART_stream" : c == screen.base.mm_VRAM ? "mm_VRAM" : "?";
}

/* --- choice ----------------------------------------------------------------------------------- */
static void choice_sizes(void)
{
	/* every slab size class of a buffer (the 256-byte alignment up to MM_MAX_SIZE) */
	for (unsigned size = 256; size <= MM_MAX_SIZE; size *= 2) {
		struct nv04_resource *i0 = mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, size, 0);
		struct nv04_resource *k[7] = {
			/* u_upload_create_default's buffers: stream, and persistent and coherent on nv30 */
			mk(PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER | PIPE_BIND_CONSTANT_BUFFER, PIPE_USAGE_STREAM, size,
			   PIPE_RESOURCE_FLAG_MAP_PERSISTENT | PIPE_RESOURCE_FLAG_MAP_COHERENT | PIPE_RESOURCE_FLAG_SINGLE_THREAD_USE),
			mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_STREAM, size, 0),
			mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_STREAM, size, 0),
			mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, size, PIPE_RESOURCE_FLAG_MAP_PERSISTENT),
			mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, size, PIPE_RESOURCE_FLAG_MAP_COHERENT),
			mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_DEFAULT, size, PIPE_RESOURCE_FLAG_MAP_PERSISTENT),
			mk(0, PIPE_USAGE_STAGING, size, 0),
		};
		static const char *what[7] = { "an upload buffer (stream, persistent, coherent)", "a stream index buffer",
			"a stream vertex buffer", "a persistent index buffer", "a coherent index buffer",
			"a persistent vertex buffer", "a staging buffer" };
		struct nv04_resource *i1 = mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, size, 0);
		CHECK(i0->domain == NOUVEAU_BO_GART && pool_of(i0->mm) == GART && pool_of(i1->mm) == GART,
		      "choice %u: an index buffer in %s and %s, want mm_GART", size, pool_name(pool_of(i0->mm)), pool_name(pool_of(i1->mm)));
		for (unsigned j = 0; j < 7; j++) {
			struct nouveau_mman *p = pool_of(k[j]->mm);
			CHECK(k[j]->domain == NOUVEAU_BO_GART && p == STREAM, "choice %u: %s in %s, want mm_GART_stream", size,
			      what[j], pool_name(p));
			if (k[j]->base.flags & (PIPE_RESOURCE_FLAG_MAP_PERSISTENT | PIPE_RESOURCE_FLAG_MAP_COHERENT))
				CHECK(p != GART, "choice %u: a persistent or coherent buffer (%s) in mm_GART", size, what[j]);
			if (k[j]->base.usage == PIPE_USAGE_STREAM)
				CHECK(k[j]->bo != i0->bo && k[j]->bo != i1->bo,
				      "choice %u: a stream buffer (%s) shares a slab object with an index buffer", size, what[j]);
		}
		for (unsigned j = 0; j < 7; j++)
			unref(k[j]);
		unref(i0);
		unref(i1);
	}
}

static void choice_paths(void)
{
	/* a user buffer (a client array) uploaded at each draw: new storage every time */
	{
		static uint8_t user[4096];
		struct nv04_resource *u = nv04_resource(nouveau_user_buffer_create(&screen.base.base, user, 4096,
										   PIPE_BIND_VERTEX_BUFFER));
		struct nv04_resource *i = mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, 4096, 0);
		for (int n = 0; n < 3; n++) {
			CHECK(nouveau_user_buffer_upload(&ctx, u, 0, 4096) && u->domain == NOUVEAU_BO_GART, "user upload: not in GART");
			CHECK(pool_of(u->mm) == STREAM && u->bo != i->bo, "user upload: a user buffer's storage in %s, want mm_GART_stream",
			      pool_name(pool_of(u->mm)));
		}
		unref(u);
		unref(i);
	}
	/* migrate: the pool follows the buffer's usage */
	{
		struct nv04_resource *v = mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_STREAM, 4096, 0);
		CHECK(v->domain == NOUVEAU_BO_GART && pool_of(v->mm) == STREAM, "migrate: a stream vertex buffer in %s",
		      pool_name(pool_of(v->mm)));
		struct nv04_resource *d = mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_DEFAULT, 4096, 0);
		CHECK(d->domain == NOUVEAU_BO_VRAM, "migrate: a default vertex buffer not in VRAM");
		CHECK(nouveau_buffer_migrate(&ctx, d, NOUVEAU_BO_GART) && pool_of(d->mm) == GART,
		      "migrate: a default buffer moved to GART landed in %s, want mm_GART", pool_name(pool_of(d->mm)));
		unref(v);
		unref(d);
	}
	/* transfer staging and readback areas (nouveau_transfer_staging) */
	{
		struct nv04_resource *v = mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_DEFAULT, 8192, 0);
		struct pipe_box b = { .x = 0, .width = 4096, .height = 1, .depth = 1 };
		struct pipe_transfer *t = NULL;
		util_range_add(&v->base, &v->valid_buffer_range, 0, 8192);
		void *m = nouveau_buffer_transfer_map(&ctx.pipe, &v->base, 0, PIPE_MAP_WRITE | PIPE_MAP_DISCARD_RANGE, &b, &t);
		CHECK(m && t && nouveau_transfer(t)->bo, "staging: no staging area for a VRAM write");
		if (t) {
			CHECK(pool_of(nouveau_transfer(t)->mm) == STREAM, "staging: a write's staging area in %s, want mm_GART_stream",
			      pool_name(pool_of(nouveau_transfer(t)->mm)));
			nouveau_buffer_transfer_unmap(&ctx.pipe, t);
		}
		v->status |= NOUVEAU_BUFFER_STATUS_GPU_WRITING;
		t = NULL;
		m = nouveau_buffer_transfer_map(&ctx.pipe, &v->base, 0, PIPE_MAP_READ, &b, &t);
		CHECK(m && t && nouveau_transfer(t)->bo, "readback: no staging area for a read of a GPU-written buffer");
		if (t) {
			CHECK(pool_of(nouveau_transfer(t)->mm) == STREAM, "readback: a read's staging area in %s, want mm_GART_stream",
			      pool_name(pool_of(nouveau_transfer(t)->mm)));
			nouveau_buffer_transfer_unmap(&ctx.pipe, t);
		}
		unref(v);
	}
	/* nv30's move of an index buffer to GART (0070, 0071): the pool by the buffer's usage */
	{
		struct nv04_resource *v = mk(PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, 4096, 0);
		util_range_add(&v->base, &v->valid_buffer_range, 0, 4096);
		CHECK(nv30_idxbuf_to_gart(&nv, v) && v->domain == NOUVEAU_BO_GART && pool_of(v->mm) == GART,
		      "nv30 move: a default index buffer moved to %s, want mm_GART", pool_name(pool_of(v->mm)));
		struct nv04_resource *s = mk(PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DYNAMIC, 4096, 0);
		s->base.usage = PIPE_USAGE_STREAM;	/* (a stream one made in VRAM: placed there by another rule) */
		util_range_add(&s->base, &s->valid_buffer_range, 0, 4096);
		CHECK(s->domain == NOUVEAU_BO_VRAM && nv30_idxbuf_to_gart(&nv, s) && s->domain == NOUVEAU_BO_GART &&
		      pool_of(s->mm) == STREAM, "nv30 move: a stream index buffer moved to %s, want mm_GART_stream",
		      pool_name(pool_of(s->mm)));
		gpu_busy = 0;
		unref(v);
		unref(s);
	}
}

static void part_choice(void)
{
	unsetenv("NV30_PS3_GART_POOL");
	unsetenv("NV30_PS3_MM_KEEP_SLAB");
	setup(true);
	CHECK(STREAM && STREAM != GART, "choice: no mm_GART_stream on the PS3 by default");
	if (STREAM) {
		choice_sizes();
		choice_paths();
	}
	teardown();
	CHECK(bo_alive == 0, "choice: %d objects left after the screen's end", bo_alive);

	/* one pool, as before: NV30_PS3_GART_POOL=0, and other GPUs */
	for (int k = 0; k < 2; k++) {
		if (k == 0)
			setenv("NV30_PS3_GART_POOL", "0", 1);
		setup(k == 0);
		CHECK(!STREAM, "choice: a stream pool %s", k == 0 ? "with NV30_PS3_GART_POOL=0" : "off the PS3");
		struct nv04_resource *s = mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_STREAM, 4096, 0);
		struct nv04_resource *p = mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, 4096, PIPE_RESOURCE_FLAG_MAP_PERSISTENT);
		CHECK(pool_of(s->mm) == GART && pool_of(p->mm) == GART && s->bo == p->bo,
		      "choice: one pool %s: the buffers in %s and %s", k == 0 ? "(pool off)" : "(not a PS3)",
		      pool_name(pool_of(s->mm)), pool_name(pool_of(p->mm)));
		unref(s);
		unref(p);
		teardown();
		unsetenv("NV30_PS3_GART_POOL");
	}
	CHECK(bo_alive == 0, "choice: %d objects left", bo_alive);
}

/* --- keep ------------------------------------------------------------------------------------- */
#define NA 160
static struct { struct nouveau_mm_allocation *mm; struct nouveau_bo *bo; } al[NA];

static int list_len(struct list_head *h) { int n = 0; list_for_each_entry(struct mm_slab, s, h, head) n++; return n; }
static bool on_list(struct list_head *h, struct mm_slab *x)
{
	list_for_each_entry(struct mm_slab, s, h, head)
		if (s == x)
			return true;
	return false;
}
/* the pool's state: each slab on the list its fill says, free's length counted, at most keep empty
 * ones, the bytes held the slabs' sum; and live_check */
static bool live_check(const char *what, struct nouveau_mman *c);
static bool keep_check(const char *what, struct nouveau_mman *c, int keep)
{
	int bad = fails;
	uint64_t bytes = 0;
	for (int b = 0; b < MM_NUM_BUCKETS; b++) {
		struct mm_bucket *bu = &c->bucket[b];
		list_for_each_entry(struct mm_slab, s, &bu->free, head) {
			CHECK(s->free == s->count, "%s: a slab with %d live chunks on the free list", what, s->count - s->free);
			bytes += (uint64_t)s->count << s->order;
		}
		list_for_each_entry(struct mm_slab, s, &bu->used, head) {
			CHECK(s->free > 0 && s->free < s->count, "%s: a slab with %d of %d free on the used list", what, s->free, s->count);
			bytes += (uint64_t)s->count << s->order;
		}
		list_for_each_entry(struct mm_slab, s, &bu->full, head) {
			CHECK(s->free == 0, "%s: a slab with %d free on the full list", what, s->free);
			bytes += (uint64_t)s->count << s->order;
		}
		int nf = list_len(&bu->free);
		CHECK(nf == bu->num_free, "%s: bucket %d has %d empty slabs, counted %d", what, b, nf, bu->num_free);
		if (keep >= 0)
			CHECK(nf <= keep, "%s: bucket %d keeps %d empty slabs, want at most %d", what, b, nf, keep);
	}
	CHECK(bytes == c->allocated, "%s: the slabs hold %" PRIu64 " bytes, counted %" PRIu64, what, bytes, c->allocated);
	return live_check(what, c) && fails == bad;
}
/* every live allocation's slab alive (compared as a pointer: a freed one is never read), its chunk taken */
static bool live_check(const char *what, struct nouveau_mman *c)
{
	int bad = fails;
	for (int i = 0; i < NA; i++) {
		if (!al[i].mm)
			continue;
		struct mm_slab *s = al[i].mm->priv;
		struct mm_bucket *bu = &c->bucket[0];
		bool alive = false;
		for (int b = 0; b < MM_NUM_BUCKETS && !alive; b++) {
			bu = &c->bucket[b];
			alive = on_list(&bu->used, s) || on_list(&bu->full, s) || on_list(&bu->free, s);
		}
		CHECK(alive, "%s: a slab with a live allocation was freed while in use", what);
		if (!alive)
			break;
		int n = al[i].mm->offset >> s->order;
		CHECK(s->bo == al[i].bo && !(s->bits[n / 32] & (1u << (n % 32))),
		      "%s: a live allocation's chunk is free in its slab", what);
	}
	return fails == bad;
}

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* a random load on a pool keeping `keep` empty slabs per bucket */
static void keep_load(int keep)
{
	char what[64];
	union nouveau_bo_config cfg = {};
	memset(&dev, 0, sizeof(dev));
	struct nouveau_mman *c = nouveau_mm_create(&dev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP, &cfg);
	nouveau_mm_set_keep(c, keep);
	snprintf(what, sizeof(what), "keep %d", keep);
	memset(al, 0, sizeof(al));
	bool made[MM_NUM_BUCKETS] = {};
	int made_slabs = 0, bo0 = bo_made;
	for (int step = 0; step < 30000; step++) {
		int i = rnd() % NA;
		if (al[i].mm) {
			nouveau_bo_ref(NULL, &al[i].bo);	/* (the buffer's reference goes, as with a fence) */
			nouveau_mm_free(al[i].mm);
			al[i].mm = NULL;
		} else {
			/* mostly the small classes, sometimes one of the big ones */
			int order = (rnd() % 8) ? MM_MIN_ORDER + rnd() % 8 : MM_MIN_ORDER + rnd() % MM_NUM_BUCKETS;
			uint32_t size = (1u << order) - (rnd() % 2 ? (rnd() % (1u << (order - 1))) : 0);
			struct mm_bucket *bu = mm_bucket_by_size(c, size);
			bool had = !list_is_empty(&bu->used) || !list_is_empty(&bu->free);
			int before = bo_made;
			uint32_t off;
			al[i].mm = nouveau_mm_allocate(c, size, &al[i].bo, &off);
			CHECK(al[i].mm && al[i].bo, "%s: no allocation", what);
			if (!al[i].mm)
				break;
			if (bo_made != before) {
				CHECK(!had, "%s: a new slab made while the bucket had a part-used or an empty one", what);
				made_slabs++;
			}
			made[bu - c->bucket] = true;
		}
		if (!keep_check(what, c, keep))
			break;
	}
	/* everything freed: as many empty slabs kept in each bucket as it had, up to keep (all for -1) */
	int had[MM_NUM_BUCKETS];
	for (int b = 0; b < MM_NUM_BUCKETS; b++)
		had[b] = list_len(&c->bucket[b].free) + list_len(&c->bucket[b].used) + list_len(&c->bucket[b].full);
	for (int i = 0; i < NA; i++)
		if (al[i].mm) {
			nouveau_bo_ref(NULL, &al[i].bo);
			nouveau_mm_free(al[i].mm);
			al[i].mm = NULL;
		}
	keep_check(what, c, keep);
	int kept = 0;
	for (int b = 0; b < MM_NUM_BUCKETS; b++) {
		int nf = list_len(&c->bucket[b].free);
		kept += nf;
		int want = keep < 0 ? had[b] : MIN2(keep, had[b]);
		CHECK(nf == want && (!made[b] || nf > 0 || keep == 0), "%s: bucket %d keeps %d empty slabs once all is freed, "
		      "want %d (it had %d)", what, b, nf, want, had[b]);
	}
	CHECK(made_slabs == bo_made - bo0, "%s: %d objects made, %d of them slabs", what, bo_made - bo0, made_slabs);
	CHECK(bo_alive == kept, "%s: %d objects alive with %d slabs kept", what, bo_alive, kept);
	if (keep < 0)
		CHECK(kept == made_slabs, "%s: %d of %d slabs kept, want all (as before)", what, kept, made_slabs);
	struct nouveau_mm_stats st;
	nouveau_mm_get_stats(c, &st);
	CHECK(st.slabs_made == (unsigned)made_slabs && st.slabs_freed == (unsigned)(made_slabs - kept) &&
	      st.kept == (unsigned)kept, "%s: stats say %u made, %u freed, %u kept; want %d, %d, %d", what, st.slabs_made,
	      st.slabs_freed, st.kept, made_slabs, made_slabs - kept, kept);
	printf("%s: %d slabs made, %d kept at the end\n", what, made_slabs, kept);
	nouveau_mm_destroy(c);
	CHECK(bo_alive == 0, "%s: %d objects left after the pool's end", what, bo_alive);
}

/* the option on a screen: the keep of both pools, and what it does to the next allocation */
static void keep_option(const char *env, int want)
{
	if (env)
		setenv("NV30_PS3_MM_KEEP_SLAB", env, 1);
	else
		unsetenv("NV30_PS3_MM_KEEP_SLAB");
	setup(true);
	const char *e = env ? env : "unset";
	CHECK(GART->keep == want && STREAM && STREAM->keep == want, "keep: NV30_PS3_MM_KEEP_SLAB %s: mm_GART keeps %d, "
	      "mm_GART_stream %d, want %d", e, GART->keep, STREAM ? STREAM->keep : -9, want);
	CHECK(screen.base.mm_VRAM->keep == -1, "keep: mm_VRAM keeps %d, want every slab (-1, as before)", screen.base.mm_VRAM->keep);
	/* two 4 KiB slabs of 256-byte chunks in each pool, then all freed: the next allocation */
	for (int p = 0; p < 2; p++) {
		struct nouveau_mman *c = p ? STREAM : GART;
		if (!c)
			continue;
		struct nouveau_mm_allocation *mm[17];
		struct nouveau_bo *bo[17] = {};
		uint32_t off;
		for (int i = 0; i < 17; i++)
			mm[i] = nouveau_mm_allocate(c, 256, &bo[i], &off);
		for (int i = 0; i < 17; i++) {
			nouveau_bo_ref(NULL, &bo[i]);
			nouveau_mm_free(mm[i]);
		}
		int nf = list_len(&mm_bucket_by_size(c, 256)->free);
		int k = want < 0 ? 2 : MIN2(want, 2);
		CHECK(nf == k, "keep: NV30_PS3_MM_KEEP_SLAB %s: %s keeps %d of its 2 empty slabs, want %d", e, pool_name(c), nf, k);
		int before = bo_made;
		mm[0] = nouveau_mm_allocate(c, 256, &bo[0], &off);
		CHECK((bo_made == before) == (k > 0), "keep: NV30_PS3_MM_KEEP_SLAB %s: %s's next allocation %s a new object",
		      e, pool_name(c), bo_made == before ? "made no" : "made");
		nouveau_bo_ref(NULL, &bo[0]);
		nouveau_mm_free(mm[0]);
	}
	/* the screen's end frees the kept slabs */
	teardown();
	CHECK(bo_alive == 0, "keep: NV30_PS3_MM_KEEP_SLAB %s: %d objects left after the screen's end", e, bo_alive);
	unsetenv("NV30_PS3_MM_KEEP_SLAB");
}

/* the kept slab taken into use, then another slab of its bucket emptying: the kept one is in use and
 * must stay (the one that emptied is kept now) */
static bool keep_in_use(void)
{
	union nouveau_bo_config cfg = {};
	memset(&dev, 0, sizeof(dev));
	struct nouveau_mman *c = nouveau_mm_create(&dev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP, &cfg);
	nouveau_mm_set_keep(c, 1);
	memset(al, 0, sizeof(al));
	uint32_t off;
	/* (checked after every step; on a failure the pool is left as it is: nothing freed is touched) */
#define IN_USE_CHECK() do { if (!live_check("in use", c)) return false; } while (0)
#define IN_USE_ALLOC(i) do { al[i].mm = nouveau_mm_allocate(c, 256, &al[i].bo, &off); IN_USE_CHECK(); } while (0)
#define IN_USE_FREE(i) do { nouveau_bo_ref(NULL, &al[i].bo); nouveau_mm_free(al[i].mm); al[i].mm = NULL; IN_USE_CHECK(); } while (0)
	/* 256-byte chunks, 16 to a 4 KiB slab: K full and S with one */
	for (int i = 0; i < 17; i++)
		IN_USE_ALLOC(i);
	struct mm_slab *k = al[0].mm->priv, *s = al[16].mm->priv;
	CHECK(k != s && al[15].mm->priv == k, "in use: the first 16 not in one slab");
	/* K empties and is kept; S fills */
	for (int i = 0; i < 16; i++)
		IN_USE_FREE(i);
	for (int i = 17; i < 32; i++)
		IN_USE_ALLOC(i);
	CHECK(al[31].mm->priv == s, "in use: S didn't take the next 15");
	/* the next allocation takes the kept slab K */
	int before = bo_made;
	IN_USE_ALLOC(0);
	CHECK(bo_made == before && al[0].mm->priv == k, "in use: the kept slab not taken (%d objects made)", bo_made - before);
	/* S empties: K, in use, stays, and S is kept */
	for (int i = 16; i < 32; i++)
		IN_USE_FREE(i);
	CHECK(on_list(&mm_bucket_by_size(c, 256)->free, s), "in use: the slab that emptied not kept");
	if (!keep_check("in use", c, 1))
		return false;
	printf("in use: the kept slab taken into use stayed while another one emptied\n");
	IN_USE_FREE(0);
	nouveau_mm_destroy(c);
	return true;
#undef IN_USE_CHECK
#undef IN_USE_ALLOC
#undef IN_USE_FREE
}

/*
 * A slab whose last chunk is freed while a pending submission still references its object (keep 0: the
 * slab is released at once): its object closes only when that reference goes, after the GPU is done.
 * The submission's reference is an extra nouveau_bo_ref, as the pushbuf's kref is.
 */
static void keep_deferred(void)
{
	union nouveau_bo_config cfg = {};
	memset(&dev, 0, sizeof(dev));
	struct nouveau_mman *c = nouveau_mm_create(&dev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP, &cfg);
	nouveau_mm_set_keep(c, 0);
	struct nouveau_bo *bo = NULL, *pending = NULL;
	uint32_t off;
	int alive0 = bo_alive;
	struct nouveau_mm_allocation *mm = nouveau_mm_allocate(c, 256, &bo, &off);
	CHECK(mm && bo && bo_alive == alive0 + 1, "deferred: no slab made (%d objects)", bo_alive - alive0);
	nouveau_bo_ref(bo, &pending);		/* the submission that used it, not yet done */
	nouveau_bo_ref(NULL, &bo);		/* the buffer's own reference (its fence work) */
	nouveau_mm_free(mm);			/* the last chunk: the slab is released (keep 0) */
	bool open = bo_alive == alive0 + 1;
	CHECK(list_is_empty(&mm_bucket_by_size(c, 256)->free) && open,
	      "deferred: the slab's object closed while a submission still references it (%d alive)", bo_alive - alive0);
	if (!open) {
		fflush(stdout);
		nouveau_mm_destroy(c);
		return;			/* (pending names a freed object: not touched) */
	}
	nouveau_bo_ref(NULL, &pending);		/* the submission is done */
	CHECK(bo_alive == alive0, "deferred: the slab's object still open after its last reference (%d alive)",
	      bo_alive - alive0);
	printf("deferred: a released slab's object closed at its last reference, after the pending submission\n");
	nouveau_mm_destroy(c);
}

static void part_keep(void)
{
	if (!keep_in_use())
		return;	/* (the pool's state is broken: the rest would read freed slabs) */
	keep_option(NULL, -1);	/* every slab kept by default, as before 0076 (review M1) */
	keep_deferred();
	keep_option("1", 1);
	keep_option("0", 0);
	keep_option("2", 2);
	keep_option("-1", -1);
	keep_option("-7", -1);
	keep_load(1);
	keep_load(0);
	keep_load(2);
	keep_load(-1);
	/* other GPUs: the pools keep every slab, as before */
	setup(false);
	CHECK(GART->keep == -1 && screen.base.mm_VRAM->keep == -1, "keep: off the PS3 the pools keep %d and %d, want -1",
	      GART->keep, screen.base.mm_VRAM->keep);
	teardown();
}

/* --- stats ------------------------------------------------------------------------------------ */
static void stats_want(const char *what, struct nouveau_mman *c, uint64_t bytes, uint64_t peak, unsigned made,
		       unsigned freed, unsigned kept)
{
	struct nouveau_mm_stats st;
	nouveau_mm_get_stats(c, &st);
	CHECK(st.bytes == bytes && st.peak == peak && st.slabs_made == made && st.slabs_freed == freed && st.kept == kept,
	      "stats %s (%s): %" PRIu64 " bytes, peak %" PRIu64 ", %u made, %u freed, %u kept; want %" PRIu64 ", %" PRIu64
	      ", %u, %u, %u", what, pool_name(c), st.bytes, st.peak, st.slabs_made, st.slabs_freed, st.kept, bytes, peak,
	      made, freed, kept);
}

static void part_stats(void)
{
	setenv("NV30_PS3_GART_STATS", "1", 1);
	setenv("NV30_PS3_MM_KEEP_SLAB", "1", 1);	/* (the counts of freed and kept slabs: not the default) */
	setup(true);
	CHECK(screen.base.gart_stats, "stats: NV30_PS3_GART_STATS=1 not taken");
	stats_want("at the start", GART, 0, 0, 0, 0, 0);
	stats_want("at the start", STREAM, 0, 0, 0, 0, 0);
	/* 33 index buffers of 256 bytes: three 4 KiB slabs in mm_GART; a 1 MiB stream buffer: a 4 MiB slab */
	struct nv04_resource *ib[33], *s1, *s2, *big;
	for (int i = 0; i < 33; i++)
		ib[i] = mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, 200, 0);
	s1 = mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_STREAM, 1 << 20, 0);
	stats_want("33 small, 1 stream", GART, 12288, 12288, 3, 0, 0);
	stats_want("33 small, 1 stream", STREAM, 4 << 20, 4 << 20, 1, 0, 0);
	/* all the small ones go: one slab kept, two freed; the peak stays */
	for (int i = 0; i < 33; i++)
		unref(ib[i]);
	stats_want("small ones freed", GART, 4096, 12288, 3, 2, 1);
	/* the kept slab is used again, no new one */
	ib[0] = mk(PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_DEFAULT, 256, 0);
	stats_want("one again", GART, 4096, 12288, 3, 2, 0);
	/* a second stream slab and one past the slab sizes (an object of its own, not counted) */
	s2 = mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_STREAM, 2 << 20, 0);	/* (a 4 MiB slab of 2 MiB chunks) */
	big = mk(PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_STREAM, 5 << 20, 0);
	CHECK(big->bo && !big->mm, "stats: a 5 MiB buffer suballocated");
	stats_want("two stream slabs", STREAM, 8 << 20, 8 << 20, 2, 0, 0);
	unref(s1);
	unref(s2);
	unref(big);
	stats_want("stream ones freed", STREAM, 8 << 20, 8 << 20, 2, 0, 2);	/* (two buckets, one kept in each) */
	unref(ib[0]);
	stats_want("at the end", GART, 4096, 12288, 3, 2, 1);

	/* the line at the screen's end */
	char path[] = "/tmp/gart-pool-test-XXXXXX";
	int fd = mkstemp(path), saved = dup(2);
	CHECK(fd >= 0, "stats: no temporary file");
	fflush(stderr);
	dup2(fd, 2);
	teardown();
	fflush(stderr);
	dup2(saved, 2);
	close(saved);
	char line[1024] = "";
	lseek(fd, 0, SEEK_SET);
	ssize_t n = read(fd, line, sizeof(line) - 1);
	line[n > 0 ? n : 0] = 0;
	close(fd);
	unlink(path);
	const char *want = "nv30: GART slabs at the end (NV30_PS3_GART_STATS): mm_GART 4 KiB, peak 12 KiB, 3 made, 2 freed, "
			   "1 kept empty; mm_GART_stream 8192 KiB, peak 8192 KiB, 2 made, 0 freed, 2 kept empty\n";
	CHECK(!strcmp(line, want), "stats: the line at the end is\n  %s want\n  %s", line, want);
	CHECK(bo_alive == 0, "stats: %d objects left after the screen's end", bo_alive);

	/* off: no line */
	unsetenv("NV30_PS3_GART_STATS");
	setup(true);
	CHECK(!screen.base.gart_stats, "stats: on without NV30_PS3_GART_STATS");
	strcpy(path, "/tmp/gart-pool-test-XXXXXX");
	fd = mkstemp(path);
	CHECK(fd >= 0, "stats: no temporary file");
	saved = dup(2);
	fflush(stderr);
	dup2(fd, 2);
	teardown();
	fflush(stderr);
	dup2(saved, 2);
	close(saved);
	n = pread(fd, line, sizeof(line) - 1, 0);
	line[n > 0 ? n : 0] = 0;
	CHECK(n == 0, "stats: a line at the end without NV30_PS3_GART_STATS: %s", line);
	close(fd);
	unlink(path);
}

int main(int argc, char **argv)
{
	const char *part = argc > 1 ? argv[1] : "";

	setvbuf(stdout, NULL, _IOLBF, 0);	/* (each FAIL line out before a sanitizer's abort) */

	if (!strcmp(part, "choice"))
		part_choice();
	else if (!strcmp(part, "keep"))
		part_keep();
	else if (!strcmp(part, "stats"))
		part_stats();
	else {
		printf("usage: %s choice|keep|stats\n", argv[0]);
		return 2;
	}
	printf("%s %s: %d checks, %d failed\n", fails ? "FAIL" : "ok", part, checks, fails);
	return fails != 0;
}
