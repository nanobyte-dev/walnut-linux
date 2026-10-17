/* speq_skin_*_v1: a game's CPU skinning on the SPEs (deadline.spu's skinning op), called by the game (through
 * dlopen) once per skinned vertex buffer. The policy lives here, so tuning never needs a game
 * rebuild.
 *
 * Modes (SPEQ_SKIN): auto (default) and spu use the process's deadline pool (lib/pool.c: two
 * workers running deadline.spu, shared with fluidsynth's synth and every other latency-bound
 * service, jobs taken by deadline; with the DXT bulk queue that makes 4 + 2 = 6 contexts on the
 * 6 SPEs, never time-sliced), opened by the first speq_skin_open_v1 or another service and never
 * closed; its workers stay resident 200 ms past their last job (frames come every 15-60 ms) and
 * free their contexts after a minute. A mesh's jobs go to at most SPEQ_SKIN_SPES of its workers
 * (default all). auto takes batches of
 * at least SPEQ_SKIN_MIN vertices (256: below that a round trip costs about what the PPE
 * spends), spu every batch. ppe runs the same kernel (skin-kern.h) on the calling thread, into
 * libspeq's buffer: the hook's whole path without SPEs. off: speq_skin_open_v1 fails.
 *
 * A batch: begin takes the next free one of NBATCH (its output buffer grown to the size asked,
 * every page touched). add checks the mesh (skin-kern.h sk_check), writes its descriptor, touches
 * its inputs' pages (an SPU DMA from a page that isn't resident is served on the worker's thread,
 * 40-87 us a page, far more for a swapped one) and queues its jobs at once: up to one per worker,
 * at least JOB_VERTS vertices each, at most JOB_MAX_VERTS (a running job isn't preempted: the
 * pool's other services wait for it at most that long, estimate 0.1 ms), their deadline the
 * time end would give up on them. end spins SPEQ_SKIN_SPIN_US (300; the jobs take tens of us,
 * a futex sleep adds ~62) then sleeps on the done word, for at most SPEQ_SKIN_TIMEOUT_US + 50 ns
 * per vertex. A late batch is left to its jobs (taken again once its done word completes; its
 * output is never handed out) and the game skins it itself; MAX_FAILS late or failed batches in
 * a row put the SPEs aside for BACKOFF_MS, a queue whose workers all broke for good. */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "speq-int.h"
#include "skin-kern.h"

#define NBATCH		4
#define MAX_MESHES	32
#define JOB_VERTS	256
#define JOB_MAX_VERTS	4096	/* ~60-75 cycles a vertex (estimate): ~0.1 ms */
#define MAX_JOBS	32	/* per mesh; past 32 x 4096 vertices the jobs grow */
#define MIN_VERTS	256
#define SPES		0	/* all of the pool's workers */
#define TIMEOUT_US	2000
#define VERT_NS		50	/* added to the timeout per vertex */
#define SPIN_US		300
#define MAX_FAILS	3
#define BACKOFF_MS	5000
#define STATS_S		10
#define MAX_OUT		(64u << 20)

enum { M_OFF, M_PPE, M_SPU, M_AUTO };
enum { B_FREE, B_OPEN, B_LOST };

static pthread_once_t once = PTHREAD_ONCE_INIT;
static int mode = M_AUTO, debug;
static unsigned min_verts = MIN_VERTS, spes = SPES;
static long timeout_us = TIMEOUT_US, vert_ns = VERT_NS;
static int spin_us = SPIN_US;
static long page;

int spq_skin_fault;

struct batch {
	struct speq_done done;
	struct skin_desc desc[MAX_MESHES];
	uint8_t *out;
	size_t cap, bytes;
	unsigned nmesh, njobs;
	unsigned long nverts;	/* on the SPEs */
	int state, err, queued;
};

struct speq_skin {
	struct speq *q;
	struct batch *b[NBATCH];
	struct batch *cur;
	unsigned next;
	int fails;
	double backoff_until, last_print;
	struct spq_skin_stats st;
};

static unsigned env_u(const char *name, unsigned def)
{
	const char *s = getenv(name);

	return s && *s ? (unsigned)strtoul(s, NULL, 0) : def;
}

static void init(void)
{
	const char *s = getenv("SPEQ_SKIN"), *d = getenv("SPEQ_DEBUG");

	if (!s || !*s || !strcmp(s, "auto"))
		mode = M_AUTO;
	else if (!strcmp(s, "off"))
		mode = M_OFF;
	else if (!strcmp(s, "ppe"))
		mode = M_PPE;
	else if (!strcmp(s, "spu"))
		mode = M_SPU;
	else
		fprintf(stderr, "speq: SPEQ_SKIN=%s unknown, using auto\n", s);
	debug = d && *d && strcmp(d, "0");
	min_verts = env_u("SPEQ_SKIN_MIN", MIN_VERTS);
	spes = env_u("SPEQ_SKIN_SPES", SPES);
	timeout_us = env_u("SPEQ_SKIN_TIMEOUT_US", TIMEOUT_US);
	spin_us = env_u("SPEQ_SKIN_SPIN_US", SPIN_US);
	page = sysconf(_SC_PAGESIZE);
	if (page <= 0)
		page = 4096;
}

void spq_skin_tune(int m, long timeout, int per_vert_ns, int min)
{
	pthread_once(&once, init);
	if (m >= M_OFF && m <= M_AUTO)
		mode = m;
	if (timeout >= 0)
		timeout_us = timeout;
	if (per_vert_ns >= 0)
		vert_ns = per_vert_ns;
	if (min >= 0)
		min_verts = min;
}

void spq_skin_stats(struct speq_skin *s, struct spq_skin_stats *st)
{
	*st = s->st;
}

static void print_stats(struct speq_skin *s, const char *when)
{
	const struct spq_skin_stats *t = &s->st;

	fprintf(stderr, "speq: skin %p %s: %lu batches: %lu spu (%lu meshes, %lu vertices, %lu jobs, "
		"%.0f us mean wait, %.0f max), %lu ppe (%lu vertices), %lu small, %lu declined; "
		"%lu timeouts, %lu errors, %lu meshes refused, %lu with no free buffer, %lu backoffs\n",
		(void *)s, when, t->batches, t->spu, t->meshes_spu, t->verts_spu, t->jobs,
		t->spu ? t->wait_us / t->spu : 0, t->wait_max, t->ppe, t->verts_ppe, t->small,
		t->declined, t->timeouts, t->errors, t->refused, t->nofree, t->backoffs);
}

static void maybe_print(struct speq_skin *s)
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

static void fail(struct speq_skin *s)
{
	if (++s->fails >= MAX_FAILS) {
		s->fails = 0;
		s->backoff_until = spq_now_us() + BACKOFF_MS * 1000.0;
		s->st.backoffs++;
		if (debug)
			fprintf(stderr, "speq: skin %p: %d batches in a row failed on the SPEs, the game "
				"skins for %d ms\n", (void *)s, MAX_FAILS, BACKOFF_MS);
	}
}

static void free_batch(struct batch *b)
{
	if (!b)
		return;
	free(b->out);
	free(b);
}

static void free_skin(struct speq_skin *s)
{
	unsigned i;

	for (i = 0; i < NBATCH; i++)
		free_batch(s->b[i]);
	free(s);
}

int speq_skin_open_v1(struct speq_skin **sp, unsigned abi, unsigned mesh_size)
{
	struct speq_skin *s;
	unsigned i;
	int err = 0;

	pthread_once(&once, init);
	if (!sp)
		return -EINVAL;
	*sp = NULL;
	if (abi != SPEQ_SKIN_ABI || mesh_size != sizeof(struct speq_skin_mesh))
		return -EPROTO;
	if (mode == M_OFF)
		return -ENOTSUP;
	s = calloc(1, sizeof(*s));
	if (!s)
		return -ENOMEM;
	for (i = 0; i < NBATCH; i++) {
		void *p = NULL;

		if (posix_memalign(&p, SPEQ_LINE, sizeof(struct batch))) {
			free_skin(s);
			return -ENOMEM;
		}
		memset(p, 0, sizeof(struct batch));
		s->b[i] = p;
	}
	if (mode != M_PPE) {
		s->q = spq_pool(&err);
		if (!s->q) {
			free_skin(s);
			return err;
		}
	}
	s->last_print = spq_now_us();
	*sp = s;
	return 0;
}

/* A store per page (our buffer: it may have been swapped out since) */
static void touch_out(uint8_t *p, size_t bytes)
{
	uintptr_t mask = ~(uintptr_t)(page - 1), a = (uintptr_t)p, end = a + bytes;

	for (; a < end; a = (a & mask) + page)
		*(volatile uint8_t *)a = *(volatile uint8_t *)a;
}

/* A load per page of an input */
static void touch_in(uint64_t ea, size_t bytes)
{
	uintptr_t mask = ~(uintptr_t)(page - 1), a = (uintptr_t)ea, end = a + bytes;
	uint8_t sink = 0;

	if (!bytes)
		return;
	for (; a < end; a = (a & mask) + page)
		sink += *(const volatile uint8_t *)a;
	sink += *(const volatile uint8_t *)(end - 1);
	(void)sink;
}

/* An open batch that will not be ended: its jobs keep it until they are done */
static void drop(struct speq_skin *s, struct batch *b)
{
	if (b->queued && speq_wait_timeout(s->q, &b->done, 0, SPEQ_WAIT_NOSPIN) == -ETIMEDOUT)
		b->state = B_LOST;
	else
		b->state = B_FREE;
}

int speq_skin_begin_v1(struct speq_skin *s, uint32_t out_bytes, uint32_t nverts)
{
	struct batch *b = NULL;
	unsigned i, k = 0;

	if (!s || !out_bytes || out_bytes > MAX_OUT)
		return -EINVAL;
	if (s->cur) {	/* begun, never ended */
		drop(s, s->cur);
		s->cur = NULL;
	}
	if (s->q && !spq_pool_ok()) {
		s->st.declined++;
		return -ENOTSUP;
	}
	if (s->backoff_until) {
		if (spq_now_us() < s->backoff_until) {
			s->st.declined++;
			return -ENOTSUP;
		}
		s->backoff_until = 0;
	}
	if (s->q && mode == M_AUTO && nverts < min_verts) {
		s->st.small++;
		return -ENOTSUP;
	}
	for (i = 0; i < NBATCH && !b; i++) {
		struct batch *c = s->b[(s->next + i) % NBATCH];

		if (c->state == B_LOST &&
		    speq_wait_timeout(s->q, &c->done, 0, SPEQ_WAIT_NOSPIN) != -ETIMEDOUT)
			c->state = B_FREE;
		if (c->state == B_FREE) {
			b = c;
			k = (s->next + i) % NBATCH;
		}
	}
	if (!b) {
		s->st.nofree++;
		return -ENOTSUP;
	}
	if (b->cap < (size_t)out_bytes + 32) {
		size_t cap = ((size_t)out_bytes + 32 + page - 1) & ~(size_t)(page - 1);
		void *p = NULL;

		if (posix_memalign(&p, page, cap))
			return -ENOMEM;
		memset(p, 0, cap);
		free(b->out);
		b->out = p;
		b->cap = cap;
	} else {
		touch_out(b->out, out_bytes);
	}
	s->next = k + 1;
	memset(&b->done, 0, sizeof(b->done));
	b->bytes = out_bytes;
	b->nmesh = b->njobs = 0;
	b->nverts = 0;
	b->err = b->queued = 0;
	b->state = B_OPEN;
	s->cur = b;
	s->st.batches++;
	return 0;
}

static int spoil(struct speq_skin *s, struct batch *b, int err)
{
	if (!b->err)
		b->err = err;
	if (err == -EINVAL || err == -ENOTSUP || err == -ENOSPC)
		s->st.refused++;
	return err;
}

int speq_skin_add_v1(struct speq_skin *s, const struct speq_skin_mesh *m)
{
	struct batch *b = s ? s->cur : NULL;
	struct speq_job jobs[MAX_JOBS];
	struct skin_desc *d;
	const struct speq_skin_field *f[SKIN_NF];
	uint32_t n, nw, nj, per, v0, j;
	unsigned i, streams;
	int ret;

	if (!b || !m)
		return -EINVAL;
	if (b->err)
		return b->err;
	if (b->nmesh >= MAX_MESHES)
		return spoil(s, b, -ENOSPC);
	n = m->nverts;
	if ((m->dst_offset & 3) || m->dst_offset > b->bytes ||
	    (uint64_t)n * m->dst_stride > b->bytes - m->dst_offset)
		return spoil(s, b, -EINVAL);
	d = &b->desc[b->nmesh];
	memset(d, 0, sizeof(*d));
	for (i = 0; i < 2; i++) {
		d->pal[i] = (uintptr_t)m->palette[i];
		d->npal[i] = m->npal[i];
		d->src[i] = (uintptr_t)m->src[i];
		d->src_stride[i] = m->src_stride[i];
	}
	d->tmpl = (uintptr_t)m->tmpl;
	d->dst = (uintptr_t)(b->out + m->dst_offset);
	d->dst_stride = m->dst_stride;
	d->nverts = n;
	f[SKIN_F_POS] = &m->pos;
	f[SKIN_F_NORM] = &m->norm;
	f[SKIN_F_TAN] = &m->tan;
	f[SKIN_F_INDEX] = &m->index;
	f[SKIN_F_WEIGHTS] = &m->weights;
	for (i = 0; i < SKIN_NF; i++) {
		d->f_off[i] = f[i]->off;
		d->f_stream[i] = f[i]->stream;
	}
	d->d_off[SKIN_D_POS] = m->dst_pos;
	d->d_off[SKIN_D_NORM] = m->dst_norm;
	d->d_off[SKIN_D_TAN] = m->dst_tan;
	d->palette_fmt = m->palette_fmt;
	d->influence = m->influence;
	d->tan_in = m->tan_in;
	d->tan_out = m->tan_out;
	ret = sk_check(d);
	if (ret)
		return spoil(s, b, ret == -95 ? -ENOTSUP : -EINVAL);
	if (!n)
		return 0;
	b->nmesh++;

	if (!s->q) {	/* ppe: here, now */
		struct sk_pal pl = { m->palette[0], m->palette[1], m->npal[0], m->npal[0] + m->npal[1] };
		uint8_t *o = b->out + m->dst_offset;

		if (m->tmpl)
			memcpy(o, m->tmpl, (size_t)n * m->dst_stride);
		else
			memset(o, 0, (size_t)n * m->dst_stride);
		if (sk_run(d, &pl, m->src[0], m->src[1], o, n))
			return spoil(s, b, -EIO);
		s->st.verts_ppe += n;
		return 0;
	}

	streams = sk_streams(d);
	for (i = 0; i < 2; i++)
		if (streams & (1u << i))
			touch_in(d->src[i], (size_t)n * d->src_stride[i]);
	touch_in(d->tmpl, d->tmpl ? (size_t)n * d->dst_stride : 0);
	for (i = 0; i < 2; i++)
		touch_in(d->pal[i], (size_t)d->npal[i] * (d->palette_fmt == SPEQ_SKIN_DQ ? 32 : 48));
	if (spq_skin_fault > 0 && !--spq_skin_fault)
		d->src[streams & 1 ? 0 : 1] = 0x1000;	/* tests: an unmapped EA, the job faults */

	nw = speq_nworkers(s->q);
	if (spes && nw > spes)
		nw = spes;
	nj = n / JOB_VERTS;
	if (nj > nw)
		nj = nw;
	if (nj < (n + JOB_MAX_VERTS - 1) / JOB_MAX_VERTS)
		nj = (n + JOB_MAX_VERTS - 1) / JOB_MAX_VERTS;
	if (nj > MAX_JOBS)
		nj = MAX_JOBS;
	if (!nj)
		nj = 1;
	per = (n + nj - 1) / nj;
	for (j = 0, v0 = 0; j < nj && v0 < n; j++, v0 += per) {
		memset(&jobs[j], 0, sizeof(jobs[j]));
		jobs[j].op = SKIN_OP_RANGE;
		jobs[j].arg[SKIN_ARG_DESC] = (uintptr_t)d;
		jobs[j].arg[SKIN_ARG_FIRST] = v0;
		jobs[j].arg[SKIN_ARG_COUNT] = n - v0 < per ? n - v0 : per;
	}
	nj = j;
	ret = spq_submit_dl(s->q, jobs, nj, &b->done,
			    spq_deadline(timeout_us + (double)n * vert_ns / 1000));
	if (ret) {
		spq_pool_check(s->q, ret);
		b->queued = 1;	/* some may have gone: end looks */
		s->st.errors++;
		fail(s);
		return spoil(s, b, ret);
	}
	b->queued = 1;
	b->njobs += nj;
	b->nverts += n;
	s->st.jobs += nj;
	return 0;
}

int speq_skin_end_v1(struct speq_skin *s, void **out)
{
	struct batch *b = s ? s->cur : NULL;
	double t0, dt;
	int ret;

	if (!b || !out)
		return -EINVAL;
	*out = NULL;
	s->cur = NULL;
	if (b->err) {
		drop(s, b);
		maybe_print(s);
		return b->err;
	}
	if (!b->queued) {	/* ppe, or nothing to skin */
		b->state = B_FREE;
		if (!s->q)
			s->st.ppe++;
		*out = b->out;
		maybe_print(s);
		return 0;
	}
	t0 = spq_now_us();
	ret = spq_wait_ex(s->q, &b->done, spin_us, timeout_us + (long)(b->nverts * vert_ns / 1000));
	dt = spq_now_us() - t0;
	if (ret == -ETIMEDOUT) {
		b->state = B_LOST;
		s->st.timeouts++;
		fail(s);
		maybe_print(s);
		return -ETIMEDOUT;
	}
	b->state = B_FREE;
	if (ret) {
		if (debug)
			fprintf(stderr, "speq: skin %p: a batch failed on the SPEs: %d\n", (void *)s, ret);
		spq_pool_check(s->q, 0);
		s->st.errors++;
		fail(s);
		maybe_print(s);
		return -EIO;
	}
	s->fails = 0;
	s->st.spu++;
	s->st.meshes_spu += b->nmesh;
	s->st.verts_spu += b->nverts;
	s->st.wait_us += dt;
	if (dt > s->st.wait_max)
		s->st.wait_max = dt;
	*out = b->out;
	maybe_print(s);
	return 0;
}

void speq_skin_close_v1(struct speq_skin *s)
{
	unsigned i;

	if (!s)
		return;
	if (s->cur) {
		drop(s, s->cur);
		s->cur = NULL;
	}
	for (i = 0; i < NBATCH; i++) {
		struct batch *b = s->b[i];

		if (b->state == B_LOST &&
		    speq_wait_timeout(s->q, &b->done, 100000, SPEQ_WAIT_NOSPIN) == -ETIMEDOUT) {
			/* its jobs still run and write this memory: leave it */
			if (debug)
				fprintf(stderr, "speq: skin %p: a batch's jobs don't end; leaked\n",
					(void *)s);
			return;
		}
	}
	if (debug)
		print_stats(s, "closed");
	free_skin(s);
}
