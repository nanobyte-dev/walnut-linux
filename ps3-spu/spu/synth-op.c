/* synth.spu: libspeq's MIDI synthesis worker (speq_synth_*_v2, lib/synth.c). Links libspeq-spu.a
 * (main, the claim loop); one operation, SYNTH_OP_ROUND (lib/synth-job.h): every block of a
 * range of voices, rendered with fluidsynth's DSP in single precision (lib/synth-kern.h) into
 * the job's output, the filters' histories carried from block to block.
 *
 * The entries (up to 128, 32 KB) and the voices' histories come in at once; then each voice
 * block's reads (its two sample windows and its filters' sin/cos slices) are fetched two entries
 * ahead, in three buffer sets (tags 0-2), while the entries before render. The output (zeroed
 * here) and the new histories go back at the end. The coefficient tables stay in local store,
 * fetched again only when their EA changes. Every DMA is split at 16 KB and where the EA's high
 * word changes. Local store: entries 32 KB, output 16 KB, reads 3 x 16.5 KB, ext 16.5 KB,
 * tables 6 KB. */
#include "speq-spu.h"
#include "synth-kern.h"

#define MAXDMA		16384
#define WIN_BYTES	(2 * SYNTH_WIN_MAX + 48)	/* a window, its 16-byte rounding, the
							 * conversion's read past it */
#define TAB_BYTES	(8 * SYNTH_TAB_MAX + 16)
#define NSET		3

struct reads {
	uint8_t win[2][WIN_BYTES] __attribute__((aligned(128)));
	uint8_t tab[2][TAB_BYTES] __attribute__((aligned(128)));
};

static SPEQ_LOCAL struct synth_ent ents[SYNTH_JOB_ENTS] __attribute__((aligned(128)));
static SPEQ_LOCAL float out[SYNTH_OUT_MAX * SK_BUF] __attribute__((aligned(128)));
static SPEQ_LOCAL float hist[SYNTH_JOB_ENTS * 4] __attribute__((aligned(128)));
static SPEQ_LOCAL float buf[SK_BUF] __attribute__((aligned(128)));
static SPEQ_LOCAL float ext[2][SYNTH_WIN_MAX + 16] __attribute__((aligned(128)));
static SPEQ_LOCAL struct reads rd[NSET];
static SPEQ_LOCAL v4f cubic[SYNTH_INTERP_ROWS] __attribute__((aligned(128)));
static SPEQ_LOCAL float linear[SYNTH_INTERP_ROWS * 2] __attribute__((aligned(128)));
static SPEQ_LOCAL uint64_t cubic_ea, linear_ea;
static SPEQ_LOCAL uint16_t todo[SYNTH_JOB_ENTS];

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

/* Starts fetching bytes at ea (any alignment) into ls; returns where they land */
static const uint8_t *fetch(uint8_t *ls, uint64_t ea, uint32_t bytes, unsigned tag)
{
	uint64_t lo = ea & ~15ull, hi = (ea + bytes + 15) & ~15ull;

	if (bytes)
		dma(ls, lo, hi - lo, tag, 0);
	return ls + (ea - lo);
}

/* An entry's reads into set k */
static void fetch_ent(const struct synth_ent *e, int k, uint64_t sincos, struct sk_reads *r)
{
	int i;

	for (i = 0; i < 2; i++) {
		int32_t lo = e->elo[i] < 0 ? 0 : e->elo[i];
		int32_t n = e->en[i] - (lo - e->elo[i]);

		r->win[i] = (const int16_t *)fetch(rd[k].win[i], e->data + 2ull * lo,
						   n > 0 ? 2 * n : 0, k);
		r->tab[i] = (const struct sk_sincos *)fetch(rd[k].tab[i], sincos + 8ull * e->tab_first[i],
							     8 * e->tab_n[i], k);
		r->ext[i] = ext[i];
	}
}

static int ent_ok(const struct synth_ent *e, unsigned nlines)
{
	int i;

	if (e->interp > SPEQ_SYNTH_CUBIC || (e->data & 1) || e->count > SK_BUF)
		return 0;
	for (i = 0; i < 2; i++)
		if (e->en[i] < 0 || e->en[i] > SYNTH_WIN_MAX || e->tab_n[i] > SYNTH_TAB_MAX)
			return 0;
	for (i = 0; i < SPEQ_SYNTH_BUFS; i++)
		if (e->line[i] >= (int)nlines)
			return 0;
	return 1;
}

static int op_round(const struct speq_job *j)
{
	uint64_t eea = j->arg[SYNTH_ARG_ENTS], oea = j->arg[SYNTH_ARG_OUT];
	uint64_t hin = j->arg[SYNTH_ARG_HIST], hout = j->arg[SYNTH_ARG_HIST_OUT];
	uint64_t sincos = j->arg[SYNTH_ARG_SINCOS];
	uint32_t nv = j->arg[SYNTH_ARG_VOICES], K = j->arg[SYNTH_ARG_BLOCKS];
	uint32_t nlines = j->arg[SYNTH_ARG_LINES], ne, i, n = 0, k;
	struct sk_reads r[NSET];
	struct sk_tabs t;
	int err = 0;

	if (!nv || !K || nv != j->arg[SYNTH_ARG_VOICES] || K != j->arg[SYNTH_ARG_BLOCKS] ||
	    (uint64_t)nv * K > SYNTH_JOB_ENTS || !nlines || nlines > SPEQ_SYNTH_LINES ||
	    K * nlines > SYNTH_OUT_MAX || (eea & 15) || (oea & 15) || (hin & 15) || (hout & 15) ||
	    (j->arg[SYNTH_ARG_CUBIC] & 15) || (j->arg[SYNTH_ARG_LINEAR] & 15) || (sincos & 3) ||
	    !j->arg[SYNTH_ARG_SINCOS_N] || j->arg[SYNTH_ARG_SINCOS_N] > 0x7fffffff)
		return -22;
	ne = nv * K;
	/* tags: 0-2 the reads, 3 entries, histories and tables, 4 the output */
	dma(ents, eea, ne * sizeof(struct synth_ent), 3, 0);
	dma(hist, hin, nv * 4 * sizeof(float), 3, 0);
	if (cubic_ea != j->arg[SYNTH_ARG_CUBIC]) {
		cubic_ea = j->arg[SYNTH_ARG_CUBIC];
		dma(cubic, cubic_ea, sizeof(cubic), 3, 0);
	}
	if (linear_ea != j->arg[SYNTH_ARG_LINEAR]) {
		linear_ea = j->arg[SYNTH_ARG_LINEAR];
		dma(linear, linear_ea, sizeof(linear), 3, 0);
	}
	for (i = 0; i < K * nlines * SK_BUF / 4; i++)
		((v4f *)out)[i] = (v4f){ 0, 0, 0, 0 };
	speq_wait_tags(1u << 3);
	for (i = 0; i < ne; i++) {
		if (ents[i].flags & (SYNTH_ENT_SKIP | SYNTH_ENT_PPE))
			continue;
		if (!ent_ok(&ents[i], nlines)) {
			cubic_ea = linear_ea = 0;	/* not trusted either */
			return -22;
		}
		todo[n++] = i;
	}
	t.cubic = cubic;
	t.linear = linear;
	t.sincos_n = j->arg[SYNTH_ARG_SINCOS_N];

	/* reads two entries ahead */
	for (k = 0; k < 2 && k < n; k++)
		fetch_ent(&ents[todo[k]], k, sincos, &r[k]);
	for (k = 0; k < n; k++) {
		const struct synth_ent *e = &ents[todo[k]];
		unsigned v = todo[k] / K, b = todo[k] % K, s = k % NSET;

		if (k + 2 < n)
			fetch_ent(&ents[todo[k + 2]], (k + 2) % NSET, sincos, &r[(k + 2) % NSET]);
		speq_wait_tags(1u << s);
		if (sk_entry(e, &r[s], hist + 4 * v, buf, out + b * nlines * SK_BUF, &t)) {
			err = -5;
			break;
		}
	}
	speq_wait_tags(7);	/* no read still landing in local store */
	if (err)
		return err;
	dma(out, oea, K * nlines * SK_BUF * sizeof(float), 4, 1);
	dma(hist, hout, nv * 4 * sizeof(float), 4, 1);
	return 0;	/* the runtime waits for the puts before completing the job */
}

const speq_op_fn speq_ops[SYNTH_NOPS] = {
	[SYNTH_OP_ROUND] = op_round,
};
const unsigned speq_nops = SYNTH_NOPS;
