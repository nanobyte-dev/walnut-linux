/* blit-state-test: Mesa patch 0058 (nv30_transfer_rect_blit sets the window state it draws with).
 * nv30_transfer.c is compiled in whole against a fake push buffer that keeps the words; the blit
 * is run for a destination of each kind and the stream is decoded to the last value written to each
 * method:
 *   - window offset 0 and window clip (w-1, h-1) at 0x2b8..0x2c4, COORD_CONVENTIONS = h, the zeta
 *     pitch/offset/DMA equal to the colour target's (NV40 class), whatever the context drew before
 *     (a first blit to another size, so the state differs) and in GART as well as VRAM;
 *   - the dirty bits the next draw needs (NV30_NEW_FRAMEBUFFER, NV30_NEW_FRAGPROG).
 * With "dump" it prints the stream as "method value" lines, for the golden diff host-tests.sh makes
 * against the file as it was before the patch: only the words 0058 adds may differ.
 *   cc ... blit-state-test.c   (tools/run-tests.sh --only mesa-blit-state; skipped without build/work/mesa) */
#include "nv30/nv30_transfer.c"

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
void nv30_state_lost(struct nv30_context *nv30) { (void)nv30; }
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

static struct nv30_screen screen;
static struct nouveau_object eng3d;
static struct nv04_fifo fifo = { .vram = 0xbeef0001, .gart = 0xbeef0002 };
static struct nouveau_object chan;
static struct nouveau_pushbuf_priv ppush;

static struct nv30_context *new_context(void)
{
	static struct nv04_resource fp_res;
	static struct nouveau_bo fp_bo;
	static struct nouveau_heap vp;
	struct nv30_context *nv30 = calloc(1, sizeof(*nv30));
	struct nouveau_pushbuf *push = calloc(1, sizeof(*push));

	chan.data = &fifo;
	push->channel = &chan;
	ppush.screen = &screen.base;
	push->user_priv = &ppush;
	eng3d.oclass = NV40_3D_CLASS;
	screen.eng3d = &eng3d;
	screen.base.is_ps3 = true;
	nv30->screen = &screen;
	nv30->base.pushbuf = push;
	nv30->base.pipe.screen = &screen.base.base;
	fp_bo.flags = NOUVEAU_BO_VRAM;
	fp_res.bo = &fp_bo;
	fp_res.domain = NOUVEAU_BO_VRAM;
	nv30->blit_fp = &fp_res.base;
	nv30->blit_fp_gen = screen.lost_gen;
	nv30->blit_vp = &vp;
	return nv30;
}

static struct nouveau_bo *bo(uint32_t flags, uint64_t addr)
{
	struct nouveau_bo *b = calloc(1, sizeof(*b));
	b->flags = flags;
	b->offset = addr;
	return b;
}

/* one blit, returns the words it wrote */
static void run_blit(struct nv30_context *nv30, struct nv30_rect *src, struct nv30_rect *dst)
{
	struct nouveau_pushbuf *push = nv30->base.pushbuf;

	push->cur = push->end = NULL;
	nouveau_pushbuf_space(push, 0, 0, 0);
	const uint32_t *start = push->cur;
	nv30_transfer_rect_blit(nv30, NEAREST, src, dst);
	decode(start, push->cur);
	if (dump)
		for (unsigned i = 0; i < nseq; i++)
			printf("%04x %08x\n", seq[i].m, seq[i].v);
}

static void rect(struct nv30_rect *r, struct nouveau_bo *b, unsigned domain, unsigned w, unsigned h,
		 unsigned pitch, unsigned cpp, unsigned offset)
{
	memset(r, 0, sizeof(*r));
	r->bo = b;
	r->domain = domain;
	r->offset = offset;
	r->pitch = pitch;
	r->cpp = cpp;
	r->w = w; r->h = h; r->d = 1;
	r->x0 = 0; r->y0 = 0; r->x1 = w; r->y1 = h;
}

int main(int argc, char **argv)
{
	struct nv30_context *nv30 = new_context();
	struct nv30_rect src, dst;
	struct nouveau_bo *sb = bo(NOUVEAU_BO_VRAM, 0x01000000), *db = bo(NOUVEAU_BO_VRAM, 0x02000000),
			  *gb = bo(NOUVEAU_BO_GART, 0x03000000);

	dump = argc > 1 && !strcmp(argv[1], "dump");
	if (dump) {
		/* the golden stream: swizzled 256x128, linear 100x60, GART linear 64x64 cpp 2, in this order */
		rect(&src, sb, NOUVEAU_BO_VRAM, 64, 32, 0, 4, 0);
		rect(&dst, db, NOUVEAU_BO_VRAM, 256, 128, 0, 4, 0);
		run_blit(nv30, &src, &dst);
		rect(&dst, db, NOUVEAU_BO_VRAM, 100, 60, 512, 4, 0x1000);
		run_blit(nv30, &src, &dst);
		rect(&dst, gb, NOUVEAU_BO_GART, 64, 64, 256, 2, 0x40);
		run_blit(nv30, &src, &dst);
		return 0;
	}

	/* a draw before it left other window state: pretend (the blit must not depend on it) */
	nv30->dirty = 0;
	rect(&src, sb, NOUVEAU_BO_VRAM, 64, 32, 0, 4, 0);
	rect(&dst, db, NOUVEAU_BO_VRAM, 256, 128, 0, 4, 0);
	run_blit(nv30, &src, &dst);
	CHECK(R(0x2b8) == 0, "window offset %08x, want 0", R(0x2b8));
	CHECK(R(0x2c0) == ((255u << 16)) && R(0x2c4) == ((127u << 16)),
	      "window clip %08x %08x, want (w-1, h-1) = 00ff0000 007f0000", R(0x2c0), R(0x2c4));
	CHECK(R(0x1d88) == 128, "COORD_CONVENTIONS %08x, want the height 128", R(0x1d88));
	CHECK(R(0x214) == R(0x210) && R(0x214) != 0, "ZETA_OFFSET %08x is not COLOR0_OFFSET %08x", R(0x214), R(0x210));
	CHECK(R(0x22c) == R(0x20c) && R(0x22c) == 64, "ZETA_PITCH %08x, COLOR0_PITCH %08x, want 64 (swizzled)", R(0x22c), R(0x20c));
	CHECK(R(0x198) == fifo.vram && R(0x194) == fifo.vram, "DMA_COLOR0/ZETA %08x %08x after the blit, want VRAM", R(0x194), R(0x198));
	CHECK(nv30->dirty & NV30_NEW_FRAMEBUFFER, "NV30_NEW_FRAMEBUFFER not marked");
	CHECK(nv30->dirty & NV30_NEW_FRAGPROG, "NV30_NEW_FRAGPROG not marked");
	CHECK(nwrites[0x2b8 / 4] == 1 && nwrites[0x1d88 / 4] == 1, "window words written %u / %u times", nwrites[0x2b8 / 4], nwrites[0x1d88 / 4]);

	/* another size, linear, an offset, in the same context: the window follows the destination */
	rect(&dst, db, NOUVEAU_BO_VRAM, 100, 60, 512, 4, 0x1000);
	run_blit(nv30, &src, &dst);
	CHECK(R(0x2c0) == (99u << 16) && R(0x2c4) == (59u << 16), "linear 100x60: clip %08x %08x", R(0x2c0), R(0x2c4));
	CHECK(R(0x1d88) == 60, "linear 100x60: COORD %08x", R(0x1d88));
	CHECK(R(0x22c) == 512 && R(0x20c) == 512, "linear: pitches %08x %08x, want 512", R(0x22c), R(0x20c));
	CHECK(R(0x214) == R(0x210), "linear: zeta offset %08x, colour %08x", R(0x214), R(0x210));

	/* GART destination: the zeta DMA follows the colour DMA during the blit, VRAM again after */
	rect(&dst, gb, NOUVEAU_BO_GART, 64, 64, 256, 2, 0x40);
	run_blit(nv30, &src, &dst);
	unsigned dma_gart = 0;
	for (unsigned i = 0; i + 1 < nseq; i++)
		if (seq[i].m == 0x194 && seq[i + 1].m == 0x198 && seq[i].v == fifo.gart && seq[i + 1].v == fifo.gart)
			dma_gart++;
	CHECK(dma_gart == 1, "GART destination: DMA_COLOR0 and DMA_ZETA both GART %u times, want 1", dma_gart);
	CHECK(R(0x194) == fifo.vram && R(0x198) == fifo.vram, "after the GART blit the DMAs are VRAM again");
	CHECK(R(0x2c0) == (63u << 16) && R(0x1d88) == 64, "GART 64x64: clip %08x, COORD %08x", R(0x2c0), R(0x1d88));

	/* NV30 class: the zeta pitch is in the colour pitch word's upper half */
	eng3d.oclass = NV30_3D_CLASS;
	rect(&dst, db, NOUVEAU_BO_VRAM, 100, 60, 512, 4, 0);
	run_blit(nv30, &src, &dst);
	CHECK(R(0x20c) == ((512u << 16) | 512u), "NV30 class: pitch word %08x", R(0x20c));
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
