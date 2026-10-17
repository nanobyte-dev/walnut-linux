/* synth.spu: libspeq's MIDI synthesis worker (speq_synth_*_v1, lib/synth.c). Links libspeq-spu.a
 * (main, the claim loop); one operation, SYNTH_OP_BLOCK (lib/synth-job.h): one block of a range
 * of voices, rendered with fluidsynth's DSP (lib/synth-kern.h) into the job's output lines.
 *
 * The voices come in at once (one DMA per 16 KB); then each voice's reads (its two sample
 * windows and its filters' sin/cos slices, the ranges its plan names) are fetched while the
 * voice before it renders: two sets of buffers, tags 0 and 1. The lines (zeroed here) and the
 * voices go back at the end. The interpolation tables stay in local store, fetched again only
 * when their EA changes. Every DMA is split at 16 KB and where the EA's high word changes.
 * Local store: voices 24 KB, lines 8 KB, reads 2 x 33 KB, tables 12 KB. */
#include "speq-spu.h"
#include "synth-kern.h"

#define MAXDMA		16384
#define WIN_BYTES	(2 * SYNTH_WIN_MAX + 32)	/* a window and its 16-byte rounding */
#define TAB_BYTES	(8 * SYNTH_TAB_MAX + 16)

struct reads {
	uint8_t win[2][WIN_BYTES] __attribute__((aligned(128)));
	uint8_t tab[2][TAB_BYTES] __attribute__((aligned(128)));
};

static SPEQ_LOCAL struct speq_synth_voice voices[SYNTH_JOB_MAX] __attribute__((aligned(128)));
static SPEQ_LOCAL double lines[SPEQ_SYNTH_LINES * SK_BUFSIZE] __attribute__((aligned(128)));
static SPEQ_LOCAL double dsp_buf[SK_BUFSIZE] __attribute__((aligned(128)));
static SPEQ_LOCAL struct reads rd[2];
static SPEQ_LOCAL double cubic[SYNTH_INTERP_ROWS * 4] __attribute__((aligned(128)));
static SPEQ_LOCAL double linear[SYNTH_INTERP_ROWS * 2] __attribute__((aligned(128)));
static SPEQ_LOCAL uint64_t cubic_ea, linear_ea;

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

/* The voice's reads into set r: its view of them */
static void fetch_voice(const struct speq_synth_voice *v, struct reads *r, unsigned tag,
			uint64_t sincos, struct sk_view *sv)
{
	const struct synth_plan *pl = (const struct synth_plan *)v->priv;
	int i;

	for (i = 0; i < 2; i++) {
		sv->w[i] = (const int16_t *)fetch(r->win[i], v->data + 2ull * pl->win_first[i],
						  2 * pl->win_n[i], tag);
		sv->first[i] = pl->win_first[i];
		sv->n[i] = pl->win_n[i];
		sv->t[i] = (const struct sk_sincos *)fetch(r->tab[i], sincos + 8ull * pl->tab_first[i],
							    8 * pl->tab_n[i], tag);
		sv->tfirst[i] = pl->tab_first[i];
		sv->tn[i] = pl->tab_n[i];
	}
}

static int plan_ok(const struct speq_synth_voice *v, unsigned nlines)
{
	const struct synth_plan *pl = (const struct synth_plan *)v->priv;
	int i;

	if (pl->skip)
		return 1;
	if (v->interp > SPEQ_SYNTH_CUBIC || (v->data & 1))
		return 0;
	for (i = 0; i < 2; i++)
		if (pl->win_n[i] > SYNTH_WIN_MAX || pl->tab_n[i] > SYNTH_TAB_MAX)
			return 0;
	for (i = 0; i < SPEQ_SYNTH_BUFS; i++)
		if (v->line[i] >= (int)nlines)
			return 0;
	return 1;
}

static int op_block(const struct speq_job *j)
{
	uint64_t vea = j->arg[SYNTH_ARG_VOICES], out = j->arg[SYNTH_ARG_OUT];
	uint64_t sincos = j->arg[SYNTH_ARG_SINCOS];
	uint32_t n = j->arg[SYNTH_ARG_COUNT], nlines = j->arg[SYNTH_ARG_LINES], vbytes, i, k;
	struct sk_view sv[2];
	int next, cur, err = 0;

	if (!n || n > SYNTH_JOB_MAX || n != j->arg[SYNTH_ARG_COUNT] || !nlines ||
	    nlines > SPEQ_SYNTH_LINES || (vea & 15) || (out & 15) || (j->arg[SYNTH_ARG_CUBIC] & 15) ||
	    (j->arg[SYNTH_ARG_LINEAR] & 15) || (sincos & 3) || !j->arg[SYNTH_ARG_SINCOS_N] ||
	    j->arg[SYNTH_ARG_SINCOS_N] > 0x7fffffff)
		return -22;
	vbytes = n * sizeof(struct speq_synth_voice);
	/* tags: 0/1 the reads, 2 voices and tables, 3 the output */
	dma(voices, vea, vbytes, 2, 0);
	if (cubic_ea != j->arg[SYNTH_ARG_CUBIC]) {
		cubic_ea = j->arg[SYNTH_ARG_CUBIC];
		dma(cubic, cubic_ea, sizeof(cubic), 2, 0);
	}
	if (linear_ea != j->arg[SYNTH_ARG_LINEAR]) {
		linear_ea = j->arg[SYNTH_ARG_LINEAR];
		dma(linear, linear_ea, sizeof(linear), 2, 0);
	}
	for (k = 0; k < nlines * SK_BUFSIZE; k++)
		lines[k] = 0;
	speq_wait_tags(1u << 2);
	for (i = 0; i < n; i++)
		if (!plan_ok(&voices[i], nlines)) {
			cubic_ea = linear_ea = 0;	/* not trusted either */
			return -22;
		}
	for (k = 0; k < 2; k++) {
		sv[k].cubic = cubic;
		sv[k].linear = linear;
		sv[k].tab_size = j->arg[SYNTH_ARG_SINCOS_N];
		sv[k].bad = 0;
	}

	/* the first voice to render, and the one after it while it renders */
	for (next = 0; next < (int)n && ((struct synth_plan *)voices[next].priv)->skip; next++)
		;
	if (next < (int)n)
		fetch_voice(&voices[next], &rd[0], 0, sincos, &sv[0]);
	k = 0;
	while (next < (int)n) {
		cur = next;
		for (next++; next < (int)n && ((struct synth_plan *)voices[next].priv)->skip; next++)
			;
		if (next < (int)n)
			fetch_voice(&voices[next], &rd[k ^ 1], k ^ 1, sincos, &sv[k ^ 1]);
		speq_wait_tags(1u << k);
		if (sk_voice(&voices[cur], &sv[k], dsp_buf, lines, nlines) || sv[k].bad) {
			err = sv[k].bad ? -14 : -5;	/* SK_CHECK builds: a read outside the plan */
			break;
		}
		k ^= 1;
	}
	speq_wait_tags(3);	/* no read still landing in local store */
	if (err)
		return err;
	dma(lines, out, nlines * SK_BUFSIZE * sizeof(double), 3, 1);
	dma(voices, vea, vbytes, 3, 1);
	return 0;	/* the runtime waits for the puts before completing the job */
}

const speq_op_fn speq_ops[SYNTH_NOPS] = {
	[SYNTH_OP_BLOCK] = op_block,
};
const unsigned speq_nops = SYNTH_NOPS;
