/* context-pushbuf-test: nv30_context.c's flush and destroy at the edges Mesa patch 0003 ("nv30:
 * push buffer robustness in the context") covers. nv30_context.c is compiled in whole; the push
 * buffer calls are faked here, everything else it calls is a stub (linked with unresolved symbols
 * ignored: a call to one of those would crash, which counts as a failure too).
 *   flush: a context that never queued anything has no push buffer space (cur == end == NULL);
 *   the kick's fence (kick_notify) is written at cur, so before the kick the flush must get space.
 *   destroy: a context whose nouveau_context_init failed (out of GART) has no push buffer; destroy
 *   runs from nv30_context_create's failure path and must not hand NULL to the winsys.
 * Each case can run alone (argument flush or destroy); tools/run-tests.sh --only mesa-alloc-fail runs
 * both against this file and against nv30_context.c as it was before 0003, where each must fail. */
#include "nv30/nv30_context.c"

static int fails, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t pushmem[256];
static int kicks, spaces, bad_kicks, null_push;

/* the winsys: space is a fresh buffer; a kick writes the fence kick_notify adds at cur */
int nouveau_pushbuf_space(struct nouveau_pushbuf *push, uint32_t dwords, uint32_t relocs, uint32_t pushes)
{
	(void)relocs; (void)pushes;
	if (!push) {
		null_push++;
		return -EINVAL;
	}
	spaces++;
	if (!push->cur || push->cur + dwords > push->end) {
		push->cur = pushmem;
		push->end = pushmem + ARRAY_SIZE(pushmem);
	}
	return 0;
}
int nouveau_pushbuf_kick(struct nouveau_pushbuf *push)
{
	if (!push) {
		null_push++;
		return -EINVAL;
	}
	kicks++;
	if (!push->cur || push->cur + 4 > push->end) {
		bad_kicks++;	/* the real kick_notify writes these 4 words through cur */
		return 0;
	}
	for (int i = 0; i < 4; i++)
		*push->cur++ = 0xfe0ce000 + i;
	push->cur = pushmem;	/* submitted */
	return 0;
}
struct nouveau_bufctx *nouveau_pushbuf_bufctx(struct nouveau_pushbuf *push, struct nouveau_bufctx *ctx)
{
	(void)ctx;
	if (!push)
		null_push++;
	return NULL;
}

/* what destroy and flush call besides: nothing to do in this model */
void nv30_shader_cache_report(struct nv30_screen *screen, const char *event) { (void)screen; (void)event; }
void nv30_framebuffer_init(struct pipe_context *pctx, const struct pipe_framebuffer_state *fb,
			   struct pipe_surface **cbufs, struct pipe_surface **zsbuf)
{
	(void)pctx; (void)fb; (void)cbufs; (void)zsbuf;
}
void util_unreference_framebuffer_state(struct pipe_framebuffer_state *fb) { (void)fb; }
void nouveau_bufctx_del(struct nouveau_bufctx **pctx) { *pctx = NULL; }
void nouveau_fence_cleanup(struct nouveau_context *nv) { (void)nv; }
void nouveau_pushbuf_destroy(struct nouveau_pushbuf **push) { (void)push; }
void nouveau_client_del(struct nouveau_client **client) { (void)client; }
void nouveau_bo_ref(struct nouveau_bo *bo, struct nouveau_bo **ref) { (void)bo; *ref = NULL; }
void nv30_staging_big_age(struct nv30_screen *screen) { (void)screen; }
int mtx_lock(mtx_t *m) { (void)m; return 0; }
int mtx_unlock(mtx_t *m) { (void)m; return 0; }
void _simple_mtx_plain_init_once(simple_mtx_t *m) { (void)m; }
void util_call_once_data_slow(once_flag *once, util_call_once_data_func func, const void *val)
{
	(void)once; func(val);
}

static struct nv30_screen screen;
static struct nouveau_pushbuf_priv ppush;

static struct nv30_context *new_context(int with_pushbuf)
{
	struct nv30_context *nv30 = calloc(1, sizeof(*nv30));
	struct nouveau_pushbuf *push = calloc(1, sizeof(*push));

	ppush.screen = &screen.base;
	push->user_priv = &ppush;
	push->rsvd_kick = 16;
	nv30->screen = &screen;
	nv30->base.screen = &screen.base;
	nv30->base.pipe.screen = &screen.base.base;
	if (with_pushbuf)
		nv30->base.pushbuf = push;
	else
		free(push);
	return nv30;
}

/* a context that never queued anything is flushed (glFlush before any draw) */
static void test_flush(void)
{
	struct nv30_context *nv30 = new_context(1);
	struct nouveau_pushbuf *push = nv30->base.pushbuf;

	kicks = spaces = bad_kicks = 0;
	nv30_context_flush(&nv30->base.pipe, NULL, 0);
	CHECK(bad_kicks == 0, "the kick came with no push buffer space (cur %p): kick_notify writes its fence "
	      "through NULL", (void *)push->cur);
	CHECK(spaces >= 1, "no space asked before the kick");
	CHECK(kicks == 1, "%d kicks, want 1", kicks);

	/* and a second flush with space in hand kicks again */
	nv30_context_flush(&nv30->base.pipe, NULL, PIPE_FLUSH_END_OF_FRAME);
	CHECK(kicks == 2 && bad_kicks == 0, "second flush: %d kicks, %d without space", kicks, bad_kicks);
	free(push);
	free(nv30);
}

/* nv30_context_create failed before the push buffer existed: its failure path destroys the context */
static void test_destroy(void)
{
	struct nv30_context *nv30 = new_context(0);

	null_push = 0;
	nv30_context_destroy(&nv30->base.pipe);
	CHECK(null_push == 0, "destroy handed the winsys a NULL push buffer %d times (a NULL dereference "
	      "in nouveau_pushbuf_bufctx)", null_push);
}

int main(int argc, char **argv)
{
	int all = argc < 2;

	setvbuf(stdout, NULL, _IONBF, 0);
	for (int i = 1; all || i < argc; i++) {
		if (all || !strcmp(argv[i], "flush"))
			test_flush();
		if (all || !strcmp(argv[i], "destroy"))
			test_destroy();
		if (all)
			break;
	}
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
