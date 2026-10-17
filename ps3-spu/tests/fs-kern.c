/* fs-kern: lib/synth-kern.h against fluidsynth's own DSP functions, voice block by voice block:
 * fluid_rvoice_dsp_interpolate (none, linear, 4th order) and fluid_iir_filter_apply, linked from
 * a host build of unpatched fluidsynth (its object files: the functions are internal), on random
 * voices over random samples (loops of 2 and up, wraps, sample ends, filter sweeps, the custom
 * filter). Reports how many blocks match bit for bit and the largest difference, separately for
 * the interpolation and the filters. fs-kern [BLOCKS] [exact]: exit 1 when the interpolation or
 * the voices' state (phase, has_looped, count) differ, or with "exact" any filter output: for a
 * fluidsynth built without its -ffast-math on fluid_iir_filter_impl.cpp (src/CMakeLists.txt;
 * with it the compiler decides the filter's rounding). For tests/fluidsynth-compare.sh. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fluid_rvoice.h"
#include "fluid_iir_filter.h"
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

static void rand_filter(fluid_iir_filter_t *f, int res)
{
	memset(f, 0, sizeof(*f));
	f->type = res ? FLUID_IIR_LOWPASS : rnd(3) ? FLUID_IIR_DISABLED :
		  rnd(2) ? FLUID_IIR_LOWPASS : FLUID_IIR_HIGHPASS;
	f->flags = rnd(2) ? FLUID_IIR_NO_GAIN_AMP : 0;
	f->sincos_table = tab;
	f->last_fres = frnd(-900, 14000);
	f->last_q = frnd(0.0005, 12);
	f->hist1 = frnd(-1e6, 1e6);
	f->hist2 = frnd(-1e6, 1e6);
	f->a1 = frnd(-1.9, 1.9);
	f->a2 = frnd(0.1, 0.95);
	f->b02 = frnd(0, 0.5);
	f->b1 = frnd(-1, 1);
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

static double rel(double a, double b)
{
	double m = fabs(a) > 1 ? fabs(a) : 1;

	return fabs(a - b) / m;
}

int main(int argc, char **argv)
{
	static fluid_rvoice_t rv;
	static fluid_sample_t smp;
	static short data[NSAMP];
	static fluid_real_t a[64], b[64];
	struct speq_synth_voice v;
	struct synth_plan *pl = (struct synth_plan *)v.priv;
	struct sk_view sv;
	int n = argc > 1 ? atoi(argv[1]) : 200000, r, i, looping, ca, cb;
	long same_i = 0, same_f = 0, blocks = 0, bad = 0;
	double max_i = 0, max_f = 0;

	for (i = 0; i < NSAMP; i++)
		data[i] = (short)(12000 * sin(i * 0.031) + 6000 * sin(i * 0.37) + (int)rnd(2000) - 1000);
	fluid_iir_filter_init_table(tab, 44100);
	smp.data = data;
	memset(&sv, 0, sizeof(sv));
	sv.w[0] = sv.w[1] = data;
	sv.n[0] = sv.n[1] = 0xffffffffu;
	sv.t[0] = sv.t[1] = (const struct sk_sincos *)tab;
	sv.tn[0] = sv.tn[1] = SINCOS_TAB_SIZE;
	sv.tab_size = SINCOS_TAB_SIZE;
	sv.cubic = interp_coeff;
	sv.linear = interp_coeff_linear;
	for (r = 0; r < n; r++) {
		unsigned len = rnd(8) == 0 ? 2 + rnd(30) : 50 + rnd(60000);
		unsigned start = 100 + rnd(NSAMP - 200 - len), idx;
		static const int interps[3] = { FLUID_INTERP_NONE, FLUID_INTERP_LINEAR, FLUID_INTERP_4THORDER };
		int k = rnd(3);

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
			idx = rnd(5) == 0 ? rv.dsp.end - rnd(40) : start + rnd(len);
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
		filter_in(&v.custom, &rv.resonant_custom_filter);
		filter_in(&v.res, &rv.resonant_filter);
		pl->pt[SYNTH_PT_START] = data[v.start] * 256;
		pl->pt[SYNTH_PT_END] = data[v.end] * 256;
		if (v.loopend) {
			pl->pt[SYNTH_PT_LS] = data[v.loopstart] * 256;
			pl->pt[SYNTH_PT_LS1] = data[v.loopstart + 1] * 256;
			pl->pt[SYNTH_PT_LE1] = data[v.loopend - 1] * 256;
		}

		ca = fluid_rvoice_dsp_interpolate(&rv, a, looping);
		cb = k == 0 ? sk_none(&v, &sv, b, looping) : k == 1 ? sk_linear(&v, &sv, b, looping) :
		     sk_cubic(&v, &sv, b, looping);
		blocks++;
		if (ca != cb || rv.dsp.phase != v.phase || (rv.dsp.has_looped != 0) != (v.has_looped != 0)) {
			if (bad++ < 5)
				printf("  block %d: count %d/%d, phase %llx/%llx\n", r, ca, cb,
				       (unsigned long long)rv.dsp.phase, (unsigned long long)v.phase);
			continue;
		}
		if (!memcmp(a, b, ca * sizeof(a[0])))
			same_i++;
		for (i = 0; i < ca; i++)
			if (rel(a[i], b[i]) > max_i)
				max_i = rel(a[i], b[i]);
		if (!ca)
			continue;
		fluid_iir_filter_apply(&rv.resonant_filter, &rv.resonant_custom_filter, a, ca);
		sk_iir(&sv, 0, &v.custom, b, ca, !(v.custom.flags & SPEQ_SYNTH_IIR_NO_GAIN), 0,
		       v.custom.type == SPEQ_SYNTH_IIR_HIGHPASS);
		sk_iir(&sv, 1, &v.res, b, ca, 1, 1, 0);
		if (!memcmp(a, b, ca * sizeof(a[0])) && rv.resonant_filter.hist1 == v.res.hist1 &&
		    rv.resonant_filter.a1 == v.res.a1 && rv.resonant_filter.last_fres == v.res.last_fres)
			same_f++;
		for (i = 0; i < ca; i++)
			if (rel(a[i], b[i]) > max_f)
				max_f = rel(a[i], b[i]);
		if (rel(rv.resonant_filter.hist1, v.res.hist1) > max_f)
			max_f = rel(rv.resonant_filter.hist1, v.res.hist1);
	}
	printf("  %ld voice blocks: interpolation identical in %ld (max relative difference %g), "
	       "filters identical in %ld (max %g); %ld with another count or phase\n", blocks, same_i,
	       max_i, same_f, max_f, bad);
	return bad || max_i > 0 || (argc > 2 && !strcmp(argv[2], "exact") && max_f > 0);
}
