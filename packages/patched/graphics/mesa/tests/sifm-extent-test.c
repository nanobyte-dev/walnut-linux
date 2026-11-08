/* sifm-extent-test: Mesa patch 0075 (on the PS3 a SIFM copy's source fits the extent ps3rsx checks).
 * nv30_miptree.c and nv30_transfer.c are compiled in whole against a fake device (buffers in malloc
 * memory at addresses of their own, sizes page-rounded as ps3rsx's) and a fake push buffer that keeps
 * the words. Every scaled-image (SIFM) copy in the stream is checked against a model of ps3rsx's
 * rule (validate.c, sifm_check), written here from the kernel's code and fed only the words: the
 * larger of the declared IMAGE_IN_SIZE and the sampled texels, at the declared pitch, inside one
 * buffer.
 *   uploads   texture uploads into a swizzled texture (the staging buffer copied by SIFM): the
 *             session 6 SuperTuxKart case (33x21 RGBA in a 4 KiB staging buffer: 4168 bytes counted),
 *             the other pool-class edges (33x85, 34x85, 33x341), quakespasm's 256x11 (slack 0 in a
 *             page-rounded buffer), etr's 26x31 (24 bytes of slack), and a sweep of odd and even
 *             boxes, with the staging pool and without it; each fits, goes by SIFM, and the guard never
 *             fires
 *   model     nv30_sifm_src_bytes (nv30_sifm_extent.h) equals the kernel model on the words Mesa
 *             sends, for random rects, both filters, scaled and not
 *   guard     a crafted source whose buffer ends inside the last declared row: no SIFM is sent, the
 *             guard counts it, the 3D blit copies it instead
 *   copy      resource_copy_region from a linear 1024x3 texture (12 KiB, page-exact) into a swizzled
 *             one: the miptree's row of padding keeps it on SIFM
 *   stk       the copy session 6's SuperTuxKart push dump has ps3rsx refuse (Mesa 1498: chan1-s2.push,
 *             submit 28, a 35x21 glyph at (219, 154) of a 256x256 swizzled page from a 4 KiB staging
 *             buffer): its recorded words through the kernel model are refused (4176 bytes counted in
 *             4096); the same upload here sends the same words but OFFSET, from a buffer that holds them
 * Exit 0 when every check passed. (host-tests.sh sifm-extent; tools/run-tests.sh --only mesa-sifm-extent) */
#include "nv30/nv30_miptree.c"
#include "nv30/nv30_transfer.c"

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

/* --- formats: B8G8R8A8 only ------------------------------------------------------------------- */
static const struct util_format_description desc_bgra = {
	.format = PIPE_FORMAT_B8G8R8A8_UNORM, .name = "PIPE_FORMAT_B8G8R8A8_UNORM",
	.block = { 1, 1, 1, 32 }, .layout = UTIL_FORMAT_LAYOUT_PLAIN, .nr_channels = 4,
};
const struct util_format_description *util_format_description(enum pipe_format format)
{
	(void)format;
	return &desc_bgra;
}
bool util_format_is_pure_integer(enum pipe_format format) { (void)format; return false; }

/* --- the device: buffers in malloc memory, each at an address of its own ---------------------- */
#define FIFO_VRAM 0xfeed0000u	/* the DMA handles the kernel knows (validate.c H_VRAM, H_GART) */
#define FIFO_GART 0xfeed0001u
static struct nouveau_bo *live[4096];
static unsigned nlive;
static uint64_t next_addr = 0x100000;
int nouveau_bo_new(struct nouveau_device *dev, uint32_t flags, uint32_t align, uint64_t size,
		   union nouveau_bo_config *config, struct nouveau_bo **pbo)
{
	(void)dev; (void)align; (void)config;
	uint64_t sz = (size + 4095) & ~4095ull;		/* ps3rsx's PAGE_ALIGN (mem.c) */
	struct nouveau_bo *bo = calloc(1, sizeof(*bo) + sz);

	bo->size = sz;
	bo->flags = flags;
	bo->handle = 1;		/* refcount stand-in */
	bo->offset = next_addr;
	next_addr += sz + (1 << 20);	/* a gap: no extent runs into the next buffer */
	if (next_addr >> 32)
		abort();
	live[nlive++] = bo;
	*pbo = bo;
	return 0;
}
void nouveau_bo_ref(struct nouveau_bo *bo, struct nouveau_bo **ref)
{
	if (bo)
		bo->handle++;
	if (*ref && --(*ref)->handle == 0) {
		for (unsigned i = 0; i < nlive; i++)
			if (live[i] == *ref) {
				live[i] = live[--nlive];
				break;
			}
		if ((*ref)->offset + (*ref)->size + (1 << 20) == next_addr)
			next_addr = (*ref)->offset;	/* the last one: its addresses again (32-bit relocs) */
		free(*ref);
	}
	*ref = bo;
}
int nouveau_bo_map(struct nouveau_bo *bo, uint32_t access, struct nouveau_client *client)
{
	(void)access; (void)client;
	bo->map = (char *)(bo + 1);
	return 0;
}
/* fence work waits until the stream is decoded (a staging buffer lives until its copy has run) */
static struct { void (*func)(void *); void *data; } pending[64];
static unsigned npending;
bool nouveau_fence_work(struct nouveau_fence *f, void (*func)(void *), void *data)
{
	(void)f;
	if (npending == ARRAY_SIZE(pending))
		abort();
	pending[npending].func = func;
	pending[npending++].data = data;
	return true;
}
static void signal_fences(void)
{
	for (unsigned i = 0; i < npending; i++)
		pending[i].func(pending[i].data);
	npending = 0;
}
bool nouveau_fence_wait_idle(struct nouveau_context *nv) { (void)nv; signal_fences(); return true; }
void nouveau_fence_unref_bo(void *data) { struct nouveau_bo *bo = data; nouveau_bo_ref(NULL, &bo); }
int64_t os_time_get_nano(void) { return 0; }
void nv30_state_lost(struct nv30_context *nv30) { (void)nv30; }
void mesa_log(enum mesa_log_level l, const char *tag, const char *f, ...) { (void)l; (void)tag; (void)f; }
void _simple_mtx_plain_init_once(simple_mtx_t *m) { (void)m; }
void util_call_once_data_slow(once_flag *once, util_call_once_data_func func, const void *val)
{ (void)once; func(val); }

/* --- the push buffer: the words kept ---------------------------------------------------------- */
static uint32_t pushmem[1 << 20];
int nouveau_pushbuf_space(struct nouveau_pushbuf *push, uint32_t dwords, uint32_t relocs, uint32_t pushes)
{
	(void)relocs; (void)pushes;
	if (!push->cur || push->cur + dwords > push->end) {
		push->cur = pushmem;
		push->end = pushmem + ARRAY_SIZE(pushmem);
	}
	return 0;
}
int nouveau_pushbuf_refn(struct nouveau_pushbuf *p, struct nouveau_pushbuf_refn *r, int n)
{ (void)p; (void)r; (void)n; return 0; }
void nouveau_pushbuf_reloc(struct nouveau_pushbuf *p, struct nouveau_bo *bo, uint32_t data, uint32_t flags,
			   uint32_t vor, uint32_t tor)
{
	if (flags & NOUVEAU_BO_OR)		/* a DMA handle: by the buffer's domain */
		*p->cur++ = (bo->flags & NOUVEAU_BO_VRAM) ? vor : tor;
	else
		*p->cur++ = (uint32_t)bo->offset + data;	/* the address the kernel patches in */
}
int nouveau_pushbuf_kick(struct nouveau_pushbuf *p) { (void)p; return 0; }
struct nouveau_bufctx *nouveau_pushbuf_bufctx(struct nouveau_pushbuf *p, struct nouveau_bufctx *c)
{ (void)p; (void)c; return NULL; }

/* --- ps3rsx's SIFM source rule, from validate.c (sifm_end, sifm_check, ext2), on the words ------ */
static uint32_t k_bpp(uint32_t c)
{
	switch (c) {
	case 1: case 2: case 5: case 6: case 7: case 0xa: case 0xb: return 2;
	case 3: case 4: case 0xc: case 0xd: return 4;
	case 8: case 9: return 1;
	default: return 0;
	}
}
static uint64_t k_end(uint32_t p, uint32_t d, uint32_t n, uint32_t infmt)
{
	uint64_t last = ((uint64_t)p << 16) + (uint64_t)d * (n - 1);

	if (((infmt >> 16) & 0xff) == 1)
		last += d / 2;
	return (last >> 20) + 1 + ((infmt >> 24) == 1);
}
/* the bytes from OFFSET the kernel counts for the copy the SIFM state s describes */
static uint64_t k_len(const uint32_t *s)
{
	uint32_t size = s[0x400 / 4], infmt = s[0x404 / 4], point = s[0x40c / 4], osz = s[0x314 / 4];
	uint64_t ue = k_end(point & 0xffff, s[0x318 / 4], osz & 0xffff, infmt);
	uint64_t ve = k_end(point >> 16, s[0x31c / 4], osz >> 16, infmt);
	uint64_t cols = (size & 0xffff) > ue ? (size & 0xffff) : ue;
	uint64_t rows = (size >> 16) > ve ? (size >> 16) : ve;

	return (uint64_t)(infmt & 0xffff) * (rows - 1) + cols * k_bpp(s[0x300 / 4]);
}

struct sifm_seen {
	unsigned n, over, blits;	/* SIFM copies, those past their buffer, 3D draws (the blit) */
	uint64_t min_slack, len;	/* the tightest copy's slack; the last copy's length */
	uint32_t last_size, last_out;
};

/* decode the words from start: each SIFM POINT (subchannel 5, 0x40c) checked against the model */
static struct sifm_seen decode(const uint32_t *w, const uint32_t *end)
{
	static uint32_t s[0x2000 / 4];
	struct sifm_seen r = { .min_slack = UINT64_MAX };

	while (w < end) {
		uint32_t h = *w++, n = (h >> 18) & 0x7ff, sub = (h >> 13) & 7, m = h & 0x1ffc;

		for (uint32_t i = 0; i < n && w < end; i++, w++) {
			uint32_t mm = (h & 0x40000000) ? m : m + 4 * i;

			if (sub == 7 && mm == 0x1808 && *w)
				r.blits++;
			if (sub != 5)
				continue;
			s[mm / 4] = *w;
			if (mm != 0x40c)
				continue;
			uint64_t len = k_len(s), off = s[0x408 / 4];
			struct nouveau_bo *in = NULL;

			for (unsigned b = 0; b < nlive; b++)
				if (off >= live[b]->offset && off < live[b]->offset + live[b]->size)
					in = live[b];
			r.n++;
			r.len = len;
			r.last_size = s[0x400 / 4];
			r.last_out = s[0x314 / 4];
			if (!in || off + len > in->offset + in->size) {
				r.over++;
				r.min_slack = 0;
			} else if (in->offset + in->size - off - len < r.min_slack) {
				r.min_slack = in->offset + in->size - off - len;
			}
		}
	}
	return r;
}

/* --- the context ------------------------------------------------------------------------------ */
static struct nv30_screen screen;
static struct nv30_context ctx;
static struct nouveau_object chan, eng3d, surf2d, swzsurf;
static struct nv04_fifo fifo = { .vram = FIFO_VRAM, .gart = FIFO_GART };
static struct nouveau_pushbuf_priv ppush;
static struct nouveau_pushbuf pushbuf;

static void setup(void)
{
	static struct nv04_resource fp_res;
	static struct nouveau_bo fp_bo;
	static struct nouveau_heap vp;

	static struct nouveau_device dev;

	screen.base.device = &dev;
	screen.base.is_ps3 = true;
	screen.base.base.resource_destroy = nv30_miptree_destroy;
	screen.staging_max = ARRAY_SIZE(screen.staging_pool);	/* the small pool, as on the PS3 */
	eng3d.oclass = NV40_3D_CLASS;
	screen.eng3d = &eng3d;
	surf2d.handle = 0x313371c3;
	swzsurf.handle = 0x31337a73;
	screen.surf2d = &surf2d;
	screen.swzsurf = &swzsurf;
	chan.data = &fifo;
	pushbuf.channel = &chan;
	ppush.screen = &screen.base;
	pushbuf.user_priv = &ppush;
	ctx.base.pushbuf = &pushbuf;
	ctx.base.pipe.screen = &screen.base.base;
	ctx.base.screen = &screen.base;
	ctx.screen = &screen;
	fp_bo.flags = NOUVEAU_BO_VRAM;
	fp_res.bo = &fp_bo;
	fp_res.domain = NOUVEAU_BO_VRAM;
	ctx.blit_fp = &fp_res.base;
	ctx.blit_fp_gen = screen.lost_gen;
	ctx.blit_vp = &vp;
}

static const uint32_t *mark(void)
{
	nouveau_pushbuf_space(&pushbuf, 0, 0, 0);
	return pushbuf.cur;
}

#define BOX(x_, y_, w_, h_) { .x = x_, .y = y_, .z = 0, .width = w_, .height = h_, .depth = 1 }

static struct pipe_resource *texture(unsigned w, unsigned h)
{
	struct pipe_resource t = { .target = PIPE_TEXTURE_2D, .format = PIPE_FORMAT_B8G8R8A8_UNORM, .width0 = w,
				   .height0 = h, .depth0 = 1, .array_size = 1, .bind = PIPE_BIND_SAMPLER_VIEW };
	return nv30_miptree_create(&screen.base.base, &t);
}

/* one write upload of box into level 0 of pt; the SIFM copies it made */
static struct sifm_seen upload(struct pipe_resource *pt, unsigned w, unsigned h)
{
	struct pipe_box box = BOX(0, 0, w, h);
	struct pipe_transfer *tx = NULL;
	const uint32_t *start = mark();
	void *map = nv30_miptree_transfer_map(&ctx.base.pipe, pt, 0, PIPE_MAP_WRITE, &box, &tx);

	CHECK(map && tx, "the %ux%u map failed", w, h);
	if (tx)
		nv30_miptree_transfer_unmap(&ctx.base.pipe, tx);
	struct sifm_seen r = decode(start, pushbuf.cur);
	signal_fences();
	return r;
}

static void test_uploads(void)
{
	struct pipe_resource *pt = texture(1024, 1024);
	static const struct { unsigned w, h; uint64_t want_slack; const char *what; } c[] = {
		{ 33, 21, UINT64_MAX, "session 6, SuperTuxKart: 33x21 RGBA, 4032 bytes in a 4 KiB class" },
		{ 33, 85, UINT64_MAX, "33x85: 16320 bytes in a 16 KiB class" },
		{ 34, 85, UINT64_MAX, "34x85" },
		{ 33, 341, UINT64_MAX, "33x341: 65472 bytes in a 64 KiB class" },
		{ 256, 11, UINT64_MAX, "quakespasm's 256x11 (slack 0 with page rounding)" },
		{ 26, 31, 24, "etr's 26x31 (24 bytes of slack)" },
	};
	struct nv30_miptree *mt = nv30_miptree(pt);

	CHECK(mt->swizzled, "a 1024x1024 texture is not swizzled (no SIFM upload)");
	for (unsigned i = 0; i < ARRAY_SIZE(c); i++) {
		uint32_t g = screen.sifm_guarded;
		struct sifm_seen r = upload(pt, c[i].w, c[i].h);

		CHECK(r.n == 1 && !r.over, "%s: %u SIFM copies, %u past their buffer (%llu bytes counted)", c[i].what,
		      r.n, r.over, (unsigned long long)r.len);
		CHECK(screen.sifm_guarded == g, "%s: the guard sent it another way (its staging buffer short)",
		      c[i].what);
		CHECK(r.last_size == (((c[i].h + 1) & ~1u) << 16 | ((c[i].w + 1) & ~1u)),
		      "%s: SIFM declared %#x", c[i].what, r.last_size);
		if (c[i].want_slack != UINT64_MAX && !r.over)
			CHECK(r.min_slack >= c[i].want_slack, "%s: slack %llu", c[i].what,
			      (unsigned long long)r.min_slack);
	}
	/* a sweep: every box up to 70x70 and the tall thin ones, odd and even */
	unsigned bad = 0, n = 0, guarded = screen.sifm_guarded;
	for (unsigned w = 2; w <= 70; w++)
		for (unsigned h = 2; h <= 70; h++) {
			struct sifm_seen r = upload(pt, w, h);

			n += r.n;
			if ((r.over || r.n != 1) && bad++ < 3)
				printf("  %ux%u: %u SIFM copies, %u past their buffer\n", w, h, r.n, r.over);
		}
	for (unsigned w = 2; w <= 1024; w += 31)
		for (unsigned h = 3; h <= 1023; h += 64) {
			struct sifm_seen r = upload(pt, w, h);

			n += r.n;
			if ((r.over || r.n != 1) && bad++ < 6)
				printf("  %ux%u: %u SIFM copies, %u past their buffer\n", w, h, r.n, r.over);
		}
	CHECK(!bad && screen.sifm_guarded == guarded, "uploads: %u boxes wrong of %u copies, %u guarded", bad, n,
	      screen.sifm_guarded - guarded);
	/* without the pool (NV30_STAGING_POOL_MB unset): each upload a buffer of its own, page-rounded */
	unsigned keep = screen.staging_max;
	screen.staging_max = 0;
	bad = n = 0;
	for (unsigned w = 2; w <= 1024; w += 17)
		for (unsigned h = 3; h <= 1023; h += 32) {
			struct sifm_seen r = upload(pt, w, h);

			n += r.n;
			if ((r.over || r.n != 1) && bad++ < 3)
				printf("  no pool, %ux%u: %u SIFM copies, %u past their buffer\n", w, h, r.n, r.over);
		}
	CHECK(!bad && screen.sifm_guarded == guarded, "uploads without the pool: %u boxes wrong of %u copies, "
	      "%u guarded", bad, n, screen.sifm_guarded - guarded);
	screen.staging_max = keep;
	pipe_resource_reference(&pt, NULL);
}

/* nv30_sifm_src_bytes against the kernel model on the words nv30_transfer_rect_sifm sends */
static uint32_t rng = 12345;
static uint32_t rnd(uint32_t n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % n; }

static void test_model(void)
{
	struct nouveau_bo *sbo, *dbo;
	unsigned bad = 0;

	nouveau_bo_new(NULL, NOUVEAU_BO_GART, 0, 16 << 20, NULL, &sbo);
	nouveau_bo_new(NULL, NOUVEAU_BO_VRAM, 0, 16 << 20, NULL, &dbo);
	for (unsigned i = 0; i < 20000; i++) {
		struct nv30_rect src = { .bo = sbo, .domain = NOUVEAU_BO_GART, .cpp = 4 };
		struct nv30_rect dst = { .bo = dbo, .domain = NOUVEAU_BO_VRAM, .cpp = 4 };
		bool nearest = rnd(2);

		src.w = 2 + rnd(1023);
		src.h = 2 + rnd(1023);
		src.pitch = (src.w * 4 + 63 + 64 * rnd(3)) & ~63u;
		src.x0 = rnd(src.w - 1);
		src.x1 = src.x0 + 1 + rnd(src.w - src.x0);
		src.y0 = rnd(src.h - 1);
		src.y1 = src.y0 + 1 + rnd(src.h - src.y0);
		src.d = dst.d = 1;
		dst.pitch = 4096;
		dst.w = dst.h = 1024;
		dst.x0 = rnd(64);
		dst.y0 = rnd(64);
		dst.x1 = dst.x0 + (rnd(2) ? src.x1 - src.x0 : 1 + rnd(512));
		dst.y1 = dst.y0 + (rnd(2) ? src.y1 - src.y0 : 1 + rnd(512));
		const uint32_t *start = mark();
		nv30_transfer_rect_sifm(&ctx, nearest ? NEAREST : BILINEAR, &src, &dst);
		struct sifm_seen r = decode(start, pushbuf.cur);
		uint64_t mine = nv30_sifm_src_bytes(&src, &dst, nearest);

		if ((r.n != 1 || r.len != mine) && bad++ < 4)
			printf("  %ux%u pitch %u [%u,%u)x[%u,%u) -> %ux%u %s: kernel %llu, nv30_sifm_src_bytes %llu\n",
			       src.w, src.h, src.pitch, src.x0, src.x1, src.y0, src.y1, dst.x1 - dst.x0,
			       dst.y1 - dst.y0, nearest ? "nearest" : "bilinear", (unsigned long long)r.len,
			       (unsigned long long)mine);
	}
	CHECK(!bad, "model: %u of 20000 rects where nv30_sifm_src_bytes isn't the kernel's count", bad);
	nouveau_bo_ref(NULL, &sbo);
	nouveau_bo_ref(NULL, &dbo);
}

/* a source nothing padded, its buffer ending inside the last declared row: not by SIFM */
static void test_guard(void)
{
	struct pipe_resource *pt = texture(64, 64);
	struct nv30_miptree *mt = nv30_miptree(pt);
	struct nouveau_bo *sbo;
	uint32_t g = screen.sifm_guarded;

	nouveau_bo_new(NULL, NOUVEAU_BO_GART, 0, 64 * 4 * 63, NULL, &sbo);	/* 16128 -> 16384: 4 rows short of 64 */
	struct nv30_rect src = { .bo = sbo, .domain = NOUVEAU_BO_GART, .offset = 16384 - 256 * 63, .pitch = 256,
				 .cpp = 4, .w = 64, .h = 63, .d = 1, .x0 = 0, .x1 = 64, .y0 = 0, .y1 = 63 };
	struct nv30_rect dst;

	define_rect(pt, 0, 0, 0, 0, 64, 63, &dst);
	const uint32_t *start = mark();
	nv30_transfer_rect(&ctx, NEAREST, &src, &dst);
	struct sifm_seen r = decode(start, pushbuf.cur);

	CHECK(mt->swizzled && r.n == 0, "guard: %u SIFM copies of a source whose buffer ends inside its last "
	      "declared row (%u past it)", r.n, r.over);
	CHECK(screen.sifm_guarded == g + 1, "guard: counted %u, want 1", screen.sifm_guarded - g);
	CHECK(r.blits >= 1, "guard: the copy wasn't made by the 3D blit instead");
	/* the same source with one row more of buffer: by SIFM */
	src.offset -= 256;
	start = mark();
	nv30_transfer_rect(&ctx, NEAREST, &src, &dst);
	r = decode(start, pushbuf.cur);
	CHECK(r.n == 1 && !r.over && screen.sifm_guarded == g + 1, "guard: a source that fits not by SIFM (%u, %u)",
	      r.n, r.over);
	nouveau_bo_ref(NULL, &sbo);
	pipe_resource_reference(&pt, NULL);
}

/* resource_copy_region from a linear texture's only level, odd-height and page-exact */
static void test_copy(void)
{
	struct pipe_resource *lin = texture(1024, 3), *swz = texture(1024, 4);
	struct pipe_box box = BOX(0, 0, 1024, 3);
	uint32_t g = screen.sifm_guarded;

	CHECK(!nv30_miptree(lin)->swizzled && nv30_miptree(swz)->swizzled, "copy: the textures' layouts");
	const uint32_t *start = mark();
	nv30_resource_copy_region(&ctx.base.pipe, swz, 0, 0, 0, 0, lin, 0, &box);
	struct sifm_seen r = decode(start, pushbuf.cur);

	CHECK(r.n == 1 && !r.over && screen.sifm_guarded == g, "copy: a 1024x3 level (12 KiB) into a swizzled "
	      "texture: %u SIFM copies, %u past their buffer, %u guarded", r.n, r.over, screen.sifm_guarded - g);
	pipe_resource_reference(&lin, NULL);
	pipe_resource_reference(&swz, NULL);
}

/* the SIFM words of the refused copy in session 6's SuperTuxKart dump (lane A, 0 gaps): COLOR_FORMAT
 * A8R8G8B8, SRCCOPY, CLIP and OUT at (219, 154) 35x21, steps 1.0, SIZE 36x22, pitch 192 with the
 * centre origin and point sampling, POINT 0; its source the first bytes of a 0x1000-byte GART buffer */
static const struct { uint32_t m, v; } stk_words[] = {
	{ 0x300, 0x00000003 }, { 0x304, 0x00000003 }, { 0x308, 0x009a00db }, { 0x30c, 0x00150023 },
	{ 0x310, 0x009a00db }, { 0x314, 0x00150023 }, { 0x318, 0x00100000 }, { 0x31c, 0x00100000 },
	{ 0x400, 0x00160024 }, { 0x404, 0x000100c0 }, { 0x408, 0x010ae000 }, { 0x40c, 0x00000000 },
};

static void test_stk(void)
{
	static uint32_t st[0x2000 / 4];
	struct pipe_resource *pt = texture(256, 256);
	struct pipe_box box = BOX(219, 154, 35, 21);
	struct pipe_transfer *tx = NULL;

	for (unsigned i = 0; i < ARRAY_SIZE(stk_words); i++)
		st[stk_words[i].m / 4] = stk_words[i].v;
	CHECK(k_len(st) == 4176, "stk: the model counts %llu bytes for the recorded copy, the kernel 4176",
	      (unsigned long long)k_len(st));
	/* the same upload: the same words, OFFSET aside, and the buffer now holds them */
	const uint32_t *start = mark(), *w;
	void *map = nv30_miptree_transfer_map(&ctx.base.pipe, pt, 0, PIPE_MAP_WRITE, &box, &tx);
	CHECK(map && tx, "stk: the map failed");
	if (tx)
		nv30_miptree_transfer_unmap(&ctx.base.pipe, tx);
	struct sifm_seen r = decode(start, pushbuf.cur);
	unsigned diff = 0;

	memset(st, 0, sizeof(st));
	for (w = start; w < pushbuf.cur;) {	/* the SIFM words as sent */
		uint32_t h = *w++, n = (h >> 18) & 0x7ff, sub = (h >> 13) & 7, m = h & 0x1ffc;

		for (uint32_t i = 0; i < n && w < pushbuf.cur; i++, w++)
			if (sub == 5)
				st[(m + 4 * i) / 4] = *w;
	}
	for (unsigned i = 0; i < ARRAY_SIZE(stk_words); i++)
		if (stk_words[i].m != 0x408 && st[stk_words[i].m / 4] != stk_words[i].v && diff++ < 3)
			printf("  method %#x: %#010x, the dump %#010x\n", stk_words[i].m, st[stk_words[i].m / 4],
			       stk_words[i].v);
	CHECK(r.n == 1 && !diff, "stk: the upload sent %u SIFM copies, %u words unlike the dump's", r.n, diff);
	CHECK(!r.over && r.len == 4176, "stk: the copy (%llu bytes counted) past its buffer",
	      (unsigned long long)r.len);
	signal_fences();
	pipe_resource_reference(&pt, NULL);
}

int main(void)
{
	setup();
	test_uploads();
	test_model();
	test_guard();
	test_copy();
	test_stk();
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
