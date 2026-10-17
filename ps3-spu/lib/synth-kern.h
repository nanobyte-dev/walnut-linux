/* The voice DSP of fluidsynth 2.6.1 for one block, in C: the SPU program (spu/synth-op.c) and
 * libspeq's PPE path (lib/synth.c, SPEQ_SYNTH=ppe) both include it. A port of
 *   fluid_rvoice_dsp.cpp: fluid_rvoice_dsp_interpolate_{none,linear,4th_order}_local,
 *   fluid_iir_filter_impl.cpp: fluid_iir_filter_apply_local, ..._calculate_coefficients,
 *   fluid_rvoice_mixer.c: fluid_rvoice_buffers_mix (one block),
 * with the same types (fluid_real_t is double in Debian's build, the filter coefficients
 * float), the same expressions in the same order (so a compiler contracts them the same way)
 * and the same unsigned arithmetic. Keep it in step with fluidsynth: a new upstream version is
 * checked against this file (tests/fluidsynth-compare.sh renders a MIDI file both ways).
 *
 * Samples are read through windows: struct sk_view gives each window's local copy and the
 * sample index of its first element (the SPU's DMA'd ranges; on the PPE the sample data itself
 * from index 0). Window 0 serves the reads until the first loop wrap, window 1 after it.
 * SK_CHECK builds (the host tests) check every read against the window's bounds. */
#ifndef SYNTH_KERN_H
#define SYNTH_KERN_H

#include <stdint.h>
#include "speq.h"
#include "synth-job.h"

#define SK_BUFSIZE	64
#define SK_FRES_MIN	1500
#define SK_PASSES	65536	/* loop wraps in one block: past this the DSP is stuck */
#define SK_Q_MIN	((double)0.001)	/* Q_MIN: (fluid_real_t)0.001 */

struct sk_sincos {
	float sin, cos;		/* fluid_iir_sincos_t */
};

struct sk_view {
	const int16_t *w[2];		/* windows */
	uint32_t first[2], n[2];
	const struct sk_sincos *t[2];	/* sin/cos: custom filter, resonant filter */
	int32_t tfirst[2], tn[2];
	const double *cubic, *linear;
	int32_t tab_size;		/* SINCOS_TAB_SIZE */
	int bad;			/* SK_CHECK: a read outside its window */
};

#ifdef SK_CHECK
#define SK_IN(c, sv)	((c) ? 1 : ((sv)->bad = 1, 0))
#else
#define SK_IN(c, sv)	1
#endif

/* fluid_rvoice_get_sample16 + the (fluid_real_t) conversion */
static inline double sk_smp(struct sk_view *sv, const int16_t *w, uint32_t off, uint32_t n,
			    unsigned int idx)
{
	uint32_t i = idx - off;

	if (!SK_IN(i < n, sv))
		return 0;
	return (double)(int32_t)(((uint32_t)w[i]) << 8);
}

static inline double sk_pt(int32_t v)
{
	return (double)v;
}

/* fluid_phase_set_float */
static inline uint64_t sk_phase_float(double b)
{
	return (((uint64_t)(b)) << 32) | (uint32_t)(((double)(b) - (int)(b)) * (double)4294967296.0);
}

#define SK_IDX(x)	((unsigned int)((x) >> 32))
#define SK_IDX_ROUND(x)	((unsigned int)(((x) + 0x80000000) >> 32))
#define SK_ROW(x)	((unsigned int)((uint32_t)(x) & 0xff000000) >> 24)
#define SK_INT(b)	((uint64_t)(b) << 32)

/* compute_interpolation_steps */
static inline unsigned short sk_steps(uint64_t phase, uint64_t incr, unsigned int end_index,
				      unsigned short dsp_i)
{
	uint64_t boundary = SK_INT((unsigned int)(end_index + 1));
	uint64_t steps = phase > boundary ? 0 : ((boundary - phase + incr - 1) / incr);
	uint64_t room = (uint64_t)(SK_BUFSIZE - dsp_i);

	return (unsigned short)(steps < room ? steps : room);
}

/* The current window: 0, then 1 from the first loop wrap */
struct sk_win {
	const int16_t *w;
	uint32_t off, n;
};

static inline void sk_win_set(struct sk_win *k, const struct sk_view *sv, int i)
{
	k->w = sv->w[i];
	k->off = sv->first[i];
	k->n = sv->n[i];
}

#define S(idx)	sk_smp(sv, win.w, win.off, win.n, (idx))

static inline int sk_none(struct speq_synth_voice *v, struct sk_view *sv, double *dsp_buf, int looping)
{
	uint64_t dsp_phase = v->phase, dsp_phase_incr = sk_phase_float(v->phase_incr);
	unsigned short dsp_i = 0, safe_count;
	unsigned int dsp_phase_index, end_index;
	struct sk_win win;
	int passes = 0;

	sk_win_set(&win, sv, 0);
	end_index = looping ? v->loopend - 1 : v->end;
	while (1) {
		dsp_phase_index = SK_IDX_ROUND(dsp_phase);
		safe_count = sk_steps(dsp_phase, dsp_phase_incr, end_index, dsp_i);
		for (; safe_count--; dsp_i++) {
			dsp_buf[dsp_i] = S(dsp_phase_index);
			dsp_phase += dsp_phase_incr;
			dsp_phase_index = SK_IDX_ROUND(dsp_phase);
		}
		if (!looping)
			break;
		if (dsp_phase_index > end_index) {
			dsp_phase -= SK_INT(v->loopend - v->loopstart);
			v->has_looped = 1;
			sk_win_set(&win, sv, 1);
		}
		if (dsp_i >= SK_BUFSIZE)
			break;
		if (++passes > SK_PASSES)
			return -1;
	}
	v->phase = dsp_phase;
	return dsp_i;
}

static inline int sk_linear(struct speq_synth_voice *v, struct sk_view *sv, double *dsp_buf, int looping)
{
	const struct synth_plan *pl = (const struct synth_plan *)v->priv;
	uint64_t dsp_phase = v->phase, dsp_phase_incr = sk_phase_float(v->phase_incr);
	unsigned short dsp_i = 0, safe_count;
	unsigned int dsp_phase_index, end_index;
	double point;
	const double *coeffs;
	struct sk_win win;
	int passes = 0;

	sk_win_set(&win, sv, 0);
	end_index = (looping ? v->loopend - 1 : v->end) - 1;
	point = sk_pt(looping ? pl->pt[SYNTH_PT_LS] : pl->pt[SYNTH_PT_END]);
	while (1) {
		dsp_phase_index = SK_IDX(dsp_phase);
		safe_count = sk_steps(dsp_phase, dsp_phase_incr, end_index, dsp_i);
		for (; safe_count--; dsp_i++) {
			double s0 = S(dsp_phase_index), s1 = S(dsp_phase_index + 1);

			coeffs = &sv->linear[SK_ROW(dsp_phase) * 2];
			dsp_buf[dsp_i] = (coeffs[0] * s0 + coeffs[1] * s1);
			dsp_phase += dsp_phase_incr;
			dsp_phase_index = SK_IDX(dsp_phase);
		}
		if (dsp_i >= SK_BUFSIZE)
			break;
		end_index++;
		safe_count = sk_steps(dsp_phase, dsp_phase_incr, end_index, dsp_i);
		for (; safe_count--; dsp_i++) {
			double s0 = S(dsp_phase_index);

			coeffs = &sv->linear[SK_ROW(dsp_phase) * 2];
			dsp_buf[dsp_i] = (coeffs[0] * s0 + coeffs[1] * point);
			dsp_phase += dsp_phase_incr;
			dsp_phase_index = SK_IDX(dsp_phase);
		}
		if (!looping)
			break;
		if (dsp_phase_index > end_index) {
			dsp_phase -= SK_INT(v->loopend - v->loopstart);
			v->has_looped = 1;
			sk_win_set(&win, sv, 1);
		}
		if (dsp_i >= SK_BUFSIZE)
			break;
		end_index--;
		if (++passes > SK_PASSES)
			return -1;
	}
	v->phase = dsp_phase;
	return dsp_i;
}

static inline double sk_cubic1(const double *coeffs, double s0, double s1, double s2, double s3)
{
	return (coeffs[0] * s0
		+ coeffs[1] * s1
		+ coeffs[2] * s2
		+ coeffs[3] * s3);
}

static inline int sk_cubic(struct speq_synth_voice *v, struct sk_view *sv, double *dsp_buf, int looping)
{
	const struct synth_plan *pl = (const struct synth_plan *)v->priv;
	uint64_t dsp_phase = v->phase, dsp_phase_incr = sk_phase_float(v->phase_incr);
	unsigned short dsp_i = 0, safe_count;
	unsigned int dsp_phase_index, start_index, end_index;
	double start_point, end_point1, end_point2;
	struct sk_win win;
	int passes = 0;

#define C4(a, b, c, d)	sk_cubic1(&sv->cubic[SK_ROW(dsp_phase) * 4], (a), (b), (c), (d))
	sk_win_set(&win, sv, 0);
	end_index = (looping ? v->loopend - 1 : v->end) - 2;
	if (v->has_looped) {
		start_index = v->loopstart;
		start_point = sk_pt(pl->pt[SYNTH_PT_LE1]);
	} else {
		start_index = v->start;
		start_point = sk_pt(pl->pt[SYNTH_PT_START]);
	}
	if (looping) {
		end_point1 = sk_pt(pl->pt[SYNTH_PT_LS]);
		end_point2 = sk_pt(pl->pt[SYNTH_PT_LS1]);
	} else {
		end_point1 = sk_pt(pl->pt[SYNTH_PT_END]);
		end_point2 = end_point1;
	}
	while (1) {
		dsp_phase_index = SK_IDX(dsp_phase);
		safe_count = sk_steps(dsp_phase, dsp_phase_incr, start_index, dsp_i);
		for (; safe_count--; dsp_i++) {
			double s1 = S(dsp_phase_index), s2 = S(dsp_phase_index + 1),
			       s3 = S(dsp_phase_index + 2);

			dsp_buf[dsp_i] = C4(start_point, s1, s2, s3);
			dsp_phase += dsp_phase_incr;
			dsp_phase_index = SK_IDX(dsp_phase);
		}
		safe_count = sk_steps(dsp_phase, dsp_phase_incr, end_index, dsp_i);
		for (; safe_count--; dsp_i++) {
			double s0 = S(dsp_phase_index - 1), s1 = S(dsp_phase_index),
			       s2 = S(dsp_phase_index + 1), s3 = S(dsp_phase_index + 2);

			dsp_buf[dsp_i] = C4(s0, s1, s2, s3);
			dsp_phase += dsp_phase_incr;
			dsp_phase_index = SK_IDX(dsp_phase);
		}
		if (dsp_i >= SK_BUFSIZE)
			break;
		end_index++;
		safe_count = sk_steps(dsp_phase, dsp_phase_incr, end_index, dsp_i);
		for (; safe_count--; dsp_i++) {
			double s0 = S(dsp_phase_index - 1), s1 = S(dsp_phase_index),
			       s2 = S(dsp_phase_index + 1);

			dsp_buf[dsp_i] = C4(s0, s1, s2, end_point1);
			dsp_phase += dsp_phase_incr;
			dsp_phase_index = SK_IDX(dsp_phase);
		}
		end_index++;
		safe_count = sk_steps(dsp_phase, dsp_phase_incr, end_index, dsp_i);
		for (; safe_count--; dsp_i++) {
			double s0 = S(dsp_phase_index - 1), s1 = S(dsp_phase_index);

			dsp_buf[dsp_i] = C4(s0, s1, end_point1, end_point2);
			dsp_phase += dsp_phase_incr;
			dsp_phase_index = SK_IDX(dsp_phase);
		}
		if (!looping)
			break;
		if (dsp_phase_index > end_index) {
			dsp_phase -= SK_INT(v->loopend - v->loopstart);
			if (!v->has_looped) {
				v->has_looped = 1;
				start_index = v->loopstart;
				start_point = sk_pt(pl->pt[SYNTH_PT_LE1]);
			}
			sk_win_set(&win, sv, 1);
		}
		if (dsp_i >= SK_BUFSIZE)
			break;
		end_index -= 2;
		if (++passes > SK_PASSES)
			return -1;
	}
#undef C4
	v->phase = dsp_phase;
	return dsp_i;
}
#undef S

/* fluid_iir_filter_calculate_coefficients<float, gain_norm, highpass ? HIGHPASS : LOWPASS> */
static inline void sk_coeffs(struct sk_view *sv, int f, float fres, float q, int gain_norm,
			     int highpass, float *a1_out, float *a2_out, float *b02_out,
			     float *b1_out)
{
	float filter_gain = 1.0f, sin_coeff, cos_coeff, alpha_coeff, a0_inv, a1_temp, a2_temp,
	      b02_temp, b1_temp;
	int32_t tab_idx = ((int32_t)(fres) - SK_FRES_MIN) / 1, i;

	if (tab_idx < 0)
		tab_idx = 0;
	if (tab_idx > sv->tab_size - 1)
		tab_idx = sv->tab_size - 1;
	i = tab_idx - sv->tfirst[f];
	if (!SK_IN(i >= 0 && i < sv->tn[f], sv))
		i = 0;
	sin_coeff = sv->t[f][i].sin;
	cos_coeff = sv->t[f][i].cos;
	alpha_coeff = sin_coeff / (2.0f * q);
	a0_inv = 1.0f / (1.0f + alpha_coeff);
	a1_temp = -2.0f * cos_coeff * a0_inv;
	a2_temp = (1.0f - alpha_coeff) * a0_inv;
	if (gain_norm)
		filter_gain /= __builtin_sqrtf(q);
	if (highpass) {
		b1_temp = (1.0f + cos_coeff) * a0_inv * filter_gain;
		b02_temp = b1_temp * 0.5f;
		b1_temp *= -1.0f;
	} else {
		b1_temp = (1.0f - cos_coeff) * a0_inv * filter_gain;
		b02_temp = b1_temp * 0.5f;
	}
	*a1_out = a1_temp;
	*a2_out = a2_temp;
	*b02_out = b02_temp;
	*b1_out = b1_temp;
}

/* fluid_iir_filter_apply_local<gain_norm, amplify, highpass ? HIGHPASS : LOWPASS>; f: the
 * filter's sin/cos slice (0 custom, 1 resonant) */
static inline void sk_iir(struct sk_view *sv, int f, struct speq_synth_filter *iir,
			  double *dsp_buf, unsigned int count, int gain_norm, int amplify,
			  int highpass)
{
	double dsp_hist1, dsp_hist2, dsp_amp, dsp_amp_incr;
	float dsp_a1, dsp_a2, dsp_b02, dsp_b1, fres, q, fres_incr, q_incr;
	int fres_incr_count, q_incr_count;
	unsigned int dsp_i;

	if (iir->type == SPEQ_SYNTH_IIR_OFF || iir->last_q < SK_Q_MIN)
		return;
	dsp_hist1 = iir->hist1;
	dsp_hist2 = iir->hist2;
	dsp_a1 = iir->a1;
	dsp_a2 = iir->a2;
	dsp_b02 = iir->b02;
	dsp_b1 = iir->b1;
	fres_incr_count = iir->fres_incr_count;
	q_incr_count = iir->q_incr_count;
	dsp_amp = iir->amp;
	dsp_amp_incr = iir->amp_incr;
	fres = (float)iir->last_fres;
	q = (float)iir->last_q;
	fres_incr = (float)iir->fres_incr;
	q_incr = (float)iir->q_incr;
	for (dsp_i = 0; dsp_i < count; dsp_i++) {
		double dsp_centernode = dsp_buf[dsp_i] - dsp_a1 * dsp_hist1 - dsp_a2 * dsp_hist2;
		double sample = dsp_b02 * (dsp_centernode + dsp_hist2) + dsp_b1 * dsp_hist1;

		dsp_hist2 = dsp_hist1;
		dsp_hist1 = dsp_centernode;
		if (amplify) {
			dsp_buf[dsp_i] = dsp_amp * sample;
			dsp_amp += dsp_amp_incr;
		} else {
			dsp_buf[dsp_i] = sample;
		}
		if (fres_incr_count > 0 || q_incr_count > 0) {
			if (fres_incr_count > 0) {
				--fres_incr_count;
				fres += fres_incr;
			}
			if (q_incr_count > 0) {
				--q_incr_count;
				q += q_incr;
				if (q < SK_Q_MIN) {
					q_incr_count = 0;
					q = SK_Q_MIN;
				}
			}
			sk_coeffs(sv, f, fres, q, gain_norm, highpass, &dsp_a1, &dsp_a2, &dsp_b02,
				  &dsp_b1);
		}
	}
	iir->a1 = dsp_a1;
	iir->a2 = dsp_a2;
	iir->b02 = dsp_b02;
	iir->b1 = dsp_b1;
	if (__builtin_fabs(dsp_hist1) < 1e-20f)
		dsp_hist1 = 0.0f;
	if (__builtin_fabs(dsp_hist2) < 1e-20f)
		dsp_hist2 = 0.0f;
	iir->hist1 = dsp_hist1;
	iir->hist2 = dsp_hist2;
	iir->last_fres = fres;
	iir->fres_incr_count = fres_incr_count;
	iir->last_q = q;
	iir->q_incr_count = q_incr_count;
	iir->amp = dsp_amp;
}

/* fluid_rvoice_buffers_mix of one block (start_block 0, sample_count count) into lines */
static inline void sk_mix(struct speq_synth_voice *v, const double *dsp_buf, int sample_count,
			  double *lines, unsigned nlines)
{
	int i, dsp_i;

	if (sample_count <= 0)
		return;
	for (i = 0; i < SPEQ_SYNTH_BUFS; i++) {
		double *buf, target_amp = v->tgt_amp[i], current_amp = v->cur_amp[i], amp_incr;

		if (v->line[i] < 0 || (unsigned)v->line[i] >= nlines ||
		    (current_amp == 0.0f && target_amp == 0.0f))
			continue;
		buf = lines + v->line[i] * SK_BUFSIZE;
		amp_incr = (target_amp - current_amp) / SK_BUFSIZE;
		if (sample_count < SK_BUFSIZE) {
			for (dsp_i = 0; dsp_i < sample_count; dsp_i++) {
				buf[dsp_i] += current_amp * dsp_buf[dsp_i];
				current_amp += amp_incr;
			}
		} else {
			for (dsp_i = 0; dsp_i < SK_BUFSIZE; dsp_i++)
				buf[dsp_i] += (current_amp + amp_incr * dsp_i) * dsp_buf[dsp_i];
		}
		v->cur_amp[i] = target_amp;
	}
}

/* One voice: fluid_rvoice_write_dsp (interpolation, then the custom and the resonant filter)
 * and the mix. 0, or -1: the interpolation is stuck (more than SK_PASSES loop wraps). */
static inline int sk_voice(struct speq_synth_voice *v, struct sk_view *sv, double *dsp_buf,
		    double *lines, unsigned nlines)
{
	int count;

	switch (v->interp) {
	case SPEQ_SYNTH_NONE:
		count = sk_none(v, sv, dsp_buf, v->looping);
		break;
	case SPEQ_SYNTH_LINEAR:
		count = sk_linear(v, sv, dsp_buf, v->looping);
		break;
	default:
		count = sk_cubic(v, sv, dsp_buf, v->looping);
		break;
	}
	if (count < 0)
		return -1;
	v->count = count;
	if (count == 0)
		return 0;
	/* fluid_iir_filter_apply: the custom filter (no amplification), then the resonant one */
	sk_iir(sv, 0, &v->custom, dsp_buf, count, !(v->custom.flags & SPEQ_SYNTH_IIR_NO_GAIN), 0,
	       v->custom.type == SPEQ_SYNTH_IIR_HIGHPASS);
	sk_iir(sv, 1, &v->res, dsp_buf, count, 1, 1, 0);
	sk_mix(v, dsp_buf, count, lines, nlines);
	return 0;
}

#endif
