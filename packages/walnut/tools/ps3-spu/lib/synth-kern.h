/* The voice DSP of fluidsynth 2.6.1, in single precision with 4-wide vectors (GCC's generic
 * vectors: SPU, AltiVec on the PPE, SSE on the host), shared by the SPU program
 * (spu/synth-op.c) and libspeq's PPE path (lib/synth.c). Not bit-exact to fluidsynth's double C
 * path (the SPU's double precision is ~10 times slower than its single); the error is measured
 * against it (tests/fluidsynth-compare.sh, tests/fs-kern.c: SNR).
 *
 * Split in two, so that fluidsynth's control part can run every block of a round before any DSP:
 * - the control of the interpolation (sk_control: fluid_rvoice_dsp_interpolate_*'s loops without
 *   the samples: which runs of output samples read which window, the phase, has_looped and the
 *   count after the block) and of the filters (sk_filter_end: the cutoff/Q smoothing, the
 *   coefficients and the gain after the block) depend on no sample: the PPE runs them while
 *   fluidsynth's control part goes block by block (lib/synth.c, speq_synth_add_v2);
 * - the DSP (sk_entry): one voice block from its state before the block, the filters' histories
 *   carried from the voice's previous block in the round.
 *
 * Samples are read through "ext" windows: the int16 samples around the block's phase converted to
 * float, with the interpolation's end points (fluidsynth's end_point1/2, the linear's point)
 * written in where fluidsynth uses them instead of a sample, so every output sample is one dot
 * product: cubic ext[i-1..i+2], linear ext[i..i+1], none ext[round(phase)]; the cubic's start
 * segment (start_point and the samples after it) reads the int16 window itself. Window 0 serves
 * until the first loop wrap, window 1 (from loopstart - 1) after it. */
#ifndef SYNTH_KERN_H
#define SYNTH_KERN_H

#include <stdint.h>
#include <string.h>
#include "speq.h"
#include "synth-job.h"
#ifdef __SPU__
#include <spu_intrinsics.h>
#endif

#define SK_BUF		64
#define SK_FRES_MIN	1500
#define SK_PASSES	65536	/* loop wraps in one block: past this the DSP is stuck */
#define SK_Q_MIN	0.001f
#define SK_RUNS		(SK_BUF + 2)

typedef float v4f __attribute__((vector_size(16)));

struct sk_sincos {
	float sin, cos;		/* fluid_iir_sincos_t */
};

/* A run of output samples [i0, i0 + n) from phase on, all read from window w; kind 1: the
 * cubic's start segment (start_point, then 3 samples) */
struct sk_run {
	uint64_t phase;
	uint16_t i0, n;
	uint16_t w, kind;
};

#define SK_IDX(x)	((unsigned int)((x) >> 32))
#define SK_IDX_ROUND(x)	((unsigned int)(((x) + 0x80000000) >> 32))
#define SK_ROW(x)	((unsigned int)((uint32_t)(x) & 0xff000000) >> 24)
#define SK_INT(b)	((uint64_t)(b) << 32)

/* fluid_phase_set_float */
static inline uint64_t sk_phase_float(double b)
{
	return (((uint64_t)(b)) << 32) | (uint32_t)(((double)(b) - (int)(b)) * (double)4294967296.0);
}

/* n * incr for n <= 127 (no 64-bit multiply on the SPU: a libgcc call) */
static inline uint64_t sk_nmul(unsigned n, uint64_t incr)
{
	uint64_t r = 0;
	int b;

	for (b = 0; b < 7; b++)
		if (n & (1u << b))
			r += incr << b;
	return r;
}

/* compute_interpolation_steps: min(ceil((boundary - phase) / incr), room); the quotient only
 * matters below 64, so a 6-bit long division instead of a 64-bit one (a libgcc call on the
 * SPU). incr < 2^56. */
static inline unsigned sk_steps(uint64_t phase, uint64_t incr, unsigned int end_index,
				unsigned dsp_i)
{
	uint64_t boundary = SK_INT((unsigned int)(end_index + 1)), d;
	unsigned room = SK_BUF - dsp_i, q = 0;
	int b;

	if (phase > boundary)
		return 0;
	d = boundary - phase + incr - 1;
	if (d >= incr << 6)
		return room;
	for (b = 5; b >= 0; b--)
		if (d >= incr << b) {
			d -= incr << b;
			q |= 1u << b;
		}
	return q < room ? q : room;
}

struct sk_ctl {
	struct sk_run *runs;	/* NULL: count only */
	int nruns, w, kind;
	unsigned dsp_i;
	uint64_t phase, incr;
};

/* n samples of the current window from the current phase (fluidsynth's for loops) */
static inline void sk_emit(struct sk_ctl *c, unsigned n)
{
	if (!n)
		return;
	if (c->runs) {
		struct sk_run *r = c->nruns ? &c->runs[c->nruns - 1] : NULL;

		if (r && r->w == c->w && r->kind == c->kind && r->i0 + r->n == c->dsp_i &&
		    r->phase + sk_nmul(r->n, c->incr) == c->phase) {
			r->n += n;	/* contiguous, no wrap between: one run */
		} else if (c->nruns < SK_RUNS) {
			r = &c->runs[c->nruns++];
			r->phase = c->phase;
			r->i0 = c->dsp_i;
			r->n = n;
			r->w = c->w;
			r->kind = c->kind;
		}
	}
	c->phase += sk_nmul(n, c->incr);
	c->dsp_i += n;
}

/* The interpolation's control for one block (fluid_rvoice_dsp_interpolate_none/linear/
 * 4th_order_local, sample reads left out): the count, the phase and has_looped after it, and
 * the runs. -1: stuck (more than SK_PASSES wraps) or the runs don't fit. */
static int sk_control(int interp, int looping, uint64_t phase, uint64_t incr, uint32_t start,
		      uint32_t end, uint32_t loopstart, uint32_t loopend, int *has_looped,
		      uint64_t *phase_out, struct sk_run *runs, int *nruns)
{
	struct sk_ctl c = { runs, 0, 0, 0, 0, phase, incr };
	unsigned int idx, end_index, start_index;
	int passes = 0;

#define WRAP()	do { c.phase -= SK_INT(loopend - loopstart); *has_looped = 1; c.w = 1; } while (0)
	if (interp == SPEQ_SYNTH_NONE) {
		end_index = looping ? loopend - 1 : end;
		while (1) {
			sk_emit(&c, sk_steps(c.phase, incr, end_index, c.dsp_i));
			idx = SK_IDX_ROUND(c.phase);
			if (!looping)
				break;
			if (idx > end_index)
				WRAP();
			if (c.dsp_i >= SK_BUF || ++passes > SK_PASSES)
				break;
		}
	} else if (interp == SPEQ_SYNTH_LINEAR) {
		end_index = (looping ? loopend - 1 : end) - 1;
		while (1) {
			sk_emit(&c, sk_steps(c.phase, incr, end_index, c.dsp_i));
			if (c.dsp_i >= SK_BUF)
				break;
			end_index++;
			sk_emit(&c, sk_steps(c.phase, incr, end_index, c.dsp_i));
			if (!looping)
				break;
			if (SK_IDX(c.phase) > end_index)
				WRAP();
			if (c.dsp_i >= SK_BUF || ++passes > SK_PASSES)
				break;
			end_index--;
		}
	} else {
		end_index = (looping ? loopend - 1 : end) - 2;
		start_index = *has_looped ? loopstart : start;
		while (1) {
			c.kind = 1;
			sk_emit(&c, sk_steps(c.phase, incr, start_index, c.dsp_i));
			c.kind = 0;
			sk_emit(&c, sk_steps(c.phase, incr, end_index, c.dsp_i));
			if (c.dsp_i >= SK_BUF)
				break;
			end_index++;
			sk_emit(&c, sk_steps(c.phase, incr, end_index, c.dsp_i));
			end_index++;
			sk_emit(&c, sk_steps(c.phase, incr, end_index, c.dsp_i));
			if (!looping)
				break;
			if (SK_IDX(c.phase) > end_index) {
				WRAP();
				start_index = loopstart;
			}
			if (c.dsp_i >= SK_BUF || ++passes > SK_PASSES)
				break;
			end_index -= 2;
		}
	}
#undef WRAP
	if (passes > SK_PASSES || (runs && c.nruns >= SK_RUNS))
		return -1;
	*phase_out = c.phase;
	if (nruns)
		*nruns = c.nruns;
	return c.dsp_i;
}

/* fluid_iir_filter_calculate_coefficients<float, gain_norm, highpass ? HIGHPASS : LOWPASS>;
 * tab: the entries from index tfirst on (tn of them) */
static inline void sk_coeffs(const struct sk_sincos *tab, int32_t tfirst, int32_t tn,
			     int32_t tab_size, float fres, float q, int gain_norm, int highpass,
			     float *a1_out, float *a2_out, float *b02_out, float *b1_out)
{
	float filter_gain = 1.0f, sin_coeff, cos_coeff, alpha_coeff, a0_inv, b1_temp;
	int32_t tab_idx = (int32_t)(fres) - SK_FRES_MIN, i;

	if (tab_idx < 0)
		tab_idx = 0;
	if (tab_idx > tab_size - 1)
		tab_idx = tab_size - 1;
	i = tab_idx - tfirst;
	if (i < 0)
		i = 0;
	if (i > tn - 1)
		i = tn - 1;
	sin_coeff = tab[i].sin;
	cos_coeff = tab[i].cos;
	alpha_coeff = sin_coeff / (2.0f * q);
	a0_inv = 1.0f / (1.0f + alpha_coeff);
	*a1_out = -2.0f * cos_coeff * a0_inv;
	*a2_out = (1.0f - alpha_coeff) * a0_inv;
	if (gain_norm)
		filter_gain /= __builtin_sqrtf(q);
	if (highpass) {
		b1_temp = (1.0f + cos_coeff) * a0_inv * filter_gain;
		*b02_out = b1_temp * 0.5f;
		*b1_out = -b1_temp;
	} else {
		b1_temp = (1.0f - cos_coeff) * a0_inv * filter_gain;
		*b02_out = b1_temp * 0.5f;
		*b1_out = b1_temp;
	}
}

/* The filter's parameters after count samples (fluid_iir_filter_apply_local without the
 * samples): the smoothing counts, cutoff and Q, the coefficients (from the last cutoff and Q,
 * as the per-sample recomputation leaves them), the gain. Histories untouched. */
static inline void sk_filter_end(struct speq_synth_filter *f, unsigned count, int amplify,
				 const struct sk_sincos *tab, int32_t tab_size)
{
	float fres, q, fres_incr, q_incr;
	int fc, qc, changed = 0;
	unsigned k;

	if (f->type == SPEQ_SYNTH_IIR_OFF || f->last_q < (double)0.001 || !count)
		return;
	fres = (float)f->last_fres;
	q = (float)f->last_q;
	fres_incr = (float)f->fres_incr;
	q_incr = (float)f->q_incr;
	fc = f->fres_incr_count;
	qc = f->q_incr_count;
	for (k = 0; k < count && (fc > 0 || qc > 0); k++) {
		if (fc > 0) {
			--fc;
			fres += fres_incr;
		}
		if (qc > 0) {
			--qc;
			q += q_incr;
			if (q < SK_Q_MIN) {
				qc = 0;
				q = SK_Q_MIN;
			}
		}
		changed = 1;
	}
	if (changed)
		sk_coeffs(tab, 0, tab_size, tab_size, fres, q,
			  amplify || !(f->flags & SPEQ_SYNTH_IIR_NO_GAIN),
			  !amplify && f->type == SPEQ_SYNTH_IIR_HIGHPASS, &f->a1, &f->a2, &f->b02, &f->b1);
	f->last_fres = fres;
	f->last_q = q;
	f->fres_incr_count = fc;
	f->q_incr_count = qc;
	if (amplify)
		f->amp += f->amp_incr * count;
}

/* --- the DSP --- */

struct sk_tabs {
	const v4f *cubic;		/* 256 rows of 4 */
	const float *linear;		/* 256 rows of 2 */
	int32_t sincos_n;
};

/* A window into ext: the int16 samples from index lo on (src points at sample max(lo, 0)), n
 * entries, as fluid_rvoice_get_sample16 gives them (x 256), then the special points. ext is
 * 16-aligned with room for 8 past n. */
static inline void sk_ext(float *ext, int32_t lo, int32_t n, const int16_t *src,
			  const struct synth_ent *e, int w)
{
	unsigned int limit = e->looping ? e->loopend - 1 : e->end;
	int32_t i = 0;

	if (lo < 0) {	/* the sample at index 0 of the data: nothing before it */
		for (; i < -lo && i < n; i++)
			ext[i] = 0;
		for (; i < n; i++)
			ext[i] = (float)((int32_t)src[i + lo] * 256);
	} else {
#ifdef __SPU__
		/* 8 samples per step: an unaligned quadword, each halfword into the top of a word,
		 * an arithmetic shift right by 8 (x 256), a conversion */
		const vec_uchar16 hi4 = { 0, 1, 0x80, 0x80, 2, 3, 0x80, 0x80, 4, 5, 0x80, 0x80, 6, 7, 0x80, 0x80 };
		const vec_uchar16 lo4 = { 8, 9, 0x80, 0x80, 10, 11, 0x80, 0x80, 12, 13, 0x80, 0x80, 14, 15, 0x80, 0x80 };
		const vec_uchar16 *q = (const vec_uchar16 *)((uintptr_t)src & ~15u);
		unsigned off = (uintptr_t)src & 15u;
		vec_uchar16 q0 = q[0], q1, h;
		vec_float4 *x = (vec_float4 *)ext;

		for (; i < n; i += 8, q++) {
			q1 = q[1];
			h = spu_or(spu_slqwbyte(q0, off), spu_rlmaskqwbyte(q1, (int)off - 16));
			x[0] = spu_convtf(spu_rlmaska((vec_int4)spu_shuffle(h, h, hi4), -8), 0);
			x[1] = spu_convtf(spu_rlmaska((vec_int4)spu_shuffle(h, h, lo4), -8), 0);
			x += 2;
			q0 = q1;
		}
#else
		for (; i < n; i++)
			ext[i] = (float)((int32_t)src[i] * 256);
#endif
	}
	if (e->interp == SPEQ_SYNTH_NONE)
		return;
#define PUT(idx, v)	do { int64_t j_ = (int64_t)(idx) - lo; if (j_ >= 0 && j_ < n) ext[j_] = (v); } while (0)
	PUT((uint64_t)limit + 1, e->pt[SYNTH_PT_EP1]);
	if (e->interp == SPEQ_SYNTH_CUBIC)
		PUT((uint64_t)limit + 2, e->pt[SYNTH_PT_EP2]);
#undef PUT
	(void)w;
}

#ifdef __SPU__
/* 4 floats from ext[j] on (ext 16-aligned, j >= 0): two quadwords shifted together */
static inline vec_float4 sk_ld4(const float *ext, int32_t j)
{
	const vec_uchar16 *p = (const vec_uchar16 *)ext + (j >> 2);
	int sh = (j & 3) << 2;

	return (vec_float4)spu_or(spu_slqwbyte(p[0], sh), spu_rlmaskqwbyte(p[1], sh - 16));
}

/* 4 floats to out (a full-block run is aligned; others go one by one) */
static inline void sk_st4(float *out, vec_float4 v)
{
	if (!((uintptr_t)out & 15)) {
		*(vec_float4 *)out = v;
	} else {
		out[0] = spu_extract(v, 0);
		out[1] = spu_extract(v, 1);
		out[2] = spu_extract(v, 2);
		out[3] = spu_extract(v, 3);
	}
}
#endif

/* One run: n output samples from phase, out[0..n) */
static inline void sk_interp(int interp, const float *ext, int32_t lo, uint64_t phase,
			     uint64_t incr, unsigned n, float *out, const struct sk_tabs *t)
{
	unsigned k = 0;

	if (interp == SPEQ_SYNTH_CUBIC) {
#ifdef __SPU__
		const vec_uchar16 ev = { 0, 1, 2, 3, 16, 17, 18, 19, 8, 9, 10, 11, 24, 25, 26, 27 };
		const vec_uchar16 od = { 4, 5, 6, 7, 20, 21, 22, 23, 12, 13, 14, 15, 28, 29, 30, 31 };
		const vec_uchar16 lo2 = { 0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23 };
		const vec_uchar16 hi2 = { 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 30, 31 };

		for (; k + 4 <= n; k += 4) {
			vec_float4 m0, m1, m2, m3, a, b, c;

			m0 = spu_mul(sk_ld4(ext, SK_IDX(phase) - 1 - lo), t->cubic[SK_ROW(phase)]);
			phase += incr;
			m1 = spu_mul(sk_ld4(ext, SK_IDX(phase) - 1 - lo), t->cubic[SK_ROW(phase)]);
			phase += incr;
			m2 = spu_mul(sk_ld4(ext, SK_IDX(phase) - 1 - lo), t->cubic[SK_ROW(phase)]);
			phase += incr;
			m3 = spu_mul(sk_ld4(ext, SK_IDX(phase) - 1 - lo), t->cubic[SK_ROW(phase)]);
			phase += incr;
			/* four horizontal sums at once */
			a = spu_add(spu_shuffle(m0, m1, ev), spu_shuffle(m0, m1, od));
			c = spu_add(spu_shuffle(m2, m3, ev), spu_shuffle(m2, m3, od));
			b = spu_add(spu_shuffle(a, c, lo2), spu_shuffle(a, c, hi2));
			sk_st4(out + k, b);
		}
#else
		for (; k + 4 <= n; k += 4) {
			v4f m[4], a, b, c, d;
			int j;

			for (j = 0; j < 4; j++) {
				v4f s;

				memcpy(&s, &ext[(int32_t)(SK_IDX(phase) - 1 - lo)], sizeof(s));
				m[j] = s * t->cubic[SK_ROW(phase)];
				phase += incr;
			}
			/* four horizontal sums at once */
			a = __builtin_shuffle(m[0], m[1], (__attribute__((vector_size(16))) int){ 0, 4, 2, 6 });
			b = __builtin_shuffle(m[0], m[1], (__attribute__((vector_size(16))) int){ 1, 5, 3, 7 });
			c = __builtin_shuffle(m[2], m[3], (__attribute__((vector_size(16))) int){ 0, 4, 2, 6 });
			d = __builtin_shuffle(m[2], m[3], (__attribute__((vector_size(16))) int){ 1, 5, 3, 7 });
			a += b;
			c += d;
			b = __builtin_shuffle(a, c, (__attribute__((vector_size(16))) int){ 0, 1, 4, 5 });
			d = __builtin_shuffle(a, c, (__attribute__((vector_size(16))) int){ 2, 3, 6, 7 });
			b += d;
			memcpy(&out[k], &b, sizeof(b));
		}
#endif
		for (; k < n; k++) {
			const float *s = &ext[(int32_t)(SK_IDX(phase) - 1 - lo)];
			const float *c = (const float *)&t->cubic[SK_ROW(phase)];

			out[k] = c[0] * s[0] + c[1] * s[1] + c[2] * s[2] + c[3] * s[3];
			phase += incr;
		}
	} else if (interp == SPEQ_SYNTH_LINEAR) {
		for (; k < n; k++) {
			const float *s = &ext[(int32_t)(SK_IDX(phase) - lo)];
			const float *c = &t->linear[SK_ROW(phase) * 2];

			out[k] = c[0] * s[0] + c[1] * s[1];
			phase += incr;
		}
	} else {
		for (; k < n; k++) {
			out[k] = ext[(int32_t)(SK_IDX_ROUND(phase) - lo)];
			phase += incr;
		}
	}
}

/* fluid_iir_filter_apply_local in float: hist is the filter's (h1, h2), carried */
static inline void sk_iir(const struct sk_filt *f, float *hist, float *buf, unsigned count,
			  int gain_norm, int amplify, int highpass, const struct sk_sincos *tab,
			  int32_t tfirst, int32_t tn, int32_t tab_size)
{
	float h1 = hist[0], h2 = hist[1], a1 = f->a1, a2 = f->a2, b02 = f->b02, b1 = f->b1;
	float amp = f->amp, amp_incr = f->amp_incr, fres = f->fres, q = f->q;
	int fc = f->fcount, qc = f->qcount;
	unsigned i;

	if (f->off)
		return;
	if (fc <= 0 && qc <= 0) {
		/* constant coefficients: the common case; the gain after, in vectors */
		static const v4f ramp = { 0, 1, 2, 3 };
		v4f *bv = (v4f *)buf, g, gi;

		i = 0;
#ifdef __SPU__
		for (; i + 4 <= count; i += 4) {
			vec_float4 x = bv[i / 4];
			float c0, c1, c2, c3, y0, y1, y2, y3;

			/* the older history first: one multiply-add on the dependency chain */
			c0 = spu_extract(x, 0) - a2 * h2 - a1 * h1;
			y0 = b02 * (c0 + h2) + b1 * h1;
			c1 = spu_extract(x, 1) - a2 * h1 - a1 * c0;
			y1 = b02 * (c1 + h1) + b1 * c0;
			c2 = spu_extract(x, 2) - a2 * c0 - a1 * c1;
			y2 = b02 * (c2 + c0) + b1 * c1;
			c3 = spu_extract(x, 3) - a2 * c1 - a1 * c2;
			y3 = b02 * (c3 + c1) + b1 * c2;
			h2 = c2;
			h1 = c3;
			bv[i / 4] = (vec_float4){ y0, y1, y2, y3 };
		}
#endif
		for (; i < count; i++) {
			float cn = buf[i] - a2 * h2 - a1 * h1;
			float s = b02 * (cn + h2) + b1 * h1;

			h2 = h1;
			h1 = cn;
			buf[i] = s;
		}
		if (amplify) {
			g = amp + amp_incr * ramp;
			gi = (v4f){ 4, 4, 4, 4 } * amp_incr;
			for (i = 0; i < SK_BUF / 4; i++) {
				bv[i] *= g;
				g += gi;
			}
		}
	} else {
		for (i = 0; i < count; i++) {
			float cn = buf[i] - a2 * h2 - a1 * h1;
			float s = b02 * (cn + h2) + b1 * h1;

			h2 = h1;
			h1 = cn;
			buf[i] = amplify ? (amp + amp_incr * (float)i) * s : s;
			if (fc > 0 || qc > 0) {
				if (fc > 0) {
					--fc;
					fres += f->fres_incr;
				}
				if (qc > 0) {
					--qc;
					q += f->q_incr;
					if (q < SK_Q_MIN) {
						qc = 0;
						q = SK_Q_MIN;
					}
				}
				sk_coeffs(tab, tfirst, tn, tab_size, fres, q, gain_norm, highpass, &a1,
					  &a2, &b02, &b1);
			}
		}
	}
	/* denormals: as fluidsynth, and the SPU flushes them anyway */
	if (__builtin_fabsf(h1) < 1e-20f)
		h1 = 0;
	if (__builtin_fabsf(h2) < 1e-20f)
		h2 = 0;
	hist[0] = h1;
	hist[1] = h2;
}

/* fluid_rvoice_buffers_mix of one block into the block's lines; buf is zero past count */
static inline void sk_mix(const struct synth_ent *e, const float *buf, unsigned count,
			  float *lines)
{
	static const v4f ramp = { 0, 1, 2, 3 };
	int i, k;

	if (!count)
		return;
	for (i = 0; i < SPEQ_SYNTH_BUFS; i++) {
		float cur = e->cur[i], tgt = e->tgt[i], inc;
		v4f *l, g, gi;

		if (e->line[i] < 0 || (cur == 0.0f && tgt == 0.0f))
			continue;
		l = (v4f *)(lines + e->line[i] * SK_BUF);
		inc = (tgt - cur) / SK_BUF;
		g = cur + inc * ramp;
		gi = (v4f){ 4, 4, 4, 4 } * inc;
		for (k = 0; k < SK_BUF / 4; k++) {
			l[k] += g * ((const v4f *)buf)[k];	/* buf 16-aligned */
			g += gi;
		}
	}
}

/* Where an entry's reads are: each window's int16 samples (at sample max(elo, 0)), each
 * filter's sin/cos slice, the ext scratch */
struct sk_reads {
	const int16_t *win[2];
	const struct sk_sincos *tab[2];
	float *ext[2];
};

/* The cubic's start segment: start_point, then the samples themselves (from the int16 window
 * src, at sample index first), unpatched: past a short loop's end it reads samples, not the end
 * points */
static inline void sk_start(const struct synth_ent *e, const int16_t *src, int32_t first,
			    float start_point, const struct sk_run *u, float *out,
			    const struct sk_tabs *t)
{
	uint64_t phase = u->phase;
	unsigned k;

	for (k = 0; k < u->n; k++) {
		const int16_t *s = &src[(int32_t)(SK_IDX(phase) - first)];
		const float *c = (const float *)&t->cubic[SK_ROW(phase)];

		out[k] = c[0] * start_point + c[1] * (float)(s[0] * 256) + c[2] * (float)(s[1] * 256) +
			 c[3] * (float)(s[2] * 256);
		phase += e->incr;
	}
}

/* Whether a run's reads stay inside its window (the plan's promise; checked, so that a wrong
 * plan fails its job instead of reading outside local store buffers) */
static inline int sk_inside(const struct synth_ent *e, const struct sk_run *u)
{
	uint64_t last = u->phase + sk_nmul(u->n - 1, e->incr);
	int64_t lo = (int64_t)SK_IDX(u->phase) - (e->interp == SPEQ_SYNTH_CUBIC && !u->kind);
	int64_t hi = (int64_t)(e->interp == SPEQ_SYNTH_NONE ? SK_IDX_ROUND(last) : SK_IDX(last)) +
		     (e->interp == SPEQ_SYNTH_CUBIC ? 2 : e->interp == SPEQ_SYNTH_LINEAR);

	return lo >= e->elo[u->w] && hi < (int64_t)e->elo[u->w] + e->en[u->w];
}

/* One voice block: interpolation, the custom and the resonant filter, the mix into lines (the
 * block's). hist: the voice's 4 histories (custom h1 h2, resonant h1 h2), carried. 0, or -1. */
static inline int sk_entry(const struct synth_ent *e, const struct sk_reads *r, float *hist,
			   float *buf, float *lines, const struct sk_tabs *t)
{
	struct sk_run runs[SK_RUNS];
	int nruns, i, has_looped = e->has_looped, count;
	uint64_t phase;

	count = sk_control(e->interp, e->looping, e->phase, e->incr, e->start, e->end, e->loopstart,
			   e->loopend, &has_looped, &phase, runs, &nruns);
	if (count < 0 || count != e->count)
		return -1;	/* not the PPE's control */
	if (!count)
		return 0;
	for (i = 0; i < 2; i++)
		if (e->en[i])
			sk_ext(r->ext[i], e->elo[i], e->en[i], r->win[i], e, i);
	for (i = 0; i < nruns; i++) {
		const struct sk_run *u = &runs[i];

		if (!e->en[u->w])
			return -2;	/* the plan missed the window */
		if (!sk_inside(e, u))
			return -3;	/* ... or part of it */
		if (u->kind)
			sk_start(e, r->win[u->w], e->elo[u->w] < 0 ? 0 : e->elo[u->w],
				 e->pt[u->w ? SYNTH_PT_LE1 : SYNTH_PT_SP], u, buf + u->i0, t);
		else
			sk_interp(e->interp, r->ext[u->w], e->elo[u->w], u->phase, e->incr, u->n,
				  buf + u->i0, t);
	}
	for (i = count; i < SK_BUF; i++)
		buf[i] = 0;
	sk_iir(&e->custom, hist, buf, count, !(e->custom.flags & SPEQ_SYNTH_IIR_NO_GAIN), 0,
	       e->custom.type == SPEQ_SYNTH_IIR_HIGHPASS, r->tab[0], e->tab_first[0], e->tab_n[0],
	       t->sincos_n);
	sk_iir(&e->res, hist + 2, buf, count, 1, 1, 0, r->tab[1], e->tab_first[1], e->tab_n[1],
	       t->sincos_n);
	for (i = count; i < SK_BUF; i++)
		buf[i] = 0;
	sk_mix(e, buf, count, lines);
	return 0;
}

#endif
