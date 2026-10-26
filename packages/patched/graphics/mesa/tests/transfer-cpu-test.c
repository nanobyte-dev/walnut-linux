/* transfer-cpu-test: nv30 texture transfers when the GART has no room for a staging buffer (Mesa
 * patch 0049 "nouveau, nv30: out of GART, a context keeps its fence ..."). nv30_miptree.c and
 * nv30_transfer.c are compiled in whole against a fake device: VRAM buffers in malloc memory,
 * GART buffers that cannot be had.
 *   Before 0049 the map failed and the upload was lost (SuperTuxKart: other textures' old pixels).
 *   Now: the map waits for the GPU and drains the staging pools, tries again, then hands out malloc
 *   memory; the unmap maps the texture (waiting for the GPU's use of it) and the CPU writes the
 *   texels where the GPU would have: swizzled, linear and DXT1 textures, a sub-box, a read back.
 * (tools/run-tests.sh --only mesa-query-steal runs it; skipped without build/work/mesa) */
#include "nv30/nv30_miptree.c"
#include "nv30/nv30_transfer.c"

static int fails, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

/* --- formats: just the two the test uses ------------------------------------------------------- */
static const struct util_format_description desc_bgra = {
	.format = PIPE_FORMAT_B8G8R8A8_UNORM, .name = "PIPE_FORMAT_B8G8R8A8_UNORM",
	.block = { 1, 1, 1, 32 }, .layout = UTIL_FORMAT_LAYOUT_PLAIN, .nr_channels = 4,
};
static const struct util_format_description desc_dxt1 = {
	.format = PIPE_FORMAT_DXT1_RGB, .name = "PIPE_FORMAT_DXT1_RGB",
	.block = { 4, 4, 1, 64 }, .layout = UTIL_FORMAT_LAYOUT_S3TC, .nr_channels = 3,
};
const struct util_format_description *util_format_description(enum pipe_format format)
{
	return format == PIPE_FORMAT_DXT1_RGB ? &desc_dxt1 : &desc_bgra;
}
bool util_format_is_pure_integer(enum pipe_format format) { (void)format; return false; }

/* --- the device: VRAM in malloc memory, GART that may be full ---------------------------------- */
static int gart_full, bos, gart_tries, maps_wr, waits_idle;
int nouveau_bo_new(struct nouveau_device *dev, uint32_t flags, uint32_t align, uint64_t size,
		   union nouveau_bo_config *config, struct nouveau_bo **pbo)
{
	(void)dev; (void)align; (void)config;
	if (flags & NOUVEAU_BO_GART) {
		gart_tries++;
		if (gart_full)
			return -ENOSPC;
	}
	struct nouveau_bo *bo = calloc(1, sizeof(*bo) + size);
	bo->size = size;
	bo->flags = flags;
	bo->handle = 1;		/* refcount stand-in */
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
int nouveau_bo_map(struct nouveau_bo *bo, uint32_t access, struct nouveau_client *client)
{
	(void)client;
	bo->map = (char *)(bo + 1);
	if (access & NOUVEAU_BO_WR)
		maps_wr++;
	return 0;
}
bool nouveau_fence_wait_idle(struct nouveau_context *nv) { (void)nv; waits_idle++; return true; }

/* never reached with a full GART: the GPU paths and the rest of the driver */
#define UNREACHED(...) { fails++; printf("FAIL: %s called\n", __func__); }
int nouveau_pushbuf_space(struct nouveau_pushbuf *p, uint32_t d, uint32_t r, uint32_t q)
{ (void)p; (void)d; (void)r; (void)q; UNREACHED(); return -1; }
int nouveau_pushbuf_refn(struct nouveau_pushbuf *p, struct nouveau_pushbuf_refn *r, int n)
{ (void)p; (void)r; (void)n; UNREACHED(); return -1; }
void nouveau_pushbuf_reloc(struct nouveau_pushbuf *p, struct nouveau_bo *b, uint32_t d, uint32_t f,
			   uint32_t v, uint32_t o) { (void)p; (void)b; (void)d; (void)f; (void)v; (void)o; UNREACHED(); }
bool nouveau_buffer_migrate(struct nouveau_context *n, struct nv04_resource *r, unsigned d)
{ (void)n; (void)r; (void)d; UNREACHED(); return false; }
void nouveau_copy_buffer(struct nouveau_context *n, struct nv04_resource *d, unsigned a,
			 struct nv04_resource *s, unsigned b, unsigned c)
{ (void)n; (void)d; (void)a; (void)s; (void)b; (void)c; UNREACHED(); }
void nouveau_fence_ref(struct nouveau_fence *f, struct nouveau_fence **r, struct nouveau_screen *s)
{ (void)f; (void)r; (void)s; }
bool nouveau_fence_wait(struct nouveau_fence *f, struct util_debug_callback *d) { (void)f; (void)d; return true; }
bool nouveau_fence_work(struct nouveau_fence *f, void (*func)(void *), void *data)
{ (void)f; func(data); return true; }
void nouveau_fence_unref_bo(void *data) { struct nouveau_bo *bo = data; nouveau_bo_ref(NULL, &bo); }
int nouveau_heap_alloc(struct nouveau_heap *h, unsigned s, void *p, struct nouveau_heap **r)
{ (void)h; (void)s; (void)p; (void)r; UNREACHED(); return -1; }
void nouveau_heap_free(struct nouveau_heap **h) { (void)h; }
struct nouveau_bo *nouveau_screen_bo_from_handle(struct pipe_screen *s, struct winsys_handle *w, unsigned *st)
{ (void)s; (void)w; (void)st; return NULL; }
bool nouveau_screen_bo_get_handle(struct pipe_screen *s, struct nouveau_bo *b, unsigned st, struct winsys_handle *w)
{ (void)s; (void)b; (void)st; (void)w; return false; }
void util_blitter_blit(struct blitter_context *b, const struct pipe_blit_info *i, void *s)
{ (void)b; (void)i; (void)s; UNREACHED(); }
bool util_blitter_is_blit_supported(struct blitter_context *b, const struct pipe_blit_info *i)
{ (void)b; (void)i; return false; }
bool util_try_blit_via_copy_region(struct pipe_context *c, const struct pipe_blit_info *i, bool r)
{ (void)c; (void)i; (void)r; return false; }
void util_copy_framebuffer_state(struct pipe_framebuffer_state *d, const struct pipe_framebuffer_state *s)
{ (void)d; (void)s; }
int64_t os_time_get_nano(void) { return 0; }
void _simple_mtx_plain_init_once(simple_mtx_t *m) { (void)m; }
void util_call_once_data_slow(once_flag *once, util_call_once_data_func func, const void *val)
{
	(void)once; func(val);
}

/* x, y, width, height (pipe_box orders its fields x, width, y, height, z, depth) */
#define BOX(x_, y_, w_, h_) { .x = x_, .y = y_, .z = 0, .width = w_, .height = h_, .depth = 1 }

static struct nv30_screen screen;
static struct nv30_context ctx;

/* where texel (x, y) of a square swizzled level lives: x's bits on the even bits, y's on the odd */
static unsigned morton(unsigned x, unsigned y)
{
	unsigned m = 0;
	for (unsigned b = 0; b < 16; b++)
		m |= ((x >> b) & 1) << (2 * b) | ((y >> b) & 1) << (2 * b + 1);
	return m;
}

static struct pipe_resource *texture(enum pipe_format format, unsigned w, unsigned h)
{
	struct pipe_resource t = { .target = PIPE_TEXTURE_2D, .format = format, .width0 = w, .height0 = h,
				   .depth0 = 1, .array_size = 1, .bind = PIPE_BIND_SAMPLER_VIEW };
	return nv30_miptree_create(&screen.base.base, &t);
}

static void upload(struct pipe_resource *pt, struct pipe_box *box, uint32_t (*val)(int, int), unsigned bpp)
{
	struct pipe_transfer *tx = NULL;
	int before = gart_tries;
	uint8_t *map = nv30_miptree_transfer_map(&ctx.base.pipe, pt, 0, PIPE_MAP_WRITE, box, &tx);
	CHECK(map && tx, "a write map with a full GART failed (the upload is lost)");
	CHECK(gart_tries - before == 2, "%d GART tries, want 2 (before and after waiting for the GPU)",
	      gart_tries - before);
	if (!map)
		return;
	for (int y = 0; y < box->height; y++)
		for (int x = 0; x < box->width; x++) {
			uint32_t v = val(x, y);
			memcpy(map + y * tx->stride + x * bpp, &v, bpp);
		}
	nv30_miptree_transfer_unmap(&ctx.base.pipe, tx);
}

static uint32_t pattern(int x, int y) { return 0xa5000000u | (x << 12) | y; }
static uint32_t pattern2(int x, int y) { return 0x5a000000u | (y << 12) | x; }

static void test_swizzled(void)
{
	struct pipe_resource *pt = texture(PIPE_FORMAT_B8G8R8A8_UNORM, 64, 64);
	struct nv30_miptree *mt = nv30_miptree(pt);
	CHECK(mt->swizzled, "a 64x64 texture is not swizzled");

	/* the whole level, then a sub-box over it */
	struct pipe_box all = BOX(0, 0, 64, 64), sub = BOX(8, 4, 24, 40);
	waits_idle = maps_wr = 0;
	upload(pt, &all, pattern, 4);
	CHECK(waits_idle >= 1, "no wait for the GPU before giving up on the GART");
	CHECK(maps_wr == 1, "the unmap mapped the texture for writing %d times, want 1 (waits for the GPU)",
	      maps_wr);
	upload(pt, &sub, pattern2, 4);

	uint32_t *vram = (uint32_t *)mt->base.bo->map;
	CHECK(vram, "the texture was never written");
	if (!vram)
		return;
	int bad = 0;
	for (int y = 0; y < 64; y++)
		for (int x = 0; x < 64; x++) {
			bool in = x >= 8 && x < 32 && y >= 4 && y < 44;
			uint32_t want = in ? pattern2(x - 8, y - 4) : pattern(x, y);
			if (vram[morton(x, y)] != want && bad++ < 3)
				printf("  texel %d,%d: %08x, want %08x\n", x, y, vram[morton(x, y)], want);
		}
	CHECK(bad == 0, "%d swizzled texels wrong", bad);

	/* a read back goes the same way */
	struct pipe_transfer *tx = NULL;
	struct pipe_box rb = BOX(30, 40, 10, 10);
	uint8_t *map = nv30_miptree_transfer_map(&ctx.base.pipe, pt, 0, PIPE_MAP_READ, &rb, &tx);
	CHECK(map, "a read map with a full GART failed");
	if (map) {
		bad = 0;
		for (int y = 0; y < 10; y++)
			for (int x = 0; x < 10; x++) {
				int tx_ = 30 + x, ty = 40 + y;
				bool in = tx_ < 32 && ty < 44;
				uint32_t want = in ? pattern2(tx_ - 8, ty - 4) : pattern(tx_, ty);
				uint32_t got;
				memcpy(&got, map + y * tx->stride + x * 4, 4);
				bad += got != want;
			}
		CHECK(bad == 0, "%d texels read back wrong", bad);
		nv30_miptree_transfer_unmap(&ctx.base.pipe, tx);
	}
	pipe_resource_reference(&pt, NULL);
}

static void test_linear(void)
{
	struct pipe_resource *pt = texture(PIPE_FORMAT_B8G8R8A8_UNORM, 100, 60);
	struct nv30_miptree *mt = nv30_miptree(pt);
	CHECK(!mt->swizzled, "a 100x60 texture is swizzled");
	struct pipe_box sub = BOX(3, 5, 50, 20);
	upload(pt, &sub, pattern, 4);
	uint8_t *vram = mt->base.bo->map;
	unsigned pitch = mt->level[0].pitch;
	int bad = 0;
	CHECK(vram, "the texture was never written");
	if (!vram)
		return;
	for (int y = 0; y < 60; y++)
		for (int x = 0; x < 100; x++) {
			uint32_t got, want = (x >= 3 && x < 53 && y >= 5 && y < 25) ? pattern(x - 3, y - 5) : 0;
			memcpy(&got, vram + y * pitch + x * 4, 4);
			bad += got != want;
		}
	CHECK(bad == 0, "%d linear texels wrong (pitch %u)", bad, pitch);
	pipe_resource_reference(&pt, NULL);
}

static void test_dxt1(void)
{
	/* SuperTuxKart's crash: a 512-wide DXT1 texture; 8 bytes a 4x4 block, rows of blocks */
	struct pipe_resource *pt = texture(PIPE_FORMAT_DXT1_RGB, 128, 64);
	struct nv30_miptree *mt = nv30_miptree(pt);
	struct pipe_box all = BOX(0, 0, 128, 64);
	struct pipe_transfer *tx = NULL;
	uint8_t *map = nv30_miptree_transfer_map(&ctx.base.pipe, pt, 0, PIPE_MAP_WRITE, &all, &tx);
	CHECK(map, "a DXT1 write map with a full GART failed");
	if (!map)
		return;
	for (int by = 0; by < 16; by++)
		for (int bx = 0; bx < 32; bx++)
			for (int i = 0; i < 8; i++)
				map[by * tx->stride + bx * 8 + i] = bx ^ (by << 3) ^ i;
	nv30_miptree_transfer_unmap(&ctx.base.pipe, tx);
	uint8_t *vram = mt->base.bo->map;
	unsigned pitch = mt->level[0].pitch;
	int bad = 0;
	for (int by = 0; by < 16; by++)
		for (int bx = 0; bx < 32; bx++)
			for (int i = 0; i < 8; i++)
				bad += vram[by * pitch + bx * 8 + i] != (uint8_t)(bx ^ (by << 3) ^ i);
	CHECK(bad == 0, "%d DXT1 bytes wrong", bad);
	pipe_resource_reference(&pt, NULL);
}

int main(void)
{
	screen.base.is_ps3 = true;
	screen.base.base.resource_destroy = nv30_miptree_destroy;
	ctx.base.pipe.screen = &screen.base.base;
	ctx.base.screen = &screen.base;
	ctx.screen = &screen;
	gart_full = 1;

	test_swizzled();
	test_linear();
	test_dxt1();
	CHECK(bos == 0, "%d buffer objects left", bos);
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
