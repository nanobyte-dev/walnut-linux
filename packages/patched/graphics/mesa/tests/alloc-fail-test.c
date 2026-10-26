/* alloc-fail-test: what u_upload_mgr.c, nouveau_mm.c and nouveau_fence.c do when memory runs out
 * (Mesa patches 0047 "u_upload, st, nv30: an upload buffer that cannot be had is not written
 * through", 0048 "nouveau: a buffer allocation that fails fails the call ..." and 0049 "nouveau,
 * nv30: out of GART, a context keeps its fence ..."). The files are compiled in whole against a
 * fake pipe, a fake GPU and a fake nouveau_bo_new.
 *   u_upload: a failed new buffer gives ptr NULL, *outbuf NULL (not an earlier call's buffer) and
 *   the previous buffer back in *releasebuf, so the caller can release it (it was leaked); a map
 *   failure leaves no buffer behind to be released twice; at the end no resource is left alive.
 *   nouveau_mm: when a 4 MiB slab cannot be had, a 1 MiB request still gets a buffer of its own.
 *   nouveau_fence: with no GART for a fence's wait object the context still has a current fence
 *   (before 0049 it was NULL and the next kick segfaulted at 0x20, SuperTuxKart 2026-10-26); the
 *   GPU's submitted work is waited for first, which gives buffers back; a fence without a wait
 *   object is emitted without one and its wait polls the sequence.
 * (tools/run-tests.sh --only mesa-query-steal runs it; skipped without build/work/mesa) */
#include "util/u_upload_mgr.c"
#include "nouveau_mm.c"
#include "nouveau_fence.c"

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
static int bos, bo_cap = -1;	/* bo_cap: no new buffer while that many are alive (a full GART) */
int nouveau_bo_new(struct nouveau_device *dev, uint32_t flags, uint32_t align, uint64_t size,
		   union nouveau_bo_config *config, struct nouveau_bo **pbo)
{
	(void)dev; (void)flags; (void)align; (void)config;
	if (size > bo_limit || (bo_cap >= 0 && bos >= bo_cap))
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

/* --- a fake GPU for nouveau_fence.c: emit records the wait object, the GPU's sequence moves when
 * a wait object is waited on (nouveau_bo_wait) or after a few polls ----------------------------- */
static struct nouveau_screen fscreen;
static struct nouveau_context fctx;
static struct nouveau_pushbuf fpush;
static uint32_t pushmem[64];
static uint32_t gpu_seq;		/* the last sequence the GPU wrote */
static int emits, emits_nobo, waits, polls, polls_to_signal, kicks;

static void fake_emit(struct pipe_context *p, uint32_t *sequence, struct nouveau_bo *wait)
{
	(void)p;
	*sequence = ++fscreen.fence.sequence;
	emits++;
	if (wait)
		wait->offset = *sequence;	/* the fence this wait object belongs to */
	else
		emits_nobo++;
}
static uint32_t fake_update(struct pipe_screen *s)
{
	(void)s;
	if (polls_to_signal && --polls_to_signal == 0)
		gpu_seq = fscreen.fence.sequence;
	polls++;
	return gpu_seq;
}
int nouveau_bo_wait(struct nouveau_bo *bo, uint32_t access, struct nouveau_client *client)
{
	(void)access; (void)client;
	waits++;
	if (bo->offset > gpu_seq)
		gpu_seq = bo->offset;
	return 0;
}
/* nv30_context_kick_notify(), as far as fences go */
int nouveau_pushbuf_kick(struct nouveau_pushbuf *push)
{
	(void)push;
	kicks++;
	if (!_nouveau_fence_next(&fctx))
		return -EINVAL;
	_nouveau_fence_update(&fscreen, true);
	return 0;
}
int nouveau_pushbuf_space(struct nouveau_pushbuf *push, uint32_t dwords, uint32_t relocs, uint32_t pushes)
{
	(void)push; (void)dwords; (void)relocs; (void)pushes;
	return 0;
}
int64_t os_time_get_nano(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000000ll + t.tv_nsec;
}
void _util_debug_message(struct util_debug_callback *cb, unsigned *id, enum util_debug_type type,
			 const char *fmt, ...)
{
	(void)cb; (void)id; (void)type; (void)fmt;
}

static unsigned fence_count(void)
{
	unsigned n = 0;
	for (struct nouveau_fence *f = fscreen.fence.head; f; f = f->next)
		n++;
	return n;
}

static void test_fence(void)
{
	fscreen.fence.emit = fake_emit;
	fscreen.fence.update = fake_update;
	nouveau_fence_list_init(&fscreen.fence);
	fpush.cur = pushmem;
	fpush.end = pushmem + ARRAY_SIZE(pushmem);
	fctx.screen = &fscreen;
	fctx.pushbuf = &fpush;
	bo_limit = ~0ull;
	bo_cap = -1;
	bos = 0;

	CHECK(nouveau_fence_new(&fctx, &fctx.fence) && fctx.fence->bo, "first fence and its wait object");

	/* a few batches, each fenced and submitted; the GPU runs none of them yet */
	struct nouveau_fence *f;
	for (int i = 0; i < 4; i++) {
		f = NULL;
		simple_mtx_lock(&fscreen.fence.lock);
		_nouveau_fence_ref(fctx.fence, &f);
		CHECK(nouveau_fence_kick(f), "kick %d", i);
		_nouveau_fence_ref(NULL, &f);
		simple_mtx_unlock(&fscreen.fence.lock);
	}
	CHECK(fence_count() == 4 && bos == 5, "4 fences in flight (%u), 5 wait objects (%d)", fence_count(), bos);

	/* the GART is full: the next fence's wait object comes from the submitted work's, waited for */
	bo_cap = bos;
	waits = 0;
	f = NULL;
	simple_mtx_lock(&fscreen.fence.lock);
	_nouveau_fence_ref(fctx.fence, &f);
	CHECK(nouveau_fence_kick(f), "kick with a full GART");
	CHECK(fctx.fence != NULL, "no current fence after the kick (the 0x20 segfault)");
	CHECK(fctx.fence && fctx.fence->bo, "the new fence has no wait object: nothing was reclaimed");
	CHECK(waits == 1, "%d waits for the submitted work, want 1", waits);
	CHECK(fence_count() == 1, "%u fences left in flight, want 1 (the one just submitted)", fence_count());
	_nouveau_fence_ref(NULL, &f);
	simple_mtx_unlock(&fscreen.fence.lock);

	/* no GART at all, nothing submitted to wait for: fences without wait objects */
	gpu_seq = fscreen.fence.sequence;
	simple_mtx_lock(&fscreen.fence.lock);
	_nouveau_fence_update(&fscreen, false);
	simple_mtx_unlock(&fscreen.fence.lock);
	bo_limit = 0;
	emits_nobo = 0;
	for (int i = 0; i < 3; i++) {
		f = NULL;
		simple_mtx_lock(&fscreen.fence.lock);
		_nouveau_fence_ref(fctx.fence, &f);
		CHECK(nouveau_fence_kick(f), "kick %d with no GART", i);
		CHECK(fctx.fence != NULL, "kick %d with no GART: no current fence", i);
		_nouveau_fence_ref(NULL, &f);
		simple_mtx_unlock(&fscreen.fence.lock);
	}
	CHECK(fctx.fence && !fctx.fence->bo, "with no GART the current fence has no wait object");
	CHECK(emits_nobo == 2, "%d fences emitted without a wait object, want 2", emits_nobo);

	/* waiting on one polls the sequence (the GPU gets there after 3 polls) */
	f = NULL;
	simple_mtx_lock(&fscreen.fence.lock);
	_nouveau_fence_ref(fctx.fence, &f);
	polls = 0;
	polls_to_signal = 3;
	waits = 0;
	CHECK(_nouveau_fence_wait(f, NULL), "the wait on a fence without a wait object failed");
	CHECK(f->state == NOUVEAU_FENCE_STATE_SIGNALLED, "state %d after the wait", f->state);
	CHECK(waits == 0 && polls >= 3, "%d kernel waits (want 0), %d polls (want 3 or more)", waits, polls);
	_nouveau_fence_ref(NULL, &f);
	simple_mtx_unlock(&fscreen.fence.lock);

	/* the GART comes back: wait objects again */
	bo_limit = ~0ull;
	bo_cap = -1;
	f = NULL;
	simple_mtx_lock(&fscreen.fence.lock);
	_nouveau_fence_ref(fctx.fence, &f);
	CHECK(nouveau_fence_kick(f), "kick after the GART came back");
	CHECK(fctx.fence && fctx.fence->bo, "no wait object after the GART came back");
	_nouveau_fence_ref(NULL, &f);
	simple_mtx_unlock(&fscreen.fence.lock);

	/* nouveau_fence_cleanup() as at context destruction: nothing left */
	gpu_seq = fscreen.fence.sequence;
	nouveau_fence_cleanup(&fctx);
	simple_mtx_lock(&fscreen.fence.lock);
	_nouveau_fence_update(&fscreen, false);
	simple_mtx_unlock(&fscreen.fence.lock);
	CHECK(fence_count() == 0 && bos == 0, "%u fences, %d wait objects left", fence_count(), bos);
}

int main(void)
{
	test_upload();
	test_mm();
	test_fence();
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
