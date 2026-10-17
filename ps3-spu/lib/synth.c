/* speq_synth_*_v2: fluidsynth's voice DSP on the SPEs (synth.spu), called by fluidsynth's mixer
 * (through dlopen) a round of blocks at a time. The policy lives here, so tuning never needs a
 * fluidsynth rebuild.
 *
 * Modes (SPEQ_SYNTH): auto (default) and spu use the process's synth queue: SPEQ_SYNTH_SPES
 * workers (2: the two SPEs bulk queues leave to deadline queues, speq.h SPEQ_OPEN_BULK) running
 * synth.spu, opened by the first speq_synth_open_v2 (a synth's creation, not its audio callback)
 * and never closed; its workers park after 200 ms idle (music stopped) and free their contexts
 * after a minute. auto takes rounds of at least SPEQ_SYNTH_MIN voices (4: below that the PPE
 * renders them faster than a round trip costs), spu every round. ppe runs the same DSP
 * (synth-kern.h) on the calling thread: the hook's whole path without SPEs. off:
 * speq_synth_open_v2 fails and fluidsynth stays stock.
 *
 * A round: begin takes one of NROUND rounds (entries for nvoices x K blocks, outputs, a done word)
 * and picks K (at most ENTS entries; a job's output and entries must fit its local store). add,
 * for each voice block after fluidsynth's control part, runs the sample-free part of the DSP
 * (synth-kern.h sk_control, sk_filter_end) so fluidsynth can go on to the next block, plans the
 * windows and slices the DSP will read, reads the special sample points, touches the windows'
 * pages (a DMA into a page that isn't resident costs 40-87 us on the worker's thread, a
 * swapped-out SoundFont page far more) and writes the entry. A voice whose windows exceed what a
 * job's local store holds is rendered by the PPE instead (all its blocks of the round). render
 * cuts the voices into jobs (whole voices; enough jobs for every worker), queues them, renders
 * the PPE's voices meanwhile, sleeps on the done word (no spin by default: the thread shares the
 * PPE with the game) for at most SPEQ_SYNTH_TIMEOUT_US + 5 us per voice block, then sums the
 * outputs. A late or failed round is redone on the PPE from its entries (the SPU never writes
 * them) and abandoned to its jobs (taken again once its done word completes); MAX_FAILS in a row
 * put the SPEs aside for BACKOFF_MS; a queue whose workers all broke, for good. */
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "speq-int.h"
#include "synth-kern.h"

#define SYNTH_PROG	"synth.spu"
#define NROUND		2
#define ENTS		512	/* voice blocks per round */
#define MAX_JOBS	8
#define MIN_VOICES	4
#define SPES		2
#define TIMEOUT_US	2000
#define ENT_US		5	/* added to the timeout per voice block */
#define MAX_FAILS	3
#define BACKOFF_MS	5000
#define IDLE_US		200000	/* workers stay resident between audio callbacks */
#define FREE_MS		60000
#define STATS_S		10

enum { M_OFF, M_PPE, M_SPU, M_AUTO };
enum { R_FREE, R_OPEN, R_LOST };
enum { V_HAS = 1, V_PPE = 2 };

static pthread_once_t once = PTHREAD_ONCE_INIT;
static int mode = M_AUTO, debug;
static unsigned min_voices = MIN_VOICES, spes = SPES;
static long timeout_us = TIMEOUT_US, ent_us = ENT_US;
static int spin_us;
static long page;

static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
static struct speq *queue;	/* set once, never closed */
static int qerr;		/* the open's error, or -EIO once the queue broke */

struct round {
	struct speq_done done;
	struct synth_ent *ents;		/* ENTS */
	float *hist_in, *hist_out, *hist;	/* 4 per voice slot: in, the SPU's, what render gives */
	float *jout;			/* MAX_JOBS x SYNTH_OUT_MAX x 64 */
	float *sum;			/* K x lines x 64: what render gives */
	uint8_t *vflag;
	unsigned nv, K, njobs, nents;
	int state;
	struct { unsigned v0, n; } job[MAX_JOBS];
};

struct speq_synth {
	struct speq_synth_params p;
	struct speq *q;
	struct round *r[NROUND];
	struct round *cur;
	struct speq_job jobs[MAX_JOBS];
	struct {
		v4f cubic[SYNTH_INTERP_ROWS];
		float linear[SYNTH_INTERP_ROWS * 2];
	} *tabs;
	const struct sk_sincos *sincos;	/* the round's */
	uint32_t sincos_n;
	float *ext[2];			/* the PPE's DSP scratch */
	int32_t ext_cap, ext_need;
	unsigned maxv;
	int fails;
	double backoff_until, last_print;
	struct {
		unsigned long rounds, spu, ppe, small, ents_spu, ents_ppe, jobs, timeouts, errors,
			      nofree, backoffs, redone;
		double wait_us, wait_max;
	} st;
};

static unsigned env_u(const char *name, unsigned def)
{
	const char *s = getenv(name);

	return s && *s ? (unsigned)strtoul(s, NULL, 0) : def;
}

static void init(void)
{
	const char *s = getenv("SPEQ_SYNTH"), *d = getenv("SPEQ_DEBUG");

	if (!s || !*s || !strcmp(s, "auto"))
		mode = M_AUTO;
	else if (!strcmp(s, "off"))
		mode = M_OFF;
	else if (!strcmp(s, "ppe"))
		mode = M_PPE;
	else if (!strcmp(s, "spu"))
		mode = M_SPU;
	else
		fprintf(stderr, "speq: SPEQ_SYNTH=%s unknown, using auto\n", s);
	debug = d && *d && strcmp(d, "0");
	min_voices = env_u("SPEQ_SYNTH_MIN", MIN_VOICES);
	spes = env_u("SPEQ_SYNTH_SPES", SPES);
	timeout_us = env_u("SPEQ_SYNTH_TIMEOUT_US", TIMEOUT_US);
	spin_us = env_u("SPEQ_SYNTH_SPIN_US", 0);
	page = sysconf(_SC_PAGESIZE);
	if (page <= 0)
		page = 4096;
}

void spq_synth_tune(int m, long timeout, int per_ent_us, int min)
{
	pthread_once(&once, init);
	if (per_ent_us >= 0)
		ent_us = per_ent_us;
	if (m >= M_OFF && m <= M_AUTO)
		mode = m;
	if (timeout >= 0)
		timeout_us = timeout;
	if (min >= 0)
		min_voices = min;
}

static struct speq *get_queue(int *err)
{
	struct speq *q;
	int ret;

	pthread_mutex_lock(&qlock);
	if (!queue && !qerr) {
		ret = speq_open(&q, SYNTH_PROG, spes, 0);
		if (ret) {
			qerr = ret;
		} else {
			speq_tune(q, spin_us, IDLE_US, FREE_MS);
			__atomic_store_n(&queue, q, __ATOMIC_RELEASE);
		}
	}
	q = qerr ? NULL : queue;
	*err = qerr;
	pthread_mutex_unlock(&qlock);
	return q;
}

static void queue_broke(void)
{
	pthread_mutex_lock(&qlock);
	if (!qerr)
		qerr = -EIO;
	pthread_mutex_unlock(&qlock);
}

static int queue_ok(void)
{
	return !__atomic_load_n(&qerr, __ATOMIC_ACQUIRE);
}

static void print_stats(struct speq_synth *s, const char *when)
{
	fprintf(stderr, "speq: synth %p %s: %lu rounds: %lu spu (%lu voice blocks, %lu jobs, "
		"%.0f us mean wait, %.0f max), %lu ppe, %lu small; %lu voice blocks on the PPE, "
		"%lu timeouts, %lu errors, %lu rounds redone on the PPE, %lu with no free round, "
		"%lu backoffs\n", (void *)s, when, s->st.rounds, s->st.spu, s->st.ents_spu,
		s->st.jobs, s->st.spu ? s->st.wait_us / s->st.spu : 0, s->st.wait_max, s->st.ppe,
		s->st.small, s->st.ents_ppe, s->st.timeouts, s->st.errors, s->st.redone, s->st.nofree,
		s->st.backoffs);
}

static void maybe_print(struct speq_synth *s)
{
	double now;

	if (!debug)
		return;
	now = spq_now_us();
	if (now - s->last_print >= STATS_S * 1e6) {
		s->last_print = now;
		print_stats(s, "");
	}
}

static void free_round(struct round *r)
{
	if (!r)
		return;
	free(r->ents);
	free(r->hist_in);
	free(r->hist_out);
	free(r->hist);
	free(r->jout);
	free(r->sum);
	free(r->vflag);
	free(r);
}

static void free_synth(struct speq_synth *s)
{
	unsigned i;

	for (i = 0; i < NROUND; i++)
		free_round(s->r[i]);
	free(s->tabs);
	free(s->ext[0]);
	free(s->ext[1]);
	free(s);
}

static void *zalloc(size_t n)
{
	void *p = NULL;

	if (posix_memalign(&p, SPEQ_LINE, n ? n : SPEQ_LINE))
		return NULL;
	memset(p, 0, n);	/* touched: no fault on the workers' threads */
	return p;
}

static struct round *new_round(unsigned maxv, unsigned lines)
{
	struct round *r = zalloc(sizeof(*r));

	if (!r)
		return NULL;
	r->ents = zalloc(ENTS * sizeof(struct synth_ent));
	r->hist_in = zalloc(maxv * 4 * sizeof(float));
	r->hist_out = zalloc(maxv * 4 * sizeof(float));
	r->hist = zalloc(maxv * 4 * sizeof(float));
	r->jout = zalloc(MAX_JOBS * SYNTH_OUT_MAX * SK_BUF * sizeof(float));
	r->sum = zalloc(SYNTH_OUT_MAX * SK_BUF * sizeof(float));
	r->vflag = zalloc(maxv);
	if (!r->ents || !r->hist_in || !r->hist_out || !r->hist || !r->jout || !r->sum ||
	    !r->vflag) {
		free_round(r);
		return NULL;
	}
	(void)lines;
	return r;
}

static int ext_reserve(struct speq_synth *s, int32_t n)
{
	int i;

	if (n <= s->ext_cap)
		return 0;
	for (i = 0; i < 2; i++) {
		void *p = NULL;

		if (posix_memalign(&p, 16, (n + 16) * sizeof(float)))
			return -ENOMEM;
		free(s->ext[i]);
		s->ext[i] = p;
	}
	s->ext_cap = n;
	return 0;
}

int speq_synth_open_v2(struct speq_synth **sp, const struct speq_synth_params *p)
{
	struct speq_synth *s;
	unsigned i;
	int err = 0;

	pthread_once(&once, init);
	if (!sp)
		return -EINVAL;
	*sp = NULL;
	if (!p)
		return -EINVAL;
	if (p->abi != SPEQ_SYNTH_ABI || p->voice_size != sizeof(struct speq_synth_voice))
		return -EPROTO;
	if (!p->lines || p->lines > SPEQ_SYNTH_LINES || !p->cubic || !p->linear)
		return -EINVAL;
	if (mode == M_OFF)
		return -ENOTSUP;
	s = calloc(1, sizeof(*s));
	if (!s)
		return -ENOMEM;
	s->p = *p;
	s->maxv = p->max_voices ? p->max_voices : 256;
	if (s->maxv > 1024)
		s->maxv = 1024;
	/* the coefficient tables in float, 16-aligned for the SPEs' DMA */
	s->tabs = zalloc(sizeof(*s->tabs));
	if (!s->tabs) {
		free_synth(s);
		return -ENOMEM;
	}
	for (i = 0; i < SYNTH_INTERP_ROWS; i++) {
		s->tabs->cubic[i] = (v4f){ p->cubic[4 * i], p->cubic[4 * i + 1], p->cubic[4 * i + 2],
					   p->cubic[4 * i + 3] };
		s->tabs->linear[2 * i] = p->linear[2 * i];
		s->tabs->linear[2 * i + 1] = p->linear[2 * i + 1];
	}
	for (i = 0; i < NROUND; i++)
		if (!(s->r[i] = new_round(s->maxv, p->lines))) {
			free_synth(s);
			return -ENOMEM;
		}
	if (ext_reserve(s, SYNTH_WIN_MAX + 16)) {
		free_synth(s);
		return -ENOMEM;
	}
	if (mode != M_PPE) {
		s->q = get_queue(&err);
		if (!s->q) {
			free_synth(s);
			return err;
		}
	}
	s->last_print = spq_now_us();
	*sp = s;
	return 0;
}

unsigned speq_synth_begin_v2(struct speq_synth *s, unsigned nv, unsigned blocks,
			     const void *sincos, uint32_t sincos_n)
{
	struct round *r = NULL;
	unsigned i, K, vj;

	if (!s || !nv || !blocks || nv > s->maxv || !sincos || !sincos_n)
		return 0;
	if (s->q && !queue_ok())
		return 0;
	if (s->backoff_until) {
		if (spq_now_us() < s->backoff_until)
			return 0;
		s->backoff_until = 0;
	}
	if (s->q && mode == M_AUTO && nv < min_voices) {
		s->st.small++;
		return 0;
	}
	for (i = 0; i < NROUND && !r; i++) {
		struct round *c = s->r[i];

		if (c->state == R_LOST &&
		    speq_wait_timeout(s->q, &c->done, 0, SPEQ_WAIT_NOSPIN) != -ETIMEDOUT)
			c->state = R_FREE;
		if (c->state == R_FREE)
			r = c;
	}
	if (!r) {
		s->st.nofree++;
		return 0;
	}
	K = blocks;
	if (K > ENTS / nv)
		K = ENTS / nv;
	if (K > SYNTH_OUT_MAX / s->p.lines)
		K = SYNTH_OUT_MAX / s->p.lines;
	for (; K; K--) {	/* jobs of whole voices, at most MAX_JOBS */
		vj = SYNTH_JOB_ENTS / K;
		if (vj && (nv + vj - 1) / vj <= MAX_JOBS)
			break;
	}
	if (!K)
		return 0;
	for (i = 0; i < nv * K; i++)
		r->ents[i].flags = SYNTH_ENT_SKIP;
	memset(r->vflag, 0, nv);
	r->nv = nv;
	r->K = K;
	r->nents = 0;
	r->state = R_OPEN;
	s->cur = r;
	s->sincos = sincos;
	s->sincos_n = sincos_n;
	s->ext_need = 0;
	return K;
}

/* fluid_rvoice_get_sample16 as float */
static float smp(const struct speq_synth_voice *v, unsigned int idx)
{
	const int16_t *d = (const int16_t *)(uintptr_t)v->data;

	return (float)((int32_t)d[idx] * 256);
}

static void filt_in(struct sk_filt *f, const struct speq_synth_filter *s, int res)
{
	f->b02 = s->b02;
	f->b1 = s->b1;
	f->a1 = s->a1;
	f->a2 = s->a2;
	f->fres = (float)s->last_fres;
	f->fres_incr = (float)s->fres_incr;
	f->q = (float)s->last_q;
	f->q_incr = (float)s->q_incr;
	f->amp = res ? (float)s->amp : 0;
	f->amp_incr = res ? (float)s->amp_incr : 0;
	f->fcount = s->fres_incr_count > 0 ? (s->fres_incr_count > 32767 ? 32767 : s->fres_incr_count) : 0;
	f->qcount = s->q_incr_count > 0 ? (s->q_incr_count > 32767 ? 32767 : s->q_incr_count) : 0;
	f->type = s->type;
	f->flags = s->flags;
	f->off = s->type == SPEQ_SYNTH_IIR_OFF || s->type > SPEQ_SYNTH_IIR_HIGHPASS ||
		 !(s->last_q >= (double)0.001);
}

/* A filter's sin/cos slice for the block: the entries its coefficients are recomputed from while
 * it sweeps (none when it doesn't); -1 when it's too wide or not computable */
static int plan_tab(const struct speq_synth_filter *f, unsigned count, uint32_t n,
		    uint32_t *first, uint32_t *cnt)
{
	double a, z, lo, hi;
	int steps;

	*first = *cnt = 0;
	if (f->type == SPEQ_SYNTH_IIR_OFF || !(f->last_q >= (double)0.001) ||
	    (f->fres_incr_count <= 0 && f->q_incr_count <= 0))
		return 0;
	steps = f->fres_incr_count > (int)count ? (int)count :
		f->fres_incr_count > 0 ? f->fres_incr_count : 0;
	a = f->last_fres;
	z = f->last_fres + f->fres_incr * steps;
	if (!isfinite(a) || !isfinite(z))
		return -1;
	/* (int)fres - FRES_MIN, clipped: float rounding of 64 adds moves it far less than 2 */
	lo = floor(a < z ? a : z) - SK_FRES_MIN - 2;
	hi = ceil(a < z ? z : a) - SK_FRES_MIN + 2;
	if (lo < 0)
		lo = 0;
	if (hi < 0)
		hi = 0;
	if (lo > n - 1)
		lo = n - 1;
	if (hi > n - 1)
		hi = n - 1;
	if (hi - lo + 1 > SYNTH_TAB_MAX)
		return -1;
	*first = lo;
	*cnt = hi - lo + 1;
	return 0;
}

/* One load per page of a window: a swapped-out page comes back on the PPE */
static void touch(const struct synth_ent *e)
{
	uintptr_t mask = ~(uintptr_t)(page - 1), p, end;
	uint8_t sink = 0;
	int i;

	for (i = 0; i < 2; i++) {
		int32_t lo = e->elo[i] < 0 ? 0 : e->elo[i];

		if (!e->en[i] || lo >= e->elo[i] + e->en[i])
			continue;
		p = (uintptr_t)e->data + 2 * (uintptr_t)lo;
		end = (uintptr_t)e->data + 2 * (uintptr_t)(e->elo[i] + e->en[i]);
		for (; p < end; p = (p & mask) + page)
			sink += *(const volatile uint8_t *)p;
		sink += *(const volatile uint8_t *)(end - 1);
	}
	(void)sink;
}

int speq_synth_add_v2(struct speq_synth *s, unsigned vi, unsigned b, struct speq_synth_voice *v)
{
	struct round *r = s ? s->cur : NULL;
	struct synth_ent *e;
	uint64_t incr, idx0, frac, last, span, hi_cap;
	unsigned int limit, i;
	int has_looped, count, spu_ok = 1;
	int64_t lo, hi;
	uint64_t phase;
	double pinc;

	if (!r || r->state != R_OPEN || vi >= r->nv || b >= r->K || !v)
		return -EINVAL;
	if (v->interp > SPEQ_SYNTH_CUBIC || !v->data || (v->data & 1)) {
		if (!(r->vflag[vi] & V_HAS))
			return -EINVAL;
		v->interp = SPEQ_SYNTH_CUBIC;	/* can't change midway: fluidsynth's default */
	}
	e = &r->ents[vi * r->K + b];
	memset(e, 0, sizeof(*e));
	pinc = v->phase_incr;
	if (!(pinc >= 0))
		pinc = 1;
	if (pinc > 65536)
		pinc = 65536;	/* no sound comes out of that anyway */
	incr = sk_phase_float(pinc);
	if (!incr)
		incr = 1;	/* compute_interpolation_steps would divide by zero */
	has_looped = v->has_looped;
	count = sk_control(v->interp, v->looping, v->phase, incr, v->start, v->end, v->loopstart,
			   v->loopend, &has_looped, &phase, NULL, NULL);
	if (count < 0)
		count = 0;	/* stuck: fluidsynth would hang; the voice ends */

	e->data = v->data;
	e->phase = v->phase;
	e->incr = incr;
	e->start = v->start;
	e->end = v->end;
	e->loopstart = v->loopstart;
	e->loopend = v->loopend;
	e->interp = v->interp;
	e->looping = v->looping;
	e->has_looped = v->has_looped;
	e->count = count;
	for (i = 0; i < SPEQ_SYNTH_BUFS; i++) {
		e->line[i] = v->line[i] >= 0 && v->line[i] < (int)s->p.lines ? v->line[i] : -1;
		e->cur[i] = v->cur_amp[i];
		e->tgt[i] = v->tgt_amp[i];
	}
	filt_in(&e->custom, &v->custom, 0);
	filt_in(&e->res, &v->res, 1);

	if (count) {
		/* the special points the interpolation reads, as fluidsynth reads them */
		limit = v->looping ? v->loopend - 1 : v->end;
		if (v->interp == SPEQ_SYNTH_LINEAR) {
			e->pt[SYNTH_PT_EP1] = smp(v, v->looping ? v->loopstart : v->end);
		} else if (v->interp == SPEQ_SYNTH_CUBIC) {
			e->pt[SYNTH_PT_SP] = smp(v, v->has_looped ? v->loopend - 1 : v->start);
			if (v->looping) {
				e->pt[SYNTH_PT_EP1] = smp(v, v->loopstart);
				e->pt[SYNTH_PT_EP2] = smp(v, v->loopstart + 1);
				e->pt[SYNTH_PT_LE1] = smp(v, v->loopend - 1);
			} else {
				e->pt[SYNTH_PT_EP1] = e->pt[SYNTH_PT_EP2] = smp(v, v->end);
			}
		}
		/* window 0: from the sample before the phase to 3 past the last one the block
		 * reaches, not past the special points (limit + 2; none reads limit + 1) */
		hi_cap = (uint64_t)limit + (v->interp == SPEQ_SYNTH_NONE ? 1 : 2);
		idx0 = v->phase >> 32;
		frac = v->phase & 0xffffffffull;
		last = idx0 + ((frac + 63 * incr) >> 32);
		lo = (int64_t)idx0 - 1;
		hi = (int64_t)(last + 3 < hi_cap ? last + 3 : hi_cap);
		if (lo <= hi && idx0 <= hi_cap) {
			e->elo[0] = lo;
			e->en[0] = hi - lo + 1;
		}
		/* window 1: after a wrap the phase is at loopstart or up to a step past it; a phase
		 * beyond the loop's end wraps anywhere into it: the whole loop */
		if (v->looping && last + 3 > limit) {
			span = ((frac + 64 * incr) >> 32) + 4;
			lo = (int64_t)v->loopstart - 1;
			hi = idx0 > (uint64_t)limit + 1 || v->loopstart + span >= hi_cap ?
			     (int64_t)hi_cap : (int64_t)(v->loopstart + span);
			if (lo <= hi) {
				e->elo[1] = lo;
				e->en[1] = hi - lo + 1;
			}
		}
		for (i = 0; i < 2; i++) {
			if (e->en[i] > SYNTH_WIN_MAX)
				spu_ok = 0;
			if (e->en[i] > s->ext_need)
				s->ext_need = e->en[i];
		}
		if (plan_tab(&v->custom, count, s->sincos_n, &e->tab_first[0], &e->tab_n[0]) ||
		    plan_tab(&v->res, count, s->sincos_n, &e->tab_first[1], &e->tab_n[1]))
			spu_ok = 0;
		if (!spu_ok)
			r->vflag[vi] |= V_PPE;
		if (r->vflag[vi] & V_PPE) {
			e->tab_first[0] = e->tab_first[1] = 0;	/* the PPE reads the whole table */
			e->tab_n[0] = e->tab_n[1] = s->sincos_n;
		} else if (s->q) {
			touch(e);
		}
		e->flags = 0;
		r->nents++;
	} else {
		e->flags = SYNTH_ENT_SKIP;
	}
	if (!(r->vflag[vi] & V_HAS)) {
		float *h = r->hist_in + vi * 4;

		h[0] = (float)v->custom.hist1;
		h[1] = (float)v->custom.hist2;
		h[2] = (float)v->res.hist1;
		h[3] = (float)v->res.hist2;
		r->vflag[vi] |= V_HAS;
	}

	/* the voice after the block, but for the histories */
	v->count = count;
	v->phase = phase;
	v->has_looped = has_looped;
	sk_filter_end(&v->custom, count, 0, s->sincos, s->sincos_n);
	sk_filter_end(&v->res, count, 1, s->sincos, s->sincos_n);
	if (count)
		for (i = 0; i < SPEQ_SYNTH_BUFS; i++)
			if (e->line[i] >= 0)
				v->cur_amp[i] = v->tgt_amp[i];
	return 0;
}

/* Voice slot vi's blocks of the round on this thread (all, or only an SPU voice's), into sum,
 * its histories from hist_in into hist */
static void ppe_voice(struct speq_synth *s, struct round *r, unsigned vi)
{
	const struct sk_tabs t = { s->tabs->cubic, s->tabs->linear, (int32_t)s->sincos_n };
	float buf[SK_BUF] __attribute__((aligned(16))), *h = r->hist + vi * 4;
	unsigned b;
	int i;

	memcpy(h, r->hist_in + vi * 4, 4 * sizeof(float));
	for (b = 0; b < r->K; b++) {
		struct synth_ent *e = &r->ents[vi * r->K + b];
		struct sk_reads rd;

		if (e->flags & SYNTH_ENT_SKIP)
			continue;
		for (i = 0; i < 2; i++) {
			rd.win[i] = (const int16_t *)(uintptr_t)e->data + (e->elo[i] < 0 ? 0 : e->elo[i]);
			rd.tab[i] = s->sincos + e->tab_first[i];
			rd.ext[i] = s->ext[i];
		}
		i = sk_entry(e, &rd, h, buf, r->sum + b * s->p.lines * SK_BUF, &t);
		if (i) {	/* can't happen: the control is the same code, the plan covers it */
			if (debug)
				fprintf(stderr, "speq: synth %p: voice %u block %u not rendered: %d "
					"(interp %d looping %d phase %llx incr %llx elo %d/%d en %d/%d)\n",
					(void *)s, vi, b, i, e->interp, e->looping,
					(unsigned long long)e->phase, (unsigned long long)e->incr,
					e->elo[0], e->elo[1], e->en[0], e->en[1]);
			continue;
		}
		s->st.ents_ppe++;
	}
}

static void fail(struct speq_synth *s)
{
	if (++s->fails >= MAX_FAILS) {
		s->fails = 0;
		s->backoff_until = spq_now_us() + BACKOFF_MS * 1000.0;
		s->st.backoffs++;
		if (debug)
			fprintf(stderr, "speq: synth %p: %d rounds in a row failed on the SPEs, the PPE "
				"for %d ms\n", (void *)s, MAX_FAILS, BACKOFF_MS);
	}
}

/* Jobs of whole voices in [0, nv), no voice the PPE's, each at most SYNTH_JOB_ENTS entries,
 * split so every worker gets some */
static unsigned make_jobs(struct speq_synth *s, struct round *r)
{
	unsigned v, v0 = 0, first = r->nv, end = 0, vj, nw, per, nj = 0, b;

	for (v = 0; v < r->nv; v++)
		if ((r->vflag[v] & V_HAS) && !(r->vflag[v] & V_PPE)) {
			if (first == r->nv)
				first = v;
			end = v + 1;
		}
	if (first >= end)
		return 0;
	for (v = 0; v < r->nv; v++)
		if (r->vflag[v] & V_PPE)
			for (b = 0; b < r->K; b++)
				r->ents[v * r->K + b].flags |= SYNTH_ENT_PPE;
	vj = SYNTH_JOB_ENTS / r->K;
	nw = speq_nworkers(s->q);
	per = (end - first + nw - 1) / nw;
	if (per < vj)
		vj = per;
	if (!vj)
		vj = 1;
	for (v0 = first; v0 < end && nj < MAX_JOBS; v0 += vj) {
		unsigned n = end - v0 < vj ? end - v0 : vj;
		struct speq_job *j = &s->jobs[nj];

		r->job[nj].v0 = v0;
		r->job[nj].n = n;
		memset(j, 0, sizeof(*j));
		j->op = SYNTH_OP_ROUND;
		j->arg[SYNTH_ARG_ENTS] = (uintptr_t)&r->ents[v0 * r->K];
		j->arg[SYNTH_ARG_VOICES] = n;
		j->arg[SYNTH_ARG_BLOCKS] = r->K;
		j->arg[SYNTH_ARG_OUT] = (uintptr_t)(r->jout + (size_t)nj * SYNTH_OUT_MAX * SK_BUF);
		j->arg[SYNTH_ARG_LINES] = s->p.lines;
		j->arg[SYNTH_ARG_HIST] = (uintptr_t)(r->hist_in + v0 * 4);
		j->arg[SYNTH_ARG_HIST_OUT] = (uintptr_t)(r->hist_out + v0 * 4);
		j->arg[SYNTH_ARG_CUBIC] = (uintptr_t)s->tabs->cubic;
		j->arg[SYNTH_ARG_LINEAR] = (uintptr_t)s->tabs->linear;
		j->arg[SYNTH_ARG_SINCOS] = (uintptr_t)s->sincos;
		j->arg[SYNTH_ARG_SINCOS_N] = s->sincos_n;
		nj++;
	}
	if (v0 < end) {	/* can't happen: begin chose K for MAX_JOBS */
		for (v = v0; v < end; v++)
			r->vflag[v] |= V_PPE;
	}
	return nj;
}

int speq_synth_render_v2(struct speq_synth *s, const float **out, const float **hist)
{
	struct round *r = s ? s->cur : NULL;
	size_t len;
	double t0, dt;
	unsigned v, j, k, nj = 0, spu_ents = 0;
	int ret = 0, queued = 0;

	if (!r || r->state != R_OPEN || !out || !hist)
		return -EINVAL;
	s->cur = NULL;
	s->st.rounds++;
	len = (size_t)r->K * s->p.lines * SK_BUF;
	memset(r->sum, 0, len * sizeof(float));
	*out = r->sum;
	*hist = r->hist;
	if (ext_reserve(s, s->ext_need + 8)) {
		/* no scratch for the PPE: nothing renders, the voices are silent this round */
		r->state = R_FREE;
		return 0;
	}
	if (!r->nents) {
		r->state = R_FREE;
		for (v = 0; v < r->nv; v++)
			memcpy(r->hist + v * 4, r->hist_in + v * 4, 4 * sizeof(float));
		return 0;
	}
	if (s->q) {
		nj = make_jobs(s, r);
		for (v = 0; v < r->nv; v++)
			if ((r->vflag[v] & V_HAS) && !(r->vflag[v] & V_PPE))
				for (k = 0; k < r->K; k++)
					spu_ents += !(r->ents[v * r->K + k].flags & SYNTH_ENT_SKIP);
		r->njobs = nj;
		if (nj) {
			memset(&r->done, 0, sizeof(r->done));
			ret = speq_submit(s->q, s->jobs, nj, &r->done);
			if (ret) {
				if (ret == -EIO || __atomic_load_n(&s->q->dead, __ATOMIC_ACQUIRE))
					queue_broke();
				/* jobs it queued may still run: the round is theirs until done */
				if (speq_wait_timeout(s->q, &r->done, 0, SPEQ_WAIT_NOSPIN) == -ETIMEDOUT)
					r->state = R_LOST;
				s->st.errors++;
				fail(s);
			} else {
				queued = 1;
				s->st.jobs += nj;
			}
		}
	}
	/* the PPE's voices (or every voice, without SPEs) while the SPEs work */
	for (v = 0; v < r->nv; v++)
		if ((r->vflag[v] & V_HAS) && (!queued || (r->vflag[v] & V_PPE)))
			ppe_voice(s, r, v);
		else
			memcpy(r->hist + v * 4, r->hist_in + v * 4, 4 * sizeof(float));
	if (!queued) {
		if (r->state == R_OPEN)
			r->state = R_FREE;
		if (!s->q || !nj)
			s->st.ppe++;
		else
			s->st.redone++;
		maybe_print(s);
		return 0;
	}
	t0 = spq_now_us();
	ret = speq_wait_timeout(s->q, &r->done, timeout_us + ent_us * (long)spu_ents,
				spin_us ? 0 : SPEQ_WAIT_NOSPIN);
	dt = spq_now_us() - t0;
	if (ret) {
		/* late or failed: the SPU's voices again here; the round stays the jobs' if late */
		if (debug && ret != -ETIMEDOUT)
			fprintf(stderr, "speq: synth %p: a round failed on the SPEs: %d\n", (void *)s, ret);
		if (ret == -ETIMEDOUT) {
			r->state = R_LOST;
			s->st.timeouts++;
		} else {
			r->state = R_FREE;
			if (__atomic_load_n(&s->q->dead, __ATOMIC_ACQUIRE))
				queue_broke();
			s->st.errors++;
		}
		fail(s);
		for (v = 0; v < r->nv; v++)
			if ((r->vflag[v] & V_HAS) && !(r->vflag[v] & V_PPE))
				ppe_voice(s, r, v);
		s->st.redone++;
		maybe_print(s);
		return 0;
	}
	r->state = R_FREE;
	for (j = 0; j < nj; j++) {
		const float *o = r->jout + (size_t)j * SYNTH_OUT_MAX * SK_BUF;

		for (k = 0; k < len; k++)
			r->sum[k] += o[k];
		/* the SPU's voices' histories (the PPE's voices in the range have theirs) */
		for (v = r->job[j].v0; v < r->job[j].v0 + r->job[j].n; v++)
			if ((r->vflag[v] & V_HAS) && !(r->vflag[v] & V_PPE))
				memcpy(r->hist + v * 4, r->hist_out + v * 4, 4 * sizeof(float));
	}
	s->fails = 0;
	s->st.spu++;
	s->st.ents_spu += spu_ents;
	s->st.wait_us += dt;
	if (dt > s->st.wait_max)
		s->st.wait_max = dt;
	maybe_print(s);
	return 0;
}

void speq_synth_close_v2(struct speq_synth *s)
{
	unsigned i;

	if (!s)
		return;
	for (i = 0; i < NROUND; i++) {
		struct round *r = s->r[i];

		if (r && r->state == R_LOST &&
		    speq_wait_timeout(s->q, &r->done, 100000, SPEQ_WAIT_NOSPIN) == -ETIMEDOUT) {
			/* its jobs still run and write this memory: leave it */
			if (debug)
				fprintf(stderr, "speq: synth %p: a round's jobs don't end; leaked\n",
					(void *)s);
			return;
		}
	}
	if (debug)
		print_stats(s, "closed");
	free_synth(s);
}
