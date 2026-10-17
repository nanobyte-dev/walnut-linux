/* fs-kern: libspeq's DSP (lib/synth-kern.h, single precision, through speq_synth_*_v2 with
 * SPEQ_SYNTH=ppe) against fluidsynth's own fluid_rvoice_dsp_interpolate (none, linear, 4th
 * order) and fluid_iir_filter_apply, voice block by voice block, the latter linked from a host
 * build of unpatched fluidsynth (its object files: the functions are internal), on random voices
 * over random samples (loops of 2 and up, wraps, sample ends, filter sweeps, the custom filter).
 * The phase, has_looped and count after each block must match fluidsynth's exactly (the
 * control is integer code); the samples are compared as an SNR over all blocks.
 * fs-kern [BLOCKS] [MIN_SNR_DB]: exit 1 when a control result differs or the SNR is below
 * MIN_SNR_DB (default 90). For tests/fluidsynth-compare.sh. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fluid_rvoice.h"
#include "fluid_iir_filter.h"
#include "speq-int.h"
#include "synth-kern.h"

extern const fluid_real_t *const interp_coeff;
extern const fluid_real_t *const interp_coeff_linear;

#define NSAMP (1 << 18)

static unsigned rng = 4321;

static unsigned rnd(unsigned n)
{
	rng = rng * 1103515245 + 12345;
	return n ? (rng >> 8) % n : 0;
}

static double frnd(double a, double b)
{
	return a + (b - a) * (rnd(1u << 20) / (double)(1u << 20));
}

static fluid_iir_sincos_t tab[SINCOS_TAB_SIZE];

/* A filter as fluidsynth would have it: coefficients from its cutoff and Q */
static void rand_filter(fluid_iir_filter_t *f, int res)
{
	memset(f, 0, sizeof(*f));
	f->type = res ? FLUID_IIR_LOWPASS : rnd(3) ? FLUID_IIR_DISABLED :
		  rnd(2) ? FLUID_IIR_LOWPASS : FLUID_IIR_HIGHPASS;
	f->flags = rnd(2) ? FLUID_IIR_NO_GAIN_AMP : 0;
	f->sincos_table = tab;
	f->last_fres = frnd(1500, 13000);
	f->last_q = rnd(10) ? frnd(0.5, 6) : 0.0005;
	f->hist1 = frnd(-1e5, 1e5);
	f->hist2 = frnd(-1e5, 1e5);
	sk_coeffs((const struct sk_sincos *)tab, 0, SINCOS_TAB_SIZE, SINCOS_TAB_SIZE,
		  (float)f->last_fres, f->last_q > 0.001 ? f->last_q : 0.001,
		  res || !(f->flags & FLUID_IIR_NO_GAIN_AMP), !res && f->type == FLUID_IIR_HIGHPASS,
		  &f->a1, &f->a2, &f->b02, &f->b1);
	if (rnd(3) == 0) {
		f->fres_incr_count = 64 * (1 + rnd(5));
		f->fres_incr = frnd(-8, 8);
	}
	if (rnd(4) == 0) {
		f->q_incr_count = 64;
		f->q_incr = frnd(-0.05, 0.05);
	}
	if (res) {
		f->amp = frnd(0, 2e-5);
		f->amp_incr = frnd(-1e-8, 1e-8);
	}
}

static void filter_in(struct speq_synth_filter *f, const fluid_iir_filter_t *i)
{
	memset(f, 0, sizeof(*f));
	f->hist1 = i->hist1;
	f->hist2 = i->hist2;
	f->last_fres = i->last_fres;
	f->fres_incr = i->fres_incr;
	f->last_q = i->last_q;
	f->q_incr = i->q_incr;
	f->amp = i->amp;
	f->amp_incr = i->amp_incr;
	f->b02 = i->b02;
	f->b1 = i->b1;
	f->a1 = i->a1;
	f->a2 = i->a2;
	f->fres_incr_count = i->fres_incr_count;
	f->q_incr_count = i->q_incr_count;
	f->type = i->type;
	f->flags = i->flags & FLUID_IIR_NO_GAIN_AMP ? SPEQ_SYNTH_IIR_NO_GAIN : 0;
}

int main(int argc, char **argv)
{
	static fluid_rvoice_t rv;
	static fluid_sample_t smp;
	static short data[NSAMP];
	static fluid_real_t a[64];
	struct speq_synth_params p = { SPEQ_SYNTH_ABI, sizeof(struct speq_synth_voice), 1, 1,
				       interp_coeff, interp_coeff_linear };
	struct speq_synth_voice v;
	struct speq_synth *s;
	const float *out, *hist;
	int n = argc > 1 ? atoi(argv[1]) : 200000, r, i, looping, ca;
	double min_snr = argc > 2 ? atof(argv[2]) : 90, se = 0, sr = 0, snr, worst = 1e9;
	long blocks = 0, bad = 0;

	for (i = 0; i < NSAMP; i++)
		data[i] = (short)(12000 * sin(i * 0.031) + 6000 * sin(i * 0.37) + (int)rnd(2000) - 1000);
	fluid_iir_filter_init_table(tab, 44100);
	smp.data = data;
	spq_synth_tune(SPQ_SYNTH_PPE, -1, -1, -1);
	if (speq_synth_open_v2(&s, &p)) {
		printf("  can't open a synth\n");
		return 1;
	}
	for (r = 0; r < n; r++) {
		unsigned len = rnd(8) == 0 ? 2 + rnd(30) : 50 + rnd(60000);
		unsigned start = 100 + rnd(NSAMP - 200 - len), idx;
		static const int interps[3] = { FLUID_INTERP_NONE, FLUID_INTERP_LINEAR, FLUID_INTERP_4THORDER };
		int k = rnd(3);
		double bse = 0, bsr = 0;

		memset(&rv, 0, sizeof(rv));
		rv.dsp.sample = &smp;
		rv.dsp.interp_method = interps[k];
		rv.dsp.start = start;
		rv.dsp.end = start + len;
		looping = rnd(3) != 0;
		if (looping || rnd(2)) {
			rv.dsp.loopstart = start + rnd(len - 1);
			rv.dsp.loopend = rnd(6) ? rv.dsp.loopstart + 2 + rnd(rv.dsp.end - rv.dsp.loopstart - 1) :
				    rv.dsp.loopstart + 2;
		} else {
			looping = 0;
		}
		rv.dsp.has_looped = rv.dsp.loopend && rnd(2);
		if (looping)
			idx = rv.dsp.has_looped ? rv.dsp.loopstart + rnd(rv.dsp.loopend - rv.dsp.loopstart) :
						 start + rnd(rv.dsp.loopend - start);
		else
			idx = rnd(5) == 0 ? rv.dsp.end - rnd(len < 40 ? len : 40) : start + rnd(len);
		/* a voice that looped is in its loop or past it (released): fluidsynth never has it
		 * before the loop, where its start segment would differ from the window's samples */
		if (rv.dsp.has_looped && idx < rv.dsp.loopstart)
			idx = rv.dsp.loopstart;
		rv.dsp.phase = (uint64_t)idx << 32 | (rnd(1u << 16) << 16 | rnd(1u << 16));
		rv.dsp.phase_incr = rnd(20) == 0 ? frnd(20, 60) : rnd(5) == 0 ? frnd(0.001, 0.2) : frnd(0.2, 4);
		rand_filter(&rv.resonant_custom_filter, 0);
		rand_filter(&rv.resonant_filter, 1);

		memset(&v, 0, sizeof(v));
		v.data = (uintptr_t)data;
		v.phase = rv.dsp.phase;
		v.phase_incr = rv.dsp.phase_incr;
		v.start = rv.dsp.start;
		v.end = rv.dsp.end;
		v.loopstart = rv.dsp.loopstart;
		v.loopend = rv.dsp.loopend;
		v.interp = k;
		v.looping = looping;
		v.has_looped = rv.dsp.has_looped;
		v.line[0] = 0;
		v.line[1] = v.line[2] = v.line[3] = -1;
		v.cur_amp[0] = v.tgt_amp[0] = 1;
		filter_in(&v.custom, &rv.resonant_custom_filter);
		filter_in(&v.res, &rv.resonant_filter);

		ca = fluid_rvoice_dsp_interpolate(&rv, a, looping);
		if (ca)
			fluid_iir_filter_apply(&rv.resonant_filter, &rv.resonant_custom_filter, a, ca);
		if (speq_synth_begin_v2(s, 1, 1, tab, SINCOS_TAB_SIZE) != 1 ||
		    speq_synth_add_v2(s, 0, 0, &v) || speq_synth_render_v2(s, &out, &hist)) {
			printf("  block %d: libspeq refused\n", r);
			return 1;
		}
		blocks++;
		if (ca != v.count || rv.dsp.phase != v.phase ||
		    (rv.dsp.has_looped != 0) != (v.has_looped != 0) ||
		    (ca && (float)rv.resonant_filter.last_fres != (float)v.res.last_fres)) {
			if (bad++ < 5)
				printf("  block %d: count %d/%d, phase %llx/%llx\n", r, ca, v.count,
				       (unsigned long long)rv.dsp.phase, (unsigned long long)v.phase);
			continue;
		}
		for (i = 0; i < ca; i++) {
			double d = out[i] - a[i];

			bse += d * d;
			bsr += a[i] * a[i];
		}
		se += bse;
		sr += bsr;
		if (bsr > 0 && bse > 0 && 10 * log10(bsr / bse) < worst)
			worst = 10 * log10(bsr / bse);
	}
	snr = se ? 10 * log10(sr / se) : 999;
	printf("  %ld voice blocks: control identical in %ld; samples: SNR %.1f dB over all, the worst "
	       "block %.1f dB\n", blocks, blocks - bad, snr, worst);
	speq_synth_close_v2(s);
	return bad || snr < min_snr;
}
