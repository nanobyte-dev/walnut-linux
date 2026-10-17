/* skin.spu: libspeq's vertex skinning worker (speq_skin_*_v1, lib/skin.c). Links libspeq-spu.a
 * (main, the claim loop); one operation, SKIN_OP_RANGE (lib/skin-job.h): a range of one mesh's
 * vertices, skinned by lib/skin-kern.h (the PPE path's code) into the batch's output.
 *
 * The descriptor and the palette come in first (up to 48 KB, kept for the job); then the
 * vertices in chunks of up to 8 KB per stream (the output's template too), fetched one chunk
 * ahead into two buffer sets while the chunk before is skinned, each chunk's output put back as
 * it is done. The output's first and last quadwords may hold a neighbour's bytes (another job,
 * another mesh): those are put as 4- and 8-byte DMAs of our bytes only. Every DMA is split at
 * 16 KB and where the EA's high word changes. Local store: palette 48 KB, streams 2 x 2 x 8 KB,
 * output 2 x 8 KB. */
#include "speq-spu.h"
#include "skin-kern.h"

#define MAXDMA		16384
#define NSET		2
#define BUF		(SKIN_CHUNK + 32)	/* a chunk and its 16-byte rounding */
#define TAG_PUT		2			/* + set; fetches use tag = set */
#define TAG_IN		4			/* descriptor, palette */

static SPEQ_LOCAL struct skin_desc desc __attribute__((aligned(128)));
static SPEQ_LOCAL sv4f pal[SKIN_PAL_BYTES / 16] __attribute__((aligned(128)));
static SPEQ_LOCAL uint8_t sbuf[NSET][2][BUF] __attribute__((aligned(128)));
static SPEQ_LOCAL uint8_t obuf[NSET][BUF] __attribute__((aligned(128)));

/* size bytes, both sides 16-aligned and size a multiple of 16 */
static void dma(volatile void *lsp, uint64_t ea, uint32_t size, unsigned tag, int put)
{
	volatile uint8_t *ls = lsp;

	while (size) {
		uint64_t to4g = 0x100000000ull - (ea & 0xffffffffull);
		uint32_t c = size > MAXDMA ? MAXDMA : size;

		if (c > to4g)
			c = to4g;
		if (put)
			speq_put(ls, ea, c, tag);
		else
			speq_get(ls, ea, c, tag);
		ls += c;
		ea += c;
		size -= c;
	}
}

/* Starts fetching bytes at ea (4-aligned) into ls; returns where they land */
static uint8_t *fetch(uint8_t *ls, uint64_t ea, uint32_t bytes, unsigned tag)
{
	uint64_t lo = ea & ~15ull, hi = (ea + bytes + 15) & ~15ull;

	if (bytes)
		dma(ls, lo, hi - lo, tag, 0);
	return ls + (ea - lo);
}

/* Puts exactly bytes (a multiple of 4) from ls to ea (ls and ea alike modulo 16): the partial
 * quadwords at both ends in 4- and 8-byte pieces */
static void put_exact(const uint8_t *ls, uint64_t ea, uint32_t bytes, unsigned tag)
{
	uint32_t c;

	while (bytes && (ea & 15)) {
		c = (ea & 7) || bytes < 8 ? 4 : 8;
		speq_put((volatile void *)ls, ea, c, tag);
		ls += c;
		ea += c;
		bytes -= c;
	}
	c = bytes & ~15u;
	if (c)
		dma((volatile void *)ls, ea, c, tag, 1);
	ls += c;
	ea += c;
	bytes -= c;
	while (bytes) {
		c = bytes >= 8 ? 8 : 4;
		speq_put((volatile void *)ls, ea, c, tag);
		ls += c;
		ea += c;
		bytes -= c;
	}
}

struct chunk {
	const uint8_t *s[2];
	uint8_t *o;
	uint32_t v, n;
};

static void fetch_chunk(struct chunk *c, unsigned set, unsigned streams)
{
	uint32_t ds = desc.dst_stride, s;

	for (s = 0; s < 2; s++)
		c->s[s] = streams & (1u << s) ?
			  fetch(sbuf[set][s], desc.src[s] + (uint64_t)c->v * desc.src_stride[s],
				c->n * desc.src_stride[s], set) : sbuf[set][s];
	c->o = obuf[set] + ((desc.dst + (uint64_t)c->v * ds) & 15);
	if (desc.tmpl)
		fetch(obuf[set], desc.tmpl + (uint64_t)c->v * ds, c->n * ds, set);
}

static int op_range(const struct speq_job *j)
{
	uint64_t dea = j->arg[SKIN_ARG_DESC];
	uint32_t first = j->arg[SKIN_ARG_FIRST], count = j->arg[SKIN_ARG_COUNT];
	uint32_t ebytes, npal, streams, maxs, C, nch, k, end;
	struct chunk ch[NSET];
	struct sk_pal pl;
	int err = 0;

	if (!dea || (dea & 127) || first != j->arg[SKIN_ARG_FIRST] || count != j->arg[SKIN_ARG_COUNT])
		return -22;
	dma(&desc, dea, sizeof(desc), TAG_IN, 0);
	speq_wait_tags(1u << TAG_IN);
	if (sk_check(&desc) || first > desc.nverts || count > desc.nverts - first)
		return -22;
	if (!count)
		return 0;
	ebytes = desc.palette_fmt == SPEQ_SKIN_DQ ? 32 : 48;
	npal = desc.npal[0] + desc.npal[1];
	dma(pal, desc.pal[0], desc.npal[0] * ebytes, TAG_IN, 0);
	dma((uint8_t *)pal + desc.npal[0] * ebytes, desc.pal[1], desc.npal[1] * ebytes, TAG_IN, 0);
	pl.p0 = pl.p1 = pal;
	pl.n0 = pl.n = npal;

	streams = sk_streams(&desc);
	maxs = desc.dst_stride;
	for (k = 0; k < 2; k++)
		if ((streams & (1u << k)) && desc.src_stride[k] > maxs)
			maxs = desc.src_stride[k];
	C = SKIN_CHUNK / maxs;
	nch = (count + C - 1) / C;
	end = first + count;

	ch[0].v = first;
	ch[0].n = count < C ? count : C;
	fetch_chunk(&ch[0], 0, streams);
	speq_wait_tags(1u << TAG_IN);
	for (k = 0; k < nch; k++) {
		unsigned s = k & 1, t = s ^ 1;
		struct chunk *c = &ch[s];

		if (k + 1 < nch) {
			/* set t's last output (chunk k - 1) is out before its buffers fill again */
			speq_wait_tags(1u << (TAG_PUT + t));
			ch[t].v = c->v + c->n;
			ch[t].n = end - ch[t].v < C ? end - ch[t].v : C;
			fetch_chunk(&ch[t], t, streams);
		}
		speq_wait_tags(1u << s);
		if (!desc.tmpl) {
			sv4f *q = (sv4f *)obuf[s];
			uint32_t i, nq = ((c->o - obuf[s]) + c->n * desc.dst_stride + 15) >> 4;

			for (i = 0; i < nq; i++)
				q[i] = (sv4f){ 0, 0, 0, 0 };
		}
		err = sk_run(&desc, &pl, c->s[0], c->s[1], c->o, c->n);
		if (err)
			break;
		put_exact(c->o, desc.dst + (uint64_t)c->v * desc.dst_stride, c->n * desc.dst_stride,
			  TAG_PUT + s);
	}
	speq_wait_tags(3);	/* no fetch still landing in local store */
	return err;	/* the runtime waits for the puts before completing the job */
}

const speq_op_fn speq_ops[SKIN_NOPS] = {
	[SKIN_OP_RANGE] = op_range,
};
const unsigned speq_nops = SKIN_NOPS;
