/* fb-window-test: Mesa patch 0060 (nv30_validate_fb's small-surface hack gets a window clip that stays inside
 * the block). nv30_state_validate.c is compiled in whole against a fake push buffer; nv30_validate_fb is run
 * for a 2x2 R5G6B5 swizzled level with an offset that is not 64-aligned (drawn as a 16x2 block from window
 * origin x = 8), for an ordinary swizzled target, and for a linear target with an unaligned offset, and the
 * stream is decoded:
 *   the hack: origin 8 and a clip of 8 columns (max 7), so origin + clip = 16, the block's width (before:
 *   clip max 15, origin + clip = 24); every other target keeps its clip (w - 1), the linear one included;
 * "dump" prints the stream (method value) for the golden diff in host-tests.sh against the file as it was
 * before 0060: only the hack's clip word differs.
 * (tools/run-tests.sh --only mesa-fb-window; skipped without build/work/mesa) */
#include "nv30/nv30_state_validate.c"

static int fails, checks, dump;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t pushmem[4096];
static uint32_t regs[0x2000 / 4];		/* last value written to each 3D method */
static unsigned nwrites[0x2000 / 4];
static struct { uint32_t m, v; } seq[4096];
static unsigned nseq;

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
	(void)flags; (void)vor; (void)tor;
	*p->cur++ = 0x10000000u | (uint32_t)bo->offset | data;	/* the address the kernel would patch in */
}
int nouveau_pushbuf_kick(struct nouveau_pushbuf *p) { (void)p; return 0; }
struct nouveau_bufctx *nouveau_pushbuf_bufctx(struct nouveau_pushbuf *p, struct nouveau_bufctx *c) { (void)p; (void)c; return NULL; }
int mtx_lock(mtx_t *m) { (void)m; return 0; }
int mtx_unlock(mtx_t *m) { (void)m; return 0; }
void _simple_mtx_plain_init_once(simple_mtx_t *m) { (void)m; }
void util_call_once_data_slow(once_flag *once, util_call_once_data_func func, const void *val) { (void)once; func(val); }
void mesa_log(enum mesa_log_level l, const char *tag, const char *f, ...) { (void)l; (void)tag; (void)f; }

/* the stream, decoded: increasing-method packets (BEGIN_NV04) of the 3D subchannel */
static void decode(const uint32_t *w, const uint32_t *end)
{
	memset(regs, 0, sizeof(regs));
	memset(nwrites, 0, sizeof(nwrites));
	nseq = 0;
	while (w < end) {
		uint32_t h = *w++, count = (h >> 18) & 0x7ff, m = h & 0x1ffc;
		for (uint32_t i = 0; i < count && w < end; i++, m += 4, w++) {
			if (m < 0x2000) {
				regs[m / 4] = *w;
				nwrites[m / 4]++;
			}
			if (nseq < ARRAY_SIZE(seq)) {
				seq[nseq].m = m;
				seq[nseq++].v = *w;
			}
		}
	}
}
#define R(m) regs[(m) / 4]

struct nouveau_bufref *nouveau_bufctx_mthd(struct nouveau_bufctx *ctx, int bin, uint32_t packet, struct nouveau_bo *bo,
					   uint64_t data, uint32_t flags, uint32_t vor, uint32_t tor)
{
	static struct nouveau_bufref ref;
	(void)ctx; (void)bin; (void)packet; (void)bo; (void)data; (void)flags; (void)vor; (void)tor;
	return &ref;
}
void nouveau_bufctx_reset(struct nouveau_bufctx *ctx, int bin) { (void)ctx; (void)bin; }
const struct nv30_format nv30_format_table[PIPE_FORMAT_COUNT];
static const struct util_format_description desc16 = {
	.block = { 1, 1, 1, 16 }, .layout = UTIL_FORMAT_LAYOUT_PLAIN, .nr_channels = 3,
};
const struct util_format_description *util_format_description(enum pipe_format f) { (void)f; return &desc16; }

static struct nv30_screen screen;
static struct nouveau_object eng3d;
static struct nv04_fifo fifo = { .vram = 1, .gart = 2 };
static struct nouveau_object chan;
static struct nouveau_pushbuf_priv ppush;

struct target { struct nv30_surface sf; struct nv30_miptree mt; struct nouveau_bo bo; };

/* one validate_fb for a colour target: dims w x h as the framebuffer, surface offset and kind */
static void run_fb(struct nv30_context *nv30, struct target *t, unsigned w, unsigned h, unsigned offset, bool swizzled)
{
	struct nouveau_pushbuf *push = nv30->base.pushbuf;

	memset(t, 0, sizeof(*t));
	t->bo.flags = NOUVEAU_BO_VRAM;
	t->mt.base.bo = &t->bo;
	t->sf.base.texture = &t->mt.base.base;
	t->sf.base.format = PIPE_FORMAT_B5G6R5_UNORM;
	t->sf.offset = offset;
	t->sf.pitch = swizzled ? 64 : 256;
	t->sf.swizzled = swizzled;
	t->sf.width = w; t->sf.height = h;
	nv30->fb_cbufs[0] = &t->sf.base;
	nv30->fb_zsbuf = NULL;
	nv30->framebuffer.nr_cbufs = 1;
	nv30->framebuffer.width = w;
	nv30->framebuffer.height = h;
	nv30->framebuffer.cbufs[0].texture = &t->mt.base.base;
	nv30->framebuffer.cbufs[0].format = PIPE_FORMAT_B5G6R5_UNORM;
	push->cur = push->end = NULL;
	nouveau_pushbuf_space(push, 0, 0, 0);
	const uint32_t *start = push->cur;
	nv30_validate_fb(nv30);
	decode(start, push->cur);
	if (dump)
		for (unsigned i = 0; i < nseq; i++)
			printf("%04x %08x\n", seq[i].m, seq[i].v);
}

int main(int argc, char **argv)
{
	struct nv30_context *nv30 = calloc(1, sizeof(*nv30));
	struct nouveau_pushbuf *push = calloc(1, sizeof(*push));
	struct target t;

	chan.data = &fifo;
	push->channel = (void *)&chan;
	ppush.screen = &screen.base;
	ppush.context = &nv30->base;
	push->user_priv = &ppush;
	eng3d.oclass = NV40_3D_CLASS;
	screen.eng3d = &eng3d;
	screen.base.is_ps3 = true;
	nv30->screen = &screen;
	nv30->base.pushbuf = push;
	nv30->base.pipe.screen = &screen.base.base;
	dump = argc > 1 && !strcmp(argv[1], "dump");

	/* a swizzled 2x2 whose offset is not 64-aligned: the small-surface hack (a 16x2 block, origin x = 8) */
	run_fb(nv30, &t, 2, 2, 0x20, true);
	if (!dump) {
		CHECK(R(0x2b8) == 8, "hack: window origin %08x, want x = 8", R(0x2b8));
		CHECK(((R(0x2c0) >> 16) & 0xffff) + 1 + (R(0x2b8) & 0xffff) <= 16, "hack: origin %u + clip %u columns reach past the 16-wide block",
		      R(0x2b8) & 0xffff, ((R(0x2c0) >> 16) & 0xffff) + 1);
		CHECK((R(0x2c0) >> 16) == 7, "hack: clip max %u, want 7", R(0x2c0) >> 16);
		CHECK(((R(0x200) >> 16) & 0xffff) == 16, "hack: RT_HORIZ %08x, want a 16-wide block", R(0x200));
	}
	/* an ordinary swizzled target: its clip is its width */
	run_fb(nv30, &t, 256, 128, 0x1000, true);
	if (!dump) {
		CHECK(R(0x2b8) == 0 && (R(0x2c0) >> 16) == 255 && (R(0x2c4) >> 16) == 127, "swizzled 256x128: origin %08x clip %08x %08x", R(0x2b8), R(0x2c0), R(0x2c4));
	}
	/* a swizzled 2x2 with an aligned offset: no hack */
	run_fb(nv30, &t, 2, 2, 0x40, true);
	if (!dump)
		CHECK(R(0x2b8) == 0 && (R(0x2c0) >> 16) == 1, "swizzled 2x2 aligned: origin %08x clip %08x", R(0x2b8), R(0x2c0));
	/* a linear target with an unaligned offset: unchanged (the hack's linear sibling) */
	run_fb(nv30, &t, 64, 32, 0x10, false);
	if (!dump)
		CHECK(R(0x2b8) == 8 && (R(0x2c0) >> 16) == 71, "linear, offset 16 bytes: origin %08x clip %08x (unchanged)", R(0x2b8), R(0x2c0));
	if (dump)
		return 0;
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
