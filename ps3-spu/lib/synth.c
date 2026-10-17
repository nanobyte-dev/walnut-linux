/* speq_synth_*_v1: fluidsynth's voice DSP on the SPEs (synth.spu), called by fluidsynth's mixer
 * (through dlopen) one block of voices at a time. The policy lives here, so tuning never needs
 * a fluidsynth rebuild.
 *
 * Modes (SPEQ_SYNTH): auto (default) and spu use the process's synth queue: SPEQ_SYNTH_SPES
 * workers (1: a deadline queue, inside the SPEs bulk queues leave, speq.h SPEQ_OPEN_BULK; a
 * block of 64 voices should take ~0.25 ms of one SPE, an estimate, against its 1.45 ms) running synth.spu, opened by the first speq_synth_open_v1 (a synth's creation, not
 * its audio callback) and never closed; its workers park after 200 ms idle (music stopped) and
 * free their contexts after a minute. auto takes blocks of at least SPEQ_SYNTH_MIN voices (4:
 * below that the PPE renders them faster than a round trip costs), spu every block. ppe runs
 * the same DSP code (synth-kern.h) on the calling thread: the hook's whole path without SPEs.
 * off: speq_synth_open_v1 fails and fluidsynth stays stock.
 *
 * A block: begin takes one of NBLK blocks (voices, output slots, a done word); submit plans each
 * voice (the sample windows and sin/cos slices its DSP reads, the sample points it needs),
 * touches the windows' pages (a DMA into a page that isn't resident costs 40-87 us on the
 * worker's thread, and a swapped-out SoundFont page far more), cuts the voices into jobs of
 * SPEQ_SYNTH_JOB (64) and queues them; wait sleeps on the done word (no spin by default: the
 * thread shares the PPE with the game) for at most SPEQ_SYNTH_TIMEOUT_US (2 ms against a ~0.1
 * ms job and a 1.45 ms block period at 44.1 kHz), then sums the jobs' lines. A block that timed
 * out is abandoned to its job (taken again once its done word completes); its voices are
 * rendered by fluidsynth. MAX_FAILS failed blocks in a row put the SPEs aside for BACKOFF_MS; a
 * queue whose workers all broke, for good. */
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
#define NBLK		3
#define MIN_VOICES	4
#define SPES		1
#define JOB_VOICES	64
#define TIMEOUT_US	2000
#define MAX_FAILS	3
#define BACKOFF_MS	5000
#define IDLE_US		200000	/* workers stay resident between audio callbacks */
#define FREE_MS		60000
#define STATS_S		10

enum { M_OFF, M_PPE, M_SPU, M_AUTO };
enum { B_FREE, B_TAKEN, B_QUEUED, B_DONE, B_LOST };

static pthread_once_t once = PTHREAD_ONCE_INIT;
static int mode = M_AUTO, debug;
static unsigned min_voices = MIN_VOICES, spes = SPES, job_voices = JOB_VOICES;
static long timeout_us = TIMEOUT_US;
static int spin_us;
static long page;

static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
static struct speq *queue;	/* set once, never closed */
static int qerr;		/* the open's error, or -EIO once the queue broke */

struct blk {
	struct speq_synth_block pub;	/* first: the caller's pointer */
	struct speq_done done;
	double *slot;			/* maxjobs x lines x 64 */
	unsigned njobs;
	int state;
};

struct speq_synth {
	struct speq_synth_params p;
	struct speq *q;
	struct blk *blk[NBLK];
	struct speq_job *jobs;		/* maxjobs, for submit */
	struct {
		double cubic[SYNTH_INTERP_ROWS * 4], linear[SYNTH_INTERP_ROWS * 2];
	} *tables;
	unsigned next, maxjobs, job;	/* job: voices per job, as at open */
	int fails;
	double backoff_until, last_print, t_open;
	struct {
		unsigned long blocks, spu, ppe, small, voices, refused, jobs, timeouts, errors,
			      nofree, backoffs;
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
	job_voices = env_u("SPEQ_SYNTH_JOB", JOB_VOICES);
	if (!job_voices || job_voices > SYNTH_JOB_MAX)
		job_voices = SYNTH_JOB_MAX;
	timeout_us = env_u("SPEQ_SYNTH_TIMEOUT_US", TIMEOUT_US);
	spin_us = env_u("SPEQ_SYNTH_SPIN_US", 0);
	page = sysconf(_SC_PAGESIZE);
	if (page <= 0)
		page = 4096;
}

void spq_synth_tune(int m, long timeout, int job, int min)
{
	pthread_once(&once, init);
	if (m >= M_OFF && m <= M_AUTO)
		mode = m;
	if (timeout >= 0)
		timeout_us = timeout;
	if (job > 0)
		job_voices = job > SYNTH_JOB_MAX ? SYNTH_JOB_MAX : job;
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
	fprintf(stderr, "speq: synth %p %s: %lu blocks: %lu spu (%lu voices, %lu jobs, %.0f us mean "
		"wait, %.0f max), %lu ppe, %lu small, %lu voices refused, %lu timeouts, %lu errors, "
		"%lu with no free block, %lu backoffs\n", (void *)s, when, s->st.blocks, s->st.spu,
		s->st.voices, s->st.jobs, s->st.spu ? s->st.wait_us / s->st.spu : 0, s->st.wait_max,
		s->st.ppe, s->st.small, s->st.refused, s->st.timeouts, s->st.errors, s->st.nofree,
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

static void free_synth(struct speq_synth *s)
{
	unsigned i;

	for (i = 0; i < NBLK; i++)
		if (s->blk[i]) {
			free(s->blk[i]->pub.voice);
			free(s->blk[i]->slot);
			free(s->blk[i]);
		}
	free(s->jobs);
	free(s->tables);
	free(s);
}

int speq_synth_open_v1(struct speq_synth **sp, const struct speq_synth_params *p)
{
	struct speq_synth *s;
	unsigned i, cap;
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
	/* the tables, 16-aligned for the SPEs' DMA (fluidsynth's are only 8-aligned) */
	if (posix_memalign((void **)&s->tables, SPEQ_LINE, sizeof(*s->tables))) {
		s->tables = NULL;
		free(s);
		return -ENOMEM;
	}
	memcpy(s->tables->cubic, p->cubic, sizeof(s->tables->cubic));
	memcpy(s->tables->linear, p->linear, sizeof(s->tables->linear));
	s->p.cubic = s->tables->cubic;
	s->p.linear = s->tables->linear;
	if (mode != M_PPE) {
		s->q = get_queue(&err);
		if (!s->q) {
			free_synth(s);
			return err;
		}
	}
	cap = p->max_voices ? p->max_voices : 256;
	if (cap > 256)
		cap = 256;
	s->job = job_voices;
	s->maxjobs = (cap + s->job - 1) / s->job;
	if (posix_memalign((void **)&s->jobs, SPEQ_LINE, s->maxjobs * sizeof(struct speq_job))) {
		s->jobs = NULL;
		free_synth(s);
		return -ENOMEM;
	}
	for (i = 0; i < NBLK; i++) {
		struct blk *b;
		void *v = NULL, *slot = NULL;
		size_t vb = (size_t)cap * sizeof(struct speq_synth_voice);
		size_t sb = (size_t)s->maxjobs * p->lines * SK_BUFSIZE * sizeof(double);

		if (posix_memalign((void **)&b, SPEQ_LINE, sizeof(*b))) {
			free_synth(s);
			return -ENOMEM;
		}
		memset(b, 0, sizeof(*b));
		s->blk[i] = b;
		if (posix_memalign(&v, SPEQ_LINE, vb) || posix_memalign(&slot, SPEQ_LINE, sb)) {
			free(v);
			free_synth(s);
			return -ENOMEM;
		}
		memset(v, 0, vb);	/* touched: no fault on the workers' threads */
		memset(slot, 0, sb);
		b->pub.voice = v;
		b->pub.cap = cap;
		b->pub.lines = p->lines;
		b->slot = slot;
	}
	s->t_open = s->last_print = spq_now_us();
	*sp = s;
	return 0;
}

struct speq_synth_block *speq_synth_begin_v1(struct speq_synth *s)
{
	unsigned k;

	if (!s)
		return NULL;
	if (s->q && !queue_ok())
		return NULL;
	if (s->backoff_until && spq_now_us() < s->backoff_until)
		return NULL;
	s->backoff_until = 0;
	for (k = 0; k < NBLK; k++) {
		struct blk *b = s->blk[(s->next + k) % NBLK];

		if (b->state == B_LOST &&
		    speq_wait_timeout(s->q, &b->done, 0, SPEQ_WAIT_NOSPIN) != -ETIMEDOUT)
			b->state = B_FREE;
		if (b->state == B_FREE) {
			b->state = B_TAKEN;
			b->pub.out = NULL;
			s->next = (s->next + k + 1) % NBLK;
			return &b->pub;
		}
	}
	s->st.nofree++;
	return NULL;
}

/* fluid_rvoice_get_sample16, read by the PPE */
static int32_t smp(const struct speq_synth_voice *v, unsigned int idx)
{
	const int16_t *d = (const int16_t *)(uintptr_t)v->data;

	return (int32_t)((uint32_t)d[idx] << 8);
}

/* A filter's sin/cos slice for this block: the entries its coefficients are recomputed from
 * while it sweeps (none when it doesn't); 0, or -1 when it's too wide or not computable */
static int plan_tab(const struct speq_synth_filter *f, const struct speq_synth_block *b,
		    uint32_t *first, uint32_t *n)
{
	double a, z, lo, hi;
	int steps;

	*first = *n = 0;
	if (f->type == SPEQ_SYNTH_IIR_OFF || f->last_q < SK_Q_MIN ||
	    (f->fres_incr_count <= 0 && f->q_incr_count <= 0))
		return 0;
	if (!b->sincos || !b->sincos_n)
		return -1;
	steps = f->fres_incr_count > SK_BUFSIZE ? SK_BUFSIZE :
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
	if (lo > b->sincos_n - 1)
		lo = b->sincos_n - 1;
	if (hi > b->sincos_n - 1)
		hi = b->sincos_n - 1;
	if (hi - lo + 1 > SYNTH_TAB_MAX)
		return -1;
	*first = lo;
	*n = hi - lo + 1;
	return 0;
}

/* The voice's plan: the sample points its interpolation reads (always), and for the SPEs the
 * windows and slices. Reads only the samples fluidsynth's own code would. 0, or -1: the PPE
 * renders it. */
static int plan_voice(struct speq_synth_voice *v, const struct speq_synth_block *b, int spu)
{
	struct synth_plan *pl = (struct synth_plan *)v->priv;
	uint64_t incr, idx0, frac, last, hi_read, lo, hi, span;
	unsigned int limit;

	memset(pl, 0, sizeof(*pl));
	if (v->interp > SPEQ_SYNTH_CUBIC || !v->data || (v->data & 1) ||
	    !(v->phase_incr >= 0 && v->phase_incr < 1e6))
		return -1;
	incr = sk_phase_float(v->phase_incr);
	if (!incr)
		return -1;	/* compute_interpolation_steps would divide by zero */
	if (v->interp == SPEQ_SYNTH_LINEAR) {
		if (v->looping)
			pl->pt[SYNTH_PT_LS] = smp(v, v->loopstart);
		else
			pl->pt[SYNTH_PT_END] = smp(v, v->end);
	} else if (v->interp == SPEQ_SYNTH_CUBIC) {
		if (!v->has_looped)
			pl->pt[SYNTH_PT_START] = smp(v, v->start);
		if (v->has_looped || v->looping)
			pl->pt[SYNTH_PT_LE1] = smp(v, v->loopend - 1);
		if (v->looping) {
			pl->pt[SYNTH_PT_LS] = smp(v, v->loopstart);
			pl->pt[SYNTH_PT_LS1] = smp(v, v->loopstart + 1);
		} else {
			pl->pt[SYNTH_PT_END] = smp(v, v->end);
		}
	}
	if (!spu)
		return 0;

	/* Window 0: from the sample before the phase to 3 past the last one a block reaches
	 * (the cubic's +2, none's rounding), not past what the DSP reads (limit + 1) */
	limit = v->looping ? v->loopend - 1 : v->end;
	hi_read = (uint64_t)limit + 1;
	idx0 = v->phase >> 32;
	frac = v->phase & 0xffffffffull;
	if (v->looping && idx0 > hi_read)
		return -1;	/* would wrap into the middle of the loop: not planned */
	last = idx0 + ((frac + 63 * incr) >> 32);
	lo = idx0 ? idx0 - 1 : 0;
	hi = last + 3 < hi_read ? last + 3 : hi_read;
	if (lo <= hi) {
		pl->win_first[0] = lo;
		pl->win_n[0] = hi - lo + 1;
	}
	/* Window 1: after a wrap the phase is at loopstart or up to one step past it */
	if (v->looping && last + 3 > limit) {
		span = ((frac + 64 * incr) >> 32) + 4;
		lo = v->loopstart;
		hi = lo + span < hi_read ? lo + span : hi_read;
		if (lo <= hi) {
			pl->win_first[1] = lo;
			pl->win_n[1] = hi - lo + 1;
		}
	}
	if (pl->win_n[0] > SYNTH_WIN_MAX || pl->win_n[1] > SYNTH_WIN_MAX)
		return -1;
	if (plan_tab(&v->custom, b, &pl->tab_first[0], &pl->tab_n[0]) ||
	    plan_tab(&v->res, b, &pl->tab_first[1], &pl->tab_n[1]))
		return -1;
	return 0;
}

/* One load per page of the voice's windows: a swapped-out page comes back on the PPE */
static void touch(const struct speq_synth_voice *v)
{
	const struct synth_plan *pl = (const struct synth_plan *)v->priv;
	uintptr_t mask = ~(uintptr_t)(page - 1), p, end;
	uint8_t sink = 0;
	int i;

	for (i = 0; i < 2; i++) {
		if (!pl->win_n[i])
			continue;
		p = (uintptr_t)v->data + 2 * (uintptr_t)pl->win_first[i];
		end = p + 2 * (uintptr_t)pl->win_n[i];
		for (; p < end; p = (p & mask) + page)
			sink += *(const volatile uint8_t *)p;
		sink += *(const volatile uint8_t *)(end - 1);
	}
	(void)sink;
}

static void fail(struct speq_synth *s)
{
	if (++s->fails >= MAX_FAILS) {
		s->fails = 0;
		s->backoff_until = spq_now_us() + BACKOFF_MS * 1000.0;
		s->st.backoffs++;
		if (debug)
			fprintf(stderr, "speq: synth %p: %d blocks in a row failed on the SPEs, the PPE "
				"for %d ms\n", (void *)s, MAX_FAILS, BACKOFF_MS);
	}
}

/* SPEQ_SYNTH=ppe: the DSP on this thread, into the first slot */
static int run_ppe(struct speq_synth *s, struct blk *b, unsigned n)
{
	struct speq_synth_block *pb = &b->pub;
	double dsp_buf[SK_BUFSIZE];
	struct sk_view sv;
	unsigned i;

	memset(&sv, 0, sizeof(sv));
	sv.n[0] = sv.n[1] = 0xffffffffu;
	sv.t[0] = sv.t[1] = pb->sincos;
	sv.tn[0] = sv.tn[1] = pb->sincos_n;
	sv.tab_size = pb->sincos_n;
	sv.cubic = s->p.cubic;
	sv.linear = s->p.linear;
	memset(b->slot, 0, pb->lines * SK_BUFSIZE * sizeof(double));
	for (i = 0; i < n; i++) {
		struct speq_synth_voice *v = &pb->voice[i];

		if (plan_voice(v, pb, 0)) {
			v->count = SPEQ_SYNTH_PPE;
			s->st.refused++;
			continue;
		}
		sv.w[0] = sv.w[1] = (const int16_t *)(uintptr_t)v->data;
		if (sk_voice(v, &sv, dsp_buf, b->slot, pb->lines))
			return -EIO;
	}
	return 0;
}

int speq_synth_submit_v1(struct speq_synth *s, struct speq_synth_block *pb, unsigned n)
{
	struct blk *b = (struct blk *)pb;
	struct speq_job *jobs = s ? s->jobs : NULL;
	unsigned i, i0, j, nq = 0, nj = 0, cnt;
	int ret;

	if (!s || !pb || b->state != B_TAKEN)
		return -EINVAL;
	s->st.blocks++;
	if (!n || n > pb->cap) {
		b->state = B_FREE;
		return n ? -EINVAL : -ENOTSUP;
	}
	if (!s->q) {	/* SPEQ_SYNTH=ppe */
		ret = run_ppe(s, b, n);
		if (ret) {
			s->st.errors++;
			b->state = B_FREE;
			return ret;
		}
		s->st.ppe++;
		pb->out = b->slot;
		b->state = B_DONE;
		return 0;
	}
	if (mode == M_AUTO && n < min_voices) {
		s->st.small++;
		b->state = B_FREE;
		return -ENOTSUP;
	}
	if (pb->sincos && ((uintptr_t)pb->sincos & 3)) {
		b->state = B_FREE;
		return -EINVAL;
	}
	for (i = 0; i < n; i++) {
		struct speq_synth_voice *v = &pb->voice[i];
		struct synth_plan *pl = (struct synth_plan *)v->priv;

		if (plan_voice(v, pb, 1)) {
			memset(pl, 0, sizeof(*pl));
			pl->skip = 1;
			v->count = SPEQ_SYNTH_PPE;
			s->st.refused++;
			continue;
		}
		v->count = -1;
		touch(v);
		nq++;
	}
	if (!nq) {	/* the caller renders them all; nothing to add */
		b->state = B_DONE;
		return 0;
	}
	memset(jobs, 0, sizeof(jobs[0]) * s->maxjobs);
	for (i0 = 0; i0 < n; i0 += s->job) {
		cnt = n - i0 < s->job ? n - i0 : s->job;
		for (i = 0; i < cnt; i++)
			if (!((struct synth_plan *)pb->voice[i0 + i].priv)->skip)
				break;
		if (i == cnt)
			continue;	/* all refused */
		j = nj++;
		jobs[j].op = SYNTH_OP_BLOCK;
		jobs[j].arg[SYNTH_ARG_VOICES] = (uintptr_t)&pb->voice[i0];
		jobs[j].arg[SYNTH_ARG_COUNT] = cnt;
		jobs[j].arg[SYNTH_ARG_OUT] = (uintptr_t)(b->slot + (size_t)j * pb->lines * SK_BUFSIZE);
		jobs[j].arg[SYNTH_ARG_LINES] = pb->lines;
		jobs[j].arg[SYNTH_ARG_CUBIC] = (uintptr_t)s->p.cubic;
		jobs[j].arg[SYNTH_ARG_LINEAR] = (uintptr_t)s->p.linear;
		jobs[j].arg[SYNTH_ARG_SINCOS] = (uintptr_t)pb->sincos;
		jobs[j].arg[SYNTH_ARG_SINCOS_N] = pb->sincos_n ? pb->sincos_n : 1;
	}
	b->njobs = nj;
	memset(&b->done, 0, sizeof(b->done));
	ret = speq_submit(s->q, jobs, nj, &b->done);
	if (ret) {
		/* jobs it queued may still run: the block is theirs until the done word completes */
		b->state = speq_wait_timeout(s->q, &b->done, 0, SPEQ_WAIT_NOSPIN) == -ETIMEDOUT ?
			   B_LOST : B_FREE;
		if (ret == -EIO || __atomic_load_n(&s->q->dead, __ATOMIC_ACQUIRE))
			queue_broke();
		s->st.errors++;
		fail(s);
		return ret;
	}
	s->st.jobs += nj;
	s->st.voices += nq;
	b->state = B_QUEUED;
	return 0;
}

int speq_synth_wait_v1(struct speq_synth *s, struct speq_synth_block *pb)
{
	struct blk *b = (struct blk *)pb;
	size_t len;
	double t0, dt;
	unsigned j, k;
	int ret;

	if (!s || !pb)
		return -EINVAL;
	if (b->state == B_DONE) {
		b->state = B_FREE;	/* out stays valid until the next begin takes it */
		maybe_print(s);
		return 0;
	}
	if (b->state != B_QUEUED)
		return -EINVAL;
	t0 = spq_now_us();
	ret = speq_wait_timeout(s->q, &b->done, timeout_us, spin_us ? 0 : SPEQ_WAIT_NOSPIN);
	dt = spq_now_us() - t0;
	if (ret == -ETIMEDOUT) {
		b->state = B_LOST;
		s->st.timeouts++;
		fail(s);
		maybe_print(s);
		return ret;
	}
	b->state = B_FREE;
	if (ret) {
		if (__atomic_load_n(&s->q->dead, __ATOMIC_ACQUIRE))
			queue_broke();
		s->st.errors++;
		fail(s);
		maybe_print(s);
		return ret;
	}
	/* the jobs' lines, summed in job order into the first */
	len = (size_t)pb->lines * SK_BUFSIZE;
	for (j = 1; j < b->njobs; j++)
		for (k = 0; k < len; k++)
			b->slot[k] += b->slot[j * len + k];
	pb->out = b->slot;
	s->fails = 0;
	s->st.spu++;
	s->st.wait_us += dt;
	if (dt > s->st.wait_max)
		s->st.wait_max = dt;
	maybe_print(s);
	return 0;
}

void speq_synth_close_v1(struct speq_synth *s)
{
	unsigned i;

	if (!s)
		return;
	for (i = 0; i < NBLK; i++) {
		struct blk *b = s->blk[i];

		if (b && (b->state == B_LOST || b->state == B_QUEUED) &&
		    speq_wait_timeout(s->q, &b->done, 100000, SPEQ_WAIT_NOSPIN) == -ETIMEDOUT) {
			/* its job still runs and writes this memory: leave it */
			if (debug)
				fprintf(stderr, "speq: synth %p: a block's job doesn't end; leaked\n",
					(void *)s);
			return;
		}
	}
	if (debug)
		print_stats(s, "closed");
	free_synth(s);
}
