/* synth-test: speq_synth_*_v1 on the SPU path (SPEQ_FAKE=1: synth.spu's op on host threads,
 * with every sample and table read checked against the plan's DMA'd ranges, SK_CHECK) against
 * libspeq's PPE path (the same DSP code reading memory directly). Random voices over synthetic
 * samples: the three interpolations, loops of 2 samples and up, wraps, the end of a sample,
 * filter sweeps (per-sample coefficients from the sin/cos slices), the custom filter, ramps,
 * unmapped buffers; several blocks in a row so the state carries. Same code, same compiler:
 * every result must be bit-identical with one job per block; split into small jobs the lines
 * are summed in another order (1e-12). Then refusals, and timeouts with the back-off. */
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "speq-int.h"
#include "synth-kern.h"

#define NSAMP	(1 << 18)
#define TAB_N	12001
#define LINES	4

static int failures;
static int16_t *data;
static struct sk_sincos sctab[TAB_N];
static double cubic[256 * 4] __attribute__((aligned(16)));
static double linear[256 * 2] __attribute__((aligned(16)));
static unsigned rng = 12345;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	fflush(stdout);
	if (!ok)
		failures++;
}

static unsigned rnd(unsigned n)
{
	rng = rng * 1103515245 + 12345;
	return n ? (rng >> 8) % n : 0;
}

static double frnd(double a, double b)
{
	return a + (b - a) * (rnd(1u << 20) / (double)(1u << 20));
}

/* fluidsynth's tables: gentables' formulas, fluid_iir_filter_init_table at 44.1 kHz */
static void tables(void)
{
	int i;

	for (i = 0; i < 256; i++) {
		double x = (double)i / 256;

		cubic[i * 4 + 0] = (x * (-0.5 + x * (1 - 0.5 * x)));
		cubic[i * 4 + 1] = (1.0 + x * x * (1.5 * x - 2.5));
		cubic[i * 4 + 2] = (x * (0.5 + x * (2.0 - 1.5 * x)));
		cubic[i * 4 + 3] = (0.5 * x * x * (x - 1.0));
		linear[i * 2 + 0] = 1.0 - x;
		linear[i * 2 + 1] = x;
	}
	for (i = 0; i < TAB_N; i++) {
		float period = (float)(2.0 * M_PI / 44100), fres = (float)(8.176 * pow(2.0, (1500 + i) / 1200.0));

		sctab[i].sin = sinf(period * fres);
		sctab[i].cos = cosf(period * fres);
	}
	data = malloc(NSAMP * sizeof(*data));
	for (i = 0; i < NSAMP; i++)
		data[i] = (int16_t)(12000 * sin(i * 0.031) + 6000 * sin(i * 0.37) + (int)rnd(2000) - 1000);
}

static void rand_filter(struct speq_synth_filter *f, int res)
{
	float q;

	memset(f, 0, sizeof(*f));
	f->type = res ? SPEQ_SYNTH_IIR_LOWPASS : rnd(3) ? SPEQ_SYNTH_IIR_OFF :
		  rnd(2) ? SPEQ_SYNTH_IIR_LOWPASS : SPEQ_SYNTH_IIR_HIGHPASS;
	f->flags = rnd(2) ? SPEQ_SYNTH_IIR_NO_GAIN : 0;
	f->last_fres = frnd(-900, 14000);
	q = frnd(0.0005, 12);
	f->last_q = q;
	f->hist1 = frnd(-1e6, 1e6);
	f->hist2 = frnd(-1e6, 1e6);
	{
		/* plausible coefficients: the ones fluidsynth would have */
		struct sk_view sv;

		memset(&sv, 0, sizeof(sv));
		sv.t[0] = sctab;
		sv.tn[0] = TAB_N;
		sv.tab_size = TAB_N;
		sk_coeffs(&sv, 0, (float)f->last_fres, q > 0.001f ? q : 0.001f, 1, 0, &f->a1, &f->a2,
			  &f->b02, &f->b1);
	}
	if (rnd(3) == 0) {
		f->fres_incr_count = 1 + rnd(320);
		f->fres_incr = frnd(-8, 8);
	}
	if (rnd(4) == 0) {
		f->q_incr_count = 1 + rnd(64);
		f->q_incr = frnd(-0.2, 0.2);
	}
	if (res) {
		f->amp = frnd(0, 2e-5);
		f->amp_incr = frnd(-1e-7, 1e-7);
	}
}

static void rand_voice(struct speq_synth_voice *v)
{
	uint32_t len, start, end, ls, le, idx;
	int i;

	memset(v, 0, sizeof(*v));
	v->data = (uintptr_t)data;
	len = rnd(8) == 0 ? 2 + rnd(30) : 50 + rnd(60000);
	start = 100 + rnd(NSAMP - 200 - len);
	end = start + len;
	v->start = start;
	v->end = end;
	v->interp = rnd(3);
	v->looping = rnd(3) != 0;
	if (v->looping || rnd(2)) {
		ls = start + rnd(len - 1);
		le = ls + 2 + rnd(end - ls - 1);	/* <= end + 1: fluidsynth's max_index_loop */
		if (rnd(6) == 0)
			le = ls + 2;
		v->loopstart = ls;
		v->loopend = le;
	} else {
		v->looping = 0;
	}
	v->has_looped = v->loopend && rnd(2);	/* also a released voice that looped before */
	if (v->looping)
		idx = v->has_looped ? v->loopstart + rnd(v->loopend - v->loopstart) :
				      start + rnd(v->loopend - start);
	else
		idx = rnd(5) == 0 ? end - rnd(40) : start + rnd(len);
	v->phase = (uint64_t)idx << 32 | (rnd(1u << 16) << 16 | rnd(1u << 16));
	v->phase_incr = rnd(20) == 0 ? frnd(20, 60) : rnd(5) == 0 ? frnd(0.001, 0.2) : frnd(0.2, 4);
	for (i = 0; i < SPEQ_SYNTH_BUFS; i++) {
		v->line[i] = rnd(6) == 0 ? -1 : (int8_t)rnd(LINES);
		v->cur_amp[i] = rnd(5) == 0 ? 0 : frnd(0, 1);
		v->tgt_amp[i] = rnd(5) == 0 ? 0 : rnd(3) ? v->cur_amp[i] : frnd(0, 1);
	}
	rand_filter(&v->custom, 0);
	rand_filter(&v->res, 1);
}

/* The state fields the hook copies back, plus count */
static int same_voice(const struct speq_synth_voice *a, const struct speq_synth_voice *b)
{
	return a->count == b->count && a->phase == b->phase && a->has_looped == b->has_looped &&
	       !memcmp(a->cur_amp, b->cur_amp, sizeof(a->cur_amp)) &&
	       !memcmp(&a->custom, &b->custom, sizeof(a->custom)) &&
	       !memcmp(&a->res, &b->res, sizeof(a->res));
}

/* One block through a synth: 0 and the lines, or the error */
static int run(struct speq_synth *s, struct speq_synth_voice *v, unsigned n, double *out, int *refused)
{
	struct speq_synth_block *b = speq_synth_begin_v1(s);
	unsigned i;
	int ret;

	*refused = 0;
	memset(out, 0, LINES * 64 * sizeof(double));
	if (!b)
		return -EAGAIN;
	memcpy(b->voice, v, n * sizeof(*v));
	b->sincos = sctab;
	b->sincos_n = TAB_N;
	ret = speq_synth_submit_v1(s, b, n);
	if (ret)
		return ret;
	ret = speq_synth_wait_v1(s, b);
	if (ret)
		return ret;
	for (i = 0; i < n; i++)
		if (b->voice[i].count == SPEQ_SYNTH_PPE)
			(*refused)++;
	memcpy(v, b->voice, n * sizeof(*v));
	if (b->out)
		memcpy(out, b->out, LINES * 64 * sizeof(double));
	return 0;
}

/* Carry each voice into its next block the way the hook would (fluidsynth's control part is
 * not here: the amps ramp on, the phase stays) */
static void carry(struct speq_synth_voice *v, unsigned n)
{
	unsigned i, k;

	for (i = 0; i < n; i++) {
		for (k = 0; k < SPEQ_SYNTH_BUFS; k++)
			if (rnd(4) == 0)
				v[i].tgt_amp[k] = frnd(0, 1);
		v[i].res.amp_incr = frnd(-1e-7, 1e-7);
	}
}

static int compare_rounds(struct speq_synth *ppe, struct speq_synth *spu, int rounds,
			  int exact, const char *what)
{
	static struct speq_synth_voice a[100], b[100];
	static double la[LINES * 64], lb[LINES * 64];
	int r, blk, bad = 0, ra, rb, ret, nvoices = 0;
	unsigned n, i;
	double maxrel = 0;

	for (r = 0; r < rounds && !bad; r++) {
		n = 1 + rnd(exact ? SYNTH_JOB_MAX : 100);	/* exact: one job */
		for (i = 0; i < n; i++)
			rand_voice(&a[i]);
		memcpy(b, a, sizeof(a[0]) * n);
		for (blk = 0; blk < 4 && !bad; blk++) {
			ret = run(ppe, a, n, la, &ra);
			if (ret) {
				printf("  ppe path failed: %d\n", ret);
				bad++;
				break;
			}
			ret = run(spu, b, n, lb, &rb);
			if (ret) {
				printf("  spu path failed: %d (round %d block %d)\n", ret, r, blk);
				bad++;
				break;
			}
			/* voices the SPU path refused: the hook renders them on the PPE, here the
			 * ppe path's result stands in */
			for (i = 0; i < n; i++)
				if (b[i].count == SPEQ_SYNTH_PPE) {
					b[i] = a[i];
				} else if (!same_voice(&a[i], &b[i])) {
					printf("  voice %u differs (round %d block %d): count %d/%d phase "
					       "%llx/%llx interp %d looping %d\n", i, r, blk, a[i].count,
					       b[i].count, (unsigned long long)a[i].phase,
					       (unsigned long long)b[i].phase, a[i].interp, a[i].looping);
					bad++;
					break;
				}
			nvoices += n - rb;
			if (bad)
				break;
			if (rb) {	/* the refused voices' share isn't in lb */
				carry(a, n);
				memcpy(b, a, sizeof(a[0]) * n);
				continue;
			}
			for (i = 0; i < LINES * 64; i++) {
				double d = fabs(la[i] - lb[i]), m = fabs(la[i]) > 1 ? fabs(la[i]) : 1;

				if (d / m > maxrel)
					maxrel = d / m;
			}
			if (exact ? memcmp(la, lb, sizeof(la)) != 0 : maxrel > 1e-12) {
				printf("  lines differ (round %d block %d): max relative %g\n", r, blk, maxrel);
				bad++;
			}
			carry(a, n);
			memcpy(b, a, sizeof(a[0]) * n);
		}
	}
	printf("  %d voice blocks compared, max relative difference %g\n", nvoices, maxrel);
	check(!bad, what);
	return bad;
}

int main(void)
{
	struct speq_synth_params p = {
		.abi = SPEQ_SYNTH_ABI, .voice_size = sizeof(struct speq_synth_voice),
		.lines = LINES, .max_voices = 100, .cubic = cubic, .linear = linear,
	};
	struct speq_synth *ppe, *spu, *s;
	static struct speq_synth_voice v[8];
	static double out[LINES * 64];
	int ret, refused, i, k;

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (!getenv("SPEQ_FAKE")) {
		fprintf(stderr, "synth-test: run with SPEQ_FAKE=1\n");
		return 2;
	}
	tables();
	check(sizeof(struct speq_synth_voice) == 384 && sizeof(struct synth_plan) <= 76,
	      "voice is 384 bytes, the plan fits in priv");
	p.abi = 99;
	check(speq_synth_open_v1(&s, &p) == -EPROTO && !s, "another ABI: -EPROTO");
	p.abi = SPEQ_SYNTH_ABI;
	spq_synth_tune(SPQ_SYNTH_OFF, -1, -1, -1);
	check(speq_synth_open_v1(&s, &p) == -ENOTSUP, "SPEQ_SYNTH=off: -ENOTSUP");

	spq_synth_tune(SPQ_SYNTH_PPE, -1, 64, -1);
	check(speq_synth_open_v1(&ppe, &p) == 0, "open, ppe");
	spq_synth_tune(SPQ_SYNTH_SPU, 10000000, 64, -1);
	check(speq_synth_open_v1(&spu, &p) == 0, "open, spu (fake), one job per block");
	if (!ppe || !spu)
		return 1;
	compare_rounds(ppe, spu, 300, 1, "spu = ppe, bit for bit (300 x 4 blocks of 1-64 voices, one job)");
	speq_synth_close_v1(spu);

	spq_synth_tune(SPQ_SYNTH_SPU, -1, 7, -1);
	check(speq_synth_open_v1(&spu, &p) == 0, "open, spu, 7 voices per job");
	compare_rounds(ppe, spu, 100, 0, "spu (jobs of 7) = ppe within 1e-12");

	/* refusals: a pitch whose block spans more than a window */
	for (i = 0; i < 8; i++) {
		rand_voice(&v[i]);
		v[i].looping = 0;
		v[i].phase = (uint64_t)v[i].start << 32;
	}
	v[3].phase_incr = 100;
	v[5].interp = 9;
	ret = run(spu, v, 8, out, &refused);
	check(!ret && refused == 2 && v[3].count == SPEQ_SYNTH_PPE && v[5].count == SPEQ_SYNTH_PPE &&
	      v[0].count >= 0, "a 6400-sample window and an unknown interpolation: SPEQ_SYNTH_PPE");

	/* auto: small blocks stay on the PPE */
	spq_synth_tune(SPQ_SYNTH_AUTO, -1, -1, 4);
	for (i = 0; i < 3; i++)
		rand_voice(&v[i]);
	check(run(spu, v, 3, out, &refused) == -ENOTSUP, "auto: 3 voices, -ENOTSUP (the PPE's)");
	for (i = 0; i < 8; i++)
		rand_voice(&v[i]);
	check(run(spu, v, 8, out, &refused) == 0, "auto: 8 voices, the SPEs");

	/* timeouts: a block given up on is lost to its job; three in a row: the back-off */
	spq_synth_tune(SPQ_SYNTH_SPU, 0, -1, -1);
	for (k = 0; k < 3; k++) {
		for (i = 0; i < 8; i++)
			rand_voice(&v[i]);
		ret = run(spu, v, 8, out, &refused);
		if (ret != -ETIMEDOUT)
			break;
	}
	check(k == 3, "timeout 0: -ETIMEDOUT, three times");
	check(speq_synth_begin_v1(spu) == NULL, "... then the SPEs are put aside (begin: NULL)");
	speq_synth_close_v1(spu);	/* waits for the lost blocks' jobs */
	speq_synth_close_v1(ppe);
	printf("%s: %d failure%s\n", failures ? "FAIL" : "ok", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
