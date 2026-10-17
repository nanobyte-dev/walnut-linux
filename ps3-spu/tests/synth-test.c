/* synth-test: speq_synth_*_v2 on the SPU path (SPEQ_FAKE=1: deadline.spu's synth op on host threads) against
 * libspeq's PPE path (the same DSP code on this thread), driven the way fluidsynth's hook drives
 * them: rounds of up to 16 blocks over 1-100 voice slots, each voice block added after a
 * stand-in for fluidsynth's control part (gains, filter sweeps, envelope ramps change between
 * blocks), the next block from the state add hands back. Random voices over synthetic samples:
 * the three interpolations, loops of 2 samples and up, wraps, sample ends, pitches up to 60x
 * (windows too large for the SPEs: the PPE renders those voices), the custom filter. Same code,
 * same compiler: the voices' state and histories must be bit-identical, the outputs within 1e-6
 * of their peak (the SPU path sums each job's voices, then the jobs; the PPE path voice by
 * voice). Then the auto mode's minimum, timeouts (the round redone on the PPE) and the
 * back-off. */
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
#define MAXV	100

static int failures;
static int16_t *data;
static struct sk_sincos sctab[TAB_N];
static double cubic[256 * 4], linear[256 * 2];
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
	memset(f, 0, sizeof(*f));
	f->type = res ? SPEQ_SYNTH_IIR_LOWPASS : rnd(3) ? SPEQ_SYNTH_IIR_OFF :
		  rnd(2) ? SPEQ_SYNTH_IIR_LOWPASS : SPEQ_SYNTH_IIR_HIGHPASS;
	f->flags = rnd(2) ? SPEQ_SYNTH_IIR_NO_GAIN : 0;
	f->last_fres = frnd(1500, 13000);
	f->last_q = rnd(10) ? frnd(0.5, 6) : 0.0005;
	f->hist1 = frnd(-1e5, 1e5);
	f->hist2 = frnd(-1e5, 1e5);
	sk_coeffs(sctab, 0, TAB_N, TAB_N, (float)f->last_fres, f->last_q > 0.001 ? f->last_q : 0.001,
		  1, 0, &f->a1, &f->a2, &f->b02, &f->b1);
	if (res) {
		f->amp = frnd(0, 2e-5);
		f->amp_incr = frnd(-1e-8, 1e-8);
	}
}

/* A stand-in for fluidsynth's control part between blocks */
static void control(struct speq_synth_voice *v)
{
	int k;

	for (k = 0; k < SPEQ_SYNTH_BUFS; k++)
		if (rnd(8) == 0)
			v->tgt_amp[k] = frnd(0, 1);
	v->res.amp_incr = frnd(-1e-8, 1e-8);
	if (rnd(10) == 0) {
		v->res.fres_incr_count = 64 * (1 + rnd(5));
		v->res.fres_incr = frnd(-8, 8);
	}
	if (rnd(16) == 0) {
		v->custom.q_incr_count = 64;
		v->custom.q_incr = frnd(-0.05, 0.05);
	}
}

static void rand_voice(struct speq_synth_voice *v)
{
	uint32_t len, start, end, idx;
	int i;

	memset(v, 0, sizeof(*v));
	v->data = (uintptr_t)data;
	len = rnd(8) == 0 ? 2 + rnd(30) : 50 + rnd(60000);
	start = rnd(10) == 0 ? 0 : 100 + rnd(NSAMP - 200 - len);
	end = start + len;
	v->start = start;
	v->end = end;
	v->interp = rnd(3);
	v->looping = rnd(3) != 0;
	if (v->looping || rnd(2)) {
		v->loopstart = start + rnd(len - 1);
		v->loopend = rnd(6) ? v->loopstart + 2 + rnd(end - v->loopstart - 1) : v->loopstart + 2;
	} else {
		v->looping = 0;
	}
	v->has_looped = v->loopend && rnd(2);
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

/* One round of K blocks on synth s, from voices v (advanced in place: the round's end state,
 * the histories from render); out gets K x LINES x 64 floats. The control stand-in is seeded
 * so both synths see the same. */
static int round_on(struct speq_synth *s, struct speq_synth_voice *v, unsigned nv, unsigned K,
		    unsigned seed, float *outbuf, unsigned *Kgot, char *done)
{
	const float *out, *hist;
	unsigned b, i, save = rng;
	int ret;

	memset(done, 0, nv);

	*Kgot = speq_synth_begin_v2(s, nv, K, sctab, TAB_N);
	if (!*Kgot)
		return -EAGAIN;
	rng = seed;
	for (b = 0; b < *Kgot; b++)
		for (i = 0; i < nv; i++) {
			if (done[i])
				continue;
			ret = speq_synth_add_v2(s, i, b, &v[i]);
			if (ret)
				return ret;
			if (v[i].count < SK_BUF)
				done[i] = 1;
			control(&v[i]);
		}
	rng = save;
	if ((ret = speq_synth_render_v2(s, &out, &hist)))
		return ret;
	memcpy(outbuf, out, *Kgot * LINES * SK_BUF * sizeof(float));
	for (i = 0; i < nv; i++) {
		v[i].custom.hist1 = hist[4 * i];
		v[i].custom.hist2 = hist[4 * i + 1];
		v[i].res.hist1 = hist[4 * i + 2];
		v[i].res.hist2 = hist[4 * i + 3];
	}
	return 0;
}

static void compare(struct speq_synth *a, struct speq_synth *b, int rounds, const char *what)
{
	static struct speq_synth_voice va[MAXV], vb[MAXV];
	static float oa[16 * LINES * SK_BUF], ob[16 * LINES * SK_BUF];
	unsigned nv = 1, K, Ka, Kb, i, seed;
	char da[MAXV], db[MAXV];
	int r, bad = 0, ra, rb;
	long blocks = 0;
	double maxrel = 0, peak = 0;

	for (r = 0; r < rounds && !bad; r++) {
		if (r % 8 == 0) {
			nv = 1 + rnd(MAXV);
			for (i = 0; i < nv; i++)
				rand_voice(&va[i]);
			memcpy(vb, va, sizeof(va));
		}
		K = 1 + rnd(16);
		seed = rnd(1u << 30);
		ra = round_on(a, va, nv, K, seed, oa, &Ka, da);
		rb = round_on(b, vb, nv, K, seed, ob, &Kb, db);
		if (ra || rb || Ka != Kb) {
			printf("  round %d: %d/%d, %u/%u blocks\n", r, ra, rb, Ka, Kb);
			bad++;
			break;
		}
		blocks += (long)nv * Ka;
		for (i = 0; i < Ka * LINES * SK_BUF; i++) {
			double d = fabs((double)oa[i] - ob[i]);

			if (fabs(oa[i]) > peak)
				peak = fabs(oa[i]);
			if (d > maxrel * (peak > 1 ? peak : 1))
				maxrel = d / (peak > 1 ? peak : 1);
		}
		for (i = 0; i < nv; i++)
			if (va[i].phase != vb[i].phase || va[i].res.hist1 != vb[i].res.hist1 ||
			    va[i].custom.hist2 != vb[i].custom.hist2 || va[i].res.amp != vb[i].res.amp) {
				printf("  round %d voice %u differs: phase %llx/%llx hist %g/%g\n", r, i,
				       (unsigned long long)va[i].phase, (unsigned long long)vb[i].phase,
				       va[i].res.hist1, vb[i].res.hist1);
				bad++;
				break;
			}
		for (i = 0; i < nv; i++)
			if (da[i]) {	/* a new note in the slot */
				rand_voice(&va[i]);
				vb[i] = va[i];
			}
	}
	printf("  %ld voice blocks, outputs differ by at most %g of their peak (%g)\n", blocks,
	       maxrel, peak);
	check(!bad && maxrel < 1e-6, what);
}

int main(void)
{
	struct speq_synth_params p = {
		.abi = SPEQ_SYNTH_ABI, .voice_size = sizeof(struct speq_synth_voice),
		.lines = LINES, .max_voices = MAXV, .cubic = cubic, .linear = linear,
	};
	struct speq_synth *ppe, *spu, *s;
	static struct speq_synth_voice v[8];
	static float out[16 * LINES * SK_BUF];
	unsigned K;
	int ret, k, i;

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (!getenv("SPEQ_FAKE")) {
		fprintf(stderr, "synth-test: run with SPEQ_FAKE=1\n");
		return 2;
	}
	tables();
	check(sizeof(struct speq_synth_voice) == 312 && sizeof(struct synth_ent) == 256 &&
	      sizeof(struct sk_filt) == 48, "voice 312 bytes, entry 256, filter 48");
	p.abi = 1;
	check(speq_synth_open_v2(&s, &p) == -EPROTO && !s, "another ABI: -EPROTO");
	p.abi = SPEQ_SYNTH_ABI;
	spq_synth_tune(SPQ_SYNTH_OFF, -1, -1, -1);
	check(speq_synth_open_v2(&s, &p) == -ENOTSUP, "SPEQ_SYNTH=off: -ENOTSUP");

	spq_synth_tune(SPQ_SYNTH_PPE, -1, -1, -1);
	check(speq_synth_open_v2(&ppe, &p) == 0, "open, ppe");
	spq_synth_tune(SPQ_SYNTH_SPU, 10000000, -1, -1);	/* no timeouts in the comparison */
	check(speq_synth_open_v2(&spu, &p) == 0, "open, spu (fake)");
	if (!ppe || !spu)
		return 1;
	compare(ppe, spu, 400, "spu = ppe (400 rounds of 1-16 blocks, 1-100 voices)");

	/* auto: few voices stay with fluidsynth */
	spq_synth_tune(SPQ_SYNTH_AUTO, -1, -1, 4);
	check(speq_synth_begin_v2(spu, 3, 4, sctab, TAB_N) == 0, "auto: 3 voices, begin 0");
	for (i = 0; i < 8; i++)
		rand_voice(&v[i]);
	{
		char d[8];

		check(round_on(spu, v, 8, 4, 1, out, &K, d) == 0 && K == 4,
		      "auto: 8 voices, a round of 4");
	}

	/* timeouts: the round redone on the PPE, the same; three in a row: the back-off */
	{
		static struct speq_synth_voice a[8], b[8];
		static float oa[16 * LINES * SK_BUF];
		unsigned Ka;
		char d[8];

		spq_synth_tune(SPQ_SYNTH_SPU, 0, 0, -1);
		for (k = 0; k < 3; k++) {
			for (i = 0; i < 8; i++)
				rand_voice(&a[i]);
			memcpy(b, a, sizeof(a));
			ret = round_on(spu, a, 8, 8, 7 + k, out, &K, d);
			if (ret || round_on(ppe, b, 8, 8, 7 + k, oa, &Ka, d) || K != Ka)
				break;
			for (i = 0; i < (int)(K * LINES * SK_BUF); i++)
				if (fabsf(out[i] - oa[i]) > 1e-6f * (fabsf(oa[i]) > 1 ? fabsf(oa[i]) : 1) * 64)
					break;
			if (i < (int)(K * LINES * SK_BUF))
				break;
		}
		check(k == 3, "timeout 0: three rounds redone on the PPE, = the ppe path");
		check(speq_synth_begin_v2(spu, 8, 4, sctab, TAB_N) == 0,
		      "... then the SPEs are put aside (begin: 0)");
	}
	speq_synth_close_v2(spu);	/* waits for the lost rounds' jobs */
	speq_synth_close_v2(ppe);
	printf("%s: %d failure%s\n", failures ? "FAIL" : "ok", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
