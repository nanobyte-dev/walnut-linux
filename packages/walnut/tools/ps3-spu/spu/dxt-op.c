/* dxt.spu: libspeq's DXT1 and DXT5 worker. Links libspeq-spu.a (main, the claim loop) and dxt.c
 * (the encoder); one operation, DXT_OP_ROWS (lib/dxt-job.h): a range of block rows of a tightly
 * packed RGBA8 or RGB8 image from main memory to DXT1 or DXT5 blocks in main memory.
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
 * - Output: each block row (strip) goes to dst + stride * row (+ 2 * x0; DXT5 4 * x0),
 *   8-aligned: an 8-byte head when the EA is 8 mod 16, the 16-byte body, an 8-byte tail (DXT5:
 *   16-aligned, whole quadwords); the encoder writes each row at the local-store offset that
 *   matches its EA's low 4 bits. Nothing past the row is written. Packed rows (stride == a
 *   block row) go out as one run per unit.
 * - An RGB source (DXT_JOB_SRC_RGB, 3 bytes a pixel): the same units, 3/4 of the bytes. A
 *   contiguous unit is one range as above; a strip's four pixel rows (3 * width apart, not a
 *   multiple of 16) each get their own misalignment. Once a unit's source has landed,
 *   dxt_rgb_expand writes it as RGBA rows (alpha 255) into xbuf, 16-aligned and laid out as an
 *   RGBA unit would be (strip rows RS apart), and the encoder reads xbuf: its output is the
 *   bytes an RGBA source with alpha 255 gives, which is what the reference does with an RGB one.
 * - Every DMA is split at 16 KB and where the EA's high word changes.
 * Local store: 2 x 16.1 KB source, 2 x 8 KB output, 16.1 KB for the expanded RGB unit. */
#include "dxt-job.h"
#include "dxt.h"
#include "speq-spu.h"

#if DXT_JOB_RGB != DXT1_MODE_RGB || DXT_JOB_RGBA != DXT1_MODE_RGBA
#error "dxt-job.h and dxt.h disagree on the modes"
#endif

#define CW	DXT_CHUNK_W
#define RS	(4 * CW + 16)			/* a strip's pixel rows in local store */
#define SBUF	(4 * RS + 32)			/* >= 16 KB + 16 (contiguous) + 16 read past */
#define OBUF	8192				/* k rows of roundup16(2w) + 16 (DXT5 4w + 16): at
						 * most 8 KB */
#define MAXDMA	16384

static SPEQ_LOCAL uint8_t sbuf[2][SBUF] __attribute__((aligned(128)));
static SPEQ_LOCAL uint8_t obuf[2][OBUF] __attribute__((aligned(128)));
/* an RGB unit as RGBA: <= 16 KB contiguous or 4 rows RS apart, + dxt_rgb_expand's 48-byte
 * overrun and the encoder's 16-byte read past the end */
#define XBUF	(4 * RS + 64)
static SPEQ_LOCAL uint8_t xbuf[XBUF] __attribute__((aligned(128)));

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
	uint32_t bpp;		/* output bytes per 4 pixels of a block row: 2 (DXT1), 4 (DXT5) */
	uint32_t sbpp;		/* source bytes per pixel: 4, or 3 (DXT_JOB_SRC_RGB) */
	int packed;		/* contiguous and stride == a block row: a unit's rows are one run */
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

/* Starts the unit's source DMA into s; off[r]: pixel row r's offset in its first quadword
 * (contiguous: off[0] for the whole unit; strips of an RGBA source: the same for all four) */
static void fetch(const struct jobp *p, const struct unit *un, uint8_t *s, unsigned tag,
		  unsigned off[4])
{
	uint64_t ea = p->src + (uint64_t)4 * p->sbpp * p->w * un->row + p->sbpp * un->x0;
	unsigned r;

	if (p->k) {
		off[0] = ea & 15;
		dma(s, ea - off[0], (off[0] + 4 * p->sbpp * p->w * un->rows + 15) & ~15u, tag, 0);
		return;
	}
	for (r = 0; r < 4; r++, ea += p->sbpp * p->w) {
		off[r] = ea & 15;
		dma(s + r * RS, ea - off[r], (off[r] + p->sbpp * un->cw + 15) & ~15u, tag, 0);
	}
}

/* An RGB unit's source s (as fetched) to RGBA in xbuf, laid out as fetch puts an RGBA unit
 * with offset 0 */
static void expand(const struct jobp *p, const struct unit *un, const uint8_t *s,
		   const unsigned off[4])
{
	unsigned r;

	if (p->k) {
		dxt_rgb_expand(xbuf, s, off[0], 4 * p->w * un->rows);
		return;
	}
	for (r = 0; r < 4; r++)	/* in order: a row's overrun lands in the next, rewritten after */
		dxt_rgb_expand(xbuf + r * RS, s + r * RS, off[r], un->cw);
}

static void encode(const struct jobp *p, const uint8_t *s, unsigned off, unsigned cw,
		   unsigned sstride, unsigned rows, uint8_t *d)
{
	if (p->mode == DXT_JOB_DXT5)
		dxt5_encode_rows_stride(s, off, cw, sstride, rows, d);
	else
		dxt1_encode_rows_stride(s, off, cw, sstride, rows, d, p->mode);
}

/* Encodes the unit from s into o and starts its puts */
static void encode_put(const struct jobp *p, const struct unit *un, const uint8_t *s,
		       unsigned off, uint8_t *o, unsigned tag)
{
	uint32_t rowbytes = p->bpp * un->cw, slot = ((rowbytes + 15) & ~15u) + 16, i;
	uint64_t ea = p->dst + (uint64_t)p->stride * un->row + p->bpp * un->x0;

	if (p->packed) {
		uint8_t *d = o + (ea & 15);

		encode(p, s, off, un->cw, 4 * p->w, un->rows, d);
		put_run(d, ea, rowbytes * un->rows, tag);
		return;
	}
	for (i = 0; i < un->rows; i++, ea += p->stride) {
		uint8_t *d = o + i * slot + (ea & 15);

		if (p->k)
			encode(p, s + 16 * p->w * i, off, un->cw, 4 * p->w, 1, d);
		else
			encode(p, s, off, un->cw, RS, 1, d);
		put_run(d, ea, rowbytes, tag);
	}
}

static int op_rows(const struct speq_job *j)
{
	struct jobp p;
	struct unit cur, next = { 0, 0, 0, 0 };
	unsigned off[2][4];
	uint32_t u;

	p.src = j->arg[DXT_ARG_SRC];
	p.dst = j->arg[DXT_ARG_DST];
	p.w = j->arg[DXT_ARG_WIDTH];
	p.row0 = j->arg[DXT_ARG_ROW0];
	p.rows = j->arg[DXT_ARG_ROWS];
	p.stride = j->arg[DXT_ARG_STRIDE];
	p.mode = j->arg[DXT_ARG_MODE] & ~(uint64_t)DXT_JOB_SRC_RGB;
	p.sbpp = j->arg[DXT_ARG_MODE] & DXT_JOB_SRC_RGB ? 3 : 4;
	p.bpp = p.mode == DXT_JOB_DXT5 ? 4 : 2;
	if (!p.w || p.w % 4 || p.w != j->arg[DXT_ARG_WIDTH] || !p.rows ||
	    p.rows != j->arg[DXT_ARG_ROWS] || (p.dst & 7) || (p.stride & 7) ||
	    p.stride < p.bpp * p.w || p.stride != j->arg[DXT_ARG_STRIDE] ||
	    p.mode != (j->arg[DXT_ARG_MODE] & ~(uint64_t)DXT_JOB_SRC_RGB) ||
	    (p.mode != DXT1_MODE_RGB && p.mode != DXT1_MODE_RGBA && p.mode != DXT_JOB_DXT5) ||
	    (p.mode == DXT_JOB_DXT5 && ((p.dst | p.stride) & 15)))
		return -22;
	if (p.w <= CW) {
		p.k = MAXDMA / (16 * p.w);
		p.nch = 1;
		p.units = (p.rows + p.k - 1) / p.k;
		p.packed = p.stride == p.bpp * p.w;
	} else {
		p.k = 0;
		p.nch = (p.w + CW - 1) / CW;
		p.units = p.rows * p.nch;
		p.packed = 0;
	}

	/* tags: 0/1 the source buffers, 2/3 the output buffers */
	unit_at(&p, 0, &cur);
	fetch(&p, &cur, sbuf[0], 0, off[0]);
	for (u = 0; u < p.units; u++) {
		unsigned b = u & 1;

		if (u + 1 < p.units) {
			unit_at(&p, u + 1, &next);
			fetch(&p, &next, sbuf[b ^ 1], b ^ 1, off[b ^ 1]);
		}
		/* this unit's source, and the puts of unit u - 2 out of obuf[b] */
		speq_wait_tags(1u << b | 1u << (2 + b));
		if (p.sbpp == 3) {
			expand(&p, &cur, sbuf[b], off[b]);
			encode_put(&p, &cur, xbuf, 0, obuf[b], 2 + b);
		} else {
			encode_put(&p, &cur, sbuf[b], off[b][0], obuf[b], 2 + b);
		}
		cur = next;
	}
	return 0;	/* the runtime waits for the puts before completing the job */
}

SPEQ_PROGRAM(dxt, "dxt.spu", DXT_NOPS) = {
	[DXT_OP_ROWS] = op_rows,
};
