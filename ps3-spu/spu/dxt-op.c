/* dxt.spu: libspeq's DXT1 worker. Links libspeq-spu.a (main, the claim loop) and dxt.c (the
 * encoder); one operation, DXT_OP_ROWS (lib/dxt-job.h): a range of block rows of a tightly
 * packed RGBA8 image from main memory to DXT1 blocks in main memory.
 *
 * A job is cut into units, double-buffered: the source of unit n+1 is fetched while unit n is
 * encoded, and unit n-1's output is still being put.
 * - Up to DXT_CHUNK_W pixels wide, a unit is k whole block rows (k * 16 * width <= 16 KB),
 *   one contiguous range of the source: fetched from the 16-byte boundary below its start up
 *   to the boundary above its end (the source is usually 8 mod 16: ioq3's hunk allocator), and
 *   the encoder shifts. Rounding stays inside the pages that hold the image.
 * - Wider, a unit is one block row's column strip of up to DXT_CHUNK_W pixels: its four
 *   pixel rows are fetched one by one into rows RS bytes apart (4 * width is a multiple of 16,
 *   so all four have the same misalignment, and so does every strip).
 * - Output: each block row (strip) goes to dst + stride * row (+ 2 * x0), 8-aligned: an 8-byte
 *   head when the EA is 8 mod 16, the 16-byte body, an 8-byte tail; the encoder writes each row
 *   at the local-store offset that matches its EA's low 4 bits. Nothing past the row is
 *   written. Packed rows (stride == 2 * width) go out as one run per unit.
 * - Every DMA is split at 16 KB and where the EA's high word changes.
 * Local store: 2 x 16.1 KB source, 2 x 8 KB output. */
#include "dxt-job.h"
#include "dxt.h"
#include "speq-spu.h"

#if DXT_JOB_RGB != DXT1_MODE_RGB || DXT_JOB_RGBA != DXT1_MODE_RGBA
#error "dxt-job.h and dxt.h disagree on the modes"
#endif

#define CW	DXT_CHUNK_W
#define RS	(4 * CW + 16)			/* a strip's pixel rows in local store */
#define SBUF	(4 * RS + 32)			/* >= 16 KB + 16 (contiguous) + 16 read past */
#define OBUF	8192				/* k rows of roundup16(2w) + 16: at most 8 KB */
#define MAXDMA	16384

static SPEQ_LOCAL uint8_t sbuf[2][SBUF] __attribute__((aligned(128)));
static SPEQ_LOCAL uint8_t obuf[2][OBUF] __attribute__((aligned(128)));

struct unit {
	uint32_t row, rows;	/* block rows */
	uint32_t x0, cw;	/* column strip, pixels */
};

struct jobp {
	uint64_t src, dst;
	uint32_t w, row0, rows, stride, mode;
	uint32_t k;		/* contiguous: block rows per unit; 0: strips */
	uint32_t nch;		/* strips per block row */
	uint32_t units;
	int packed;		/* contiguous and stride == 2 * w: a unit's rows are one run */
};

/* size bytes, both sides 16-aligned and size a multiple of 16 (or one small transfer) */
static void dma(uint8_t *ls, uint64_t ea, uint32_t size, unsigned tag, int put)
{
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

/* n bytes (a multiple of 8) at ls to ea, ls & 15 == ea & 15, both 8-aligned */
static void put_run(uint8_t *ls, uint64_t ea, uint32_t n, unsigned tag)
{
	uint32_t body;

	if (ea & 8) {
		speq_put(ls, ea, 8, tag);
		ls += 8;
		ea += 8;
		n -= 8;
	}
	body = n & ~15u;
	if (body) {
		dma(ls, ea, body, tag, 1);
		ls += body;
		ea += body;
		n -= body;
	}
	if (n)
		speq_put(ls, ea, 8, tag);
}

static void unit_at(const struct jobp *p, uint32_t u, struct unit *un)
{
	if (p->k) {
		un->row = p->row0 + u * p->k;
		un->rows = p->row0 + p->rows - un->row;
		if (un->rows > p->k)
			un->rows = p->k;
		un->x0 = 0;
		un->cw = p->w;
	} else {
		un->row = p->row0 + u / p->nch;
		un->rows = 1;
		un->x0 = u % p->nch * CW;
		un->cw = p->w - un->x0 < CW ? p->w - un->x0 : CW;
	}
}

/* Starts the unit's source DMA into s; returns the pixels' offset in the first quadword */
static unsigned fetch(const struct jobp *p, const struct unit *un, uint8_t *s, unsigned tag)
{
	uint64_t ea = p->src + (uint64_t)16 * p->w * un->row + 4 * un->x0;
	unsigned off = ea & 15, r;

	if (p->k) {
		dma(s, ea - off, (off + 16 * p->w * un->rows + 15) & ~15u, tag, 0);
		return off;
	}
	for (r = 0; r < 4; r++, ea += 4 * p->w)
		dma(s + r * RS, ea - off, (off + 4 * un->cw + 15) & ~15u, tag, 0);
	return off;
}

/* Encodes the unit from s into o and starts its puts */
static void encode_put(const struct jobp *p, const struct unit *un, const uint8_t *s,
		       unsigned off, uint8_t *o, unsigned tag)
{
	uint32_t rowbytes = 2 * un->cw, slot = ((rowbytes + 15) & ~15u) + 16, i;
	uint64_t ea = p->dst + (uint64_t)p->stride * un->row + 2 * un->x0;

	if (p->packed) {
		uint8_t *d = o + (ea & 15);

		dxt1_encode_rows_stride(s, off, un->cw, 4 * p->w, un->rows, d, p->mode);
		put_run(d, ea, rowbytes * un->rows, tag);
		return;
	}
	for (i = 0; i < un->rows; i++, ea += p->stride) {
		uint8_t *d = o + i * slot + (ea & 15);

		if (p->k)
			dxt1_encode_rows_stride(s + 16 * p->w * i, off, un->cw, 4 * p->w, 1, d, p->mode);
		else
			dxt1_encode_rows_stride(s, off, un->cw, RS, 1, d, p->mode);
		put_run(d, ea, rowbytes, tag);
	}
}

static int op_rows(const struct speq_job *j)
{
	struct jobp p;
	struct unit cur, next = { 0, 0, 0, 0 };
	unsigned off, noff = 0;
	uint32_t u;

	p.src = j->arg[DXT_ARG_SRC];
	p.dst = j->arg[DXT_ARG_DST];
	p.w = j->arg[DXT_ARG_WIDTH];
	p.row0 = j->arg[DXT_ARG_ROW0];
	p.rows = j->arg[DXT_ARG_ROWS];
	p.stride = j->arg[DXT_ARG_STRIDE];
	p.mode = j->arg[DXT_ARG_MODE];
	if (!p.w || p.w % 4 || p.w != j->arg[DXT_ARG_WIDTH] || !p.rows ||
	    p.rows != j->arg[DXT_ARG_ROWS] || (p.dst & 7) || (p.stride & 7) ||
	    p.stride < 2 * p.w || p.stride != j->arg[DXT_ARG_STRIDE] ||
	    (p.mode != DXT1_MODE_RGB && p.mode != DXT1_MODE_RGBA))
		return -22;
	if (p.w <= CW) {
		p.k = MAXDMA / (16 * p.w);
		p.nch = 1;
		p.units = (p.rows + p.k - 1) / p.k;
		p.packed = p.stride == 2 * p.w;
	} else {
		p.k = 0;
		p.nch = (p.w + CW - 1) / CW;
		p.units = p.rows * p.nch;
		p.packed = 0;
	}

	/* tags: 0/1 the source buffers, 2/3 the output buffers */
	unit_at(&p, 0, &cur);
	off = fetch(&p, &cur, sbuf[0], 0);
	for (u = 0; u < p.units; u++) {
		unsigned b = u & 1;

		if (u + 1 < p.units) {
			unit_at(&p, u + 1, &next);
			noff = fetch(&p, &next, sbuf[b ^ 1], b ^ 1);
		}
		/* this unit's source, and the puts of unit u - 2 out of obuf[b] */
		speq_wait_tags(1u << b | 1u << (2 + b));
		encode_put(&p, &cur, sbuf[b], off, obuf[b], 2 + b);
		cur = next;
		off = noff;
	}
	return 0;	/* the runtime waits for the puts before completing the job */
}

const speq_op_fn speq_ops[DXT_NOPS] = {
	[DXT_OP_ROWS] = op_rows,
};
const unsigned speq_nops = DXT_NOPS;
