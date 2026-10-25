/* alloc-fail-test: what u_upload_mgr.c and nouveau_mm.c do when memory runs out (Mesa patches
 * 0047 "u_upload, st, nv30: an upload buffer that cannot be had is not written through" and
 * 0048 "nouveau: a buffer allocation that fails fails the call ..."). Both files are compiled in
 * whole against a fake pipe and a fake nouveau_bo_new.
 *   u_upload: a failed new buffer gives ptr NULL, *outbuf NULL (not an earlier call's buffer) and
 *   the previous buffer back in *releasebuf, so the caller can release it (it was leaked); a map
 *   failure leaves no buffer behind to be released twice; at the end no resource is left alive.
 *   nouveau_mm: when a 4 MiB slab cannot be had, a 1 MiB request still gets a buffer of its own.
 * (tools/run-tests.sh --only mesa-query-steal runs it; skipped without build/work/mesa) */
#include "util/u_upload_mgr.c"
#include "nouveau_mm.c"

static int fails, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

/* --- a fake screen and context: buffers in malloc memory, creation and maps that can fail ---- */
struct fake_res { struct pipe_resource base; uint8_t *data; };
static int live, fail_create, fail_map;
static struct pipe_screen screen;
static struct pipe_context ctx;
static struct pipe_transfer xfer;

static struct pipe_resource *fake_create(struct pipe_screen *s, const struct pipe_resource *t)
{
	if (fail_create)
		return NULL;
	struct fake_res *r = calloc(1, sizeof(*r));
	r->base = *t;
	r->base.screen = s;
	pipe_reference_init(&r->base.reference, 1);
	r->data = malloc(t->width0);
	live++;
	return &r->base;
}
static void fake_destroy(struct pipe_screen *s, struct pipe_resource *pres)
{
	(void)s;
	struct fake_res *r = (struct fake_res *)pres;
	free(r->data);
	free(r);
	live--;
}
static void *fake_map(struct pipe_context *p, struct pipe_resource *res, unsigned level, unsigned usage,
		      const struct pipe_box *box, struct pipe_transfer **out)
{
	(void)p; (void)level; (void)usage;
	if (fail_map)
		return NULL;
	xfer.resource = res;
	xfer.box = *box;
	*out = &xfer;
	return ((struct fake_res *)res)->data + box->x;
}
static void fake_unmap(struct pipe_context *p, struct pipe_transfer *t) { (void)p; (void)t; }
static void fake_flush(struct pipe_context *p, struct pipe_transfer *t, const struct pipe_box *b)
{
	(void)p; (void)t; (void)b;
}

static void test_upload(void)
{
	screen.resource_create = fake_create;
	screen.resource_destroy = fake_destroy;
	ctx.screen = &screen;
	ctx.buffer_map = fake_map;
	ctx.buffer_unmap = fake_unmap;
	ctx.transfer_flush_region = fake_flush;
	ctx.resource_release = u_default_resource_release;

	struct u_upload_mgr *up = u_upload_create(&ctx, 4096, PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_STREAM, 0);
	struct pipe_resource *buf = NULL, *rel = NULL;
	unsigned off;
	void *ptr;

	/* a first buffer, filled */
	u_upload_alloc(up, 0, 4000, 16, &off, &buf, &rel, &ptr);
	CHECK(ptr && buf && !rel, "first allocation: ptr %p buf %p rel %p", ptr, (void *)buf, (void *)rel);
	struct pipe_resource *first = buf;

	/* the next one does not fit: a new buffer is needed, and it cannot be created */
	fail_create = 1;
	u_upload_alloc(up, 0, 4000, 16, &off, &buf, &rel, &ptr);
	CHECK(!ptr, "failed allocation gave a pointer");
	CHECK(!buf, "failed allocation left *outbuf at %p (the first buffer is %p)", (void *)buf, (void *)first);
	CHECK(rel == first, "failed allocation: *releasebuf %p, want the previous buffer %p", (void *)rel,
	      (void *)first);
	pipe_resource_release(&ctx, rel);
	rel = NULL;

	/* again: nothing more to give back */
	u_upload_alloc(up, 0, 4000, 16, &off, &buf, &rel, &ptr);
	CHECK(!ptr && !buf && !rel, "second failure: ptr %p buf %p rel %p", ptr, (void *)buf, (void *)rel);
	fail_create = 0;

	/* created but its map fails: nothing may be left in the uploader */
	fail_map = 1;
	u_upload_alloc(up, 0, 4000, 16, &off, &buf, &rel, &ptr);
	CHECK(!ptr && !buf, "map failure: ptr %p buf %p", ptr, (void *)buf);
	pipe_resource_release(&ctx, rel);
	rel = NULL;
	fail_map = 0;

	/* and it recovers */
	u_upload_alloc(up, 0, 4000, 16, &off, &buf, &rel, &ptr);
	CHECK(ptr && buf, "allocation after the failures: ptr %p buf %p", ptr, (void *)buf);
	if (ptr)
		memset(ptr, 0xab, 4000);
	pipe_resource_release(&ctx, rel);
	rel = NULL;
	u_upload_destroy(up);
	CHECK(live == 0, "%d resources still alive after destroy (leaked)", live);
}

/* --- a fake nouveau_bo_new: buffers up to a size limit -------------------------------------- */
static uint64_t bo_limit = ~0ull;
static int bos;
int nouveau_bo_new(struct nouveau_device *dev, uint32_t flags, uint32_t align, uint64_t size,
		   union nouveau_bo_config *config, struct nouveau_bo **pbo)
{
	(void)dev; (void)flags; (void)align; (void)config;
	if (size > bo_limit)
		return -ENOSPC;
	struct nouveau_bo *bo = calloc(1, sizeof(*bo));
	bo->size = size;
	bo->handle = 1;	/* refcount stand-in below */
	*pbo = bo;
	bos++;
	return 0;
}
void nouveau_bo_ref(struct nouveau_bo *bo, struct nouveau_bo **ref)
{
	if (bo)
		bo->handle++;
	if (*ref && --(*ref)->handle == 0) {
		free(*ref);
		bos--;
	}
	*ref = bo;
}
int nouveau_mesa_debug;
int mtx_lock(mtx_t *m) { (void)m; return 0; }
int mtx_unlock(mtx_t *m) { (void)m; return 0; }
int mtx_trylock(mtx_t *m) { (void)m; return thrd_busy; }	/* simple_mtx_assert_locked() asks */
void _simple_mtx_plain_init_once(simple_mtx_t *m) { (void)m; }
void util_call_once_data_slow(once_flag *once, util_call_once_data_func func, const void *val)
{
	(void)once; func(val);
}
#ifndef simple_mtx_init
void simple_mtx_init(simple_mtx_t *m, int type) { (void)type; memset(m, 0, sizeof(*m)); }
void simple_mtx_destroy(simple_mtx_t *m) { (void)m; }
#endif

static void test_mm(void)
{
	union nouveau_bo_config cfg = { 0 };
	struct nouveau_mman *mm = nouveau_mm_create(NULL, NOUVEAU_BO_GART, &cfg);
	struct nouveau_bo *bo = NULL;
	uint32_t off = 123;

	/* room for 3 MiB at once, not for the 4 MiB slab of 1 MiB chunks */
	bo_limit = 3 << 20;
	struct nouveau_mm_allocation *a = nouveau_mm_allocate(mm, 1 << 20, &bo, &off);
	CHECK(bo != NULL, "1 MiB request with no room for a 4 MiB slab: no buffer");
	CHECK(!a && off == 0, "a buffer of its own comes without a slab allocation (alloc %p, offset %u)",
	      (void *)a, off);
	CHECK(!bo || bo->size >= (1 << 20), "its size %llu", bo ? (unsigned long long)bo->size : 0ull);
	nouveau_bo_ref(NULL, &bo);

	/* no room at all: the caller sees no buffer */
	bo_limit = 0;
	a = nouveau_mm_allocate(mm, 1 << 20, &bo, &off);
	CHECK(!a && !bo, "no memory: alloc %p bo %p", (void *)a, (void *)bo);

	/* room for slabs again: chunks from a slab */
	bo_limit = ~0ull;
	a = nouveau_mm_allocate(mm, 1 << 20, &bo, &off);
	CHECK(a && bo && bo->size == (4 << 20), "slab allocation: alloc %p bo size %llu", (void *)a,
	      bo ? (unsigned long long)bo->size : 0ull);
	if (a)
		nouveau_mm_free(a);
	nouveau_bo_ref(NULL, &bo);
	nouveau_mm_destroy(mm);
	CHECK(bos == 0, "%d buffer objects left", bos);
}

int main(void)
{
	test_upload();
	test_mm();
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
