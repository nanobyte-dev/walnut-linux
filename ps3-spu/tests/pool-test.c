/* pool-test: the deadline pool (lib/pool.c) on the fake backend (SPEQ_FAKE=1: every SPU program's
 * ops on host threads, deadline.spu and dxt.spu side by side). One process, as a game with
 * fluidsynth music, SPU skinning and compressed textures:
 * - sharing: DXT's bulk queue opens first (4 workers), then the synth and skinning, which find
 *   one pool of 2 (6 contexts on the 6 "SPEs"); three threads run synth rounds, skinning batches
 *   and DXT1 images at once, each checked against its PPE path (synth within 1e-5 of the peak,
 *   skinning and DXT byte for byte), all on the SPEs;
 * - deadlines: a flood of long jobs in the pool (each a fifth of the synth's timeout, eight
 *   queued at all times) with later deadlines than the synth's rounds: the rounds still finish
 *   on the SPEs in time (they wait for at most a running job); the same flood with earlier
 *   deadlines makes them time out, so the order is what protects them; DXT meanwhile on its own
 *   queue at its own speed;
 * - faults: a skinning job that faults fails its batch (-EIO, the game skins it), the worker
 *   gets a fresh context, the next batch and the synth's rounds go on; the pool keeps 2 workers.
 * Timeouts are scaled to the machine (qemu-user in the package build is many times slower):
 * the synth's timeout is 20 idle rounds' wait, at least 2 ms. Exits nonzero on any failure. */
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dxt-ref.h"
#include "pool-job.h"
#include "speq-int.h"
#include "synth-kern.h"

static int failures;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	fflush(stdout);
	if (!ok)
		failures++;
}

static volatile int stop;

/* --- synth: looping voices over a synthetic sample, the resonant filter on --- */

#define NSAMP	65536
#define TAB_N	12001
#define LINES	4
#define NV	24
#define KMAX	4

static int16_t *data;
static struct sk_sincos sctab[TAB_N];
static double cubic[256 * 4], linear[256 * 2];

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
		data[i] = (int16_t)(12000 * sin(i * 0.031) + 6000 * sin(i * 0.37) + (i * 7919 % 2000) - 1000);
}

static void voices(struct speq_synth_voice *v)
{
	unsigned i;

	memset(v, 0, NV * sizeof(*v));
	for (i = 0; i < NV; i++) {
		struct speq_synth_filter *f = &v[i].res;

		v[i].data = (uintptr_t)data;
		v[i].start = 100 + i * 1500;
		v[i].end = v[i].start + 20000;
		v[i].loopstart = v[i].start + 2000;
		v[i].loopend = v[i].loopstart + 5000 + i * 37;
		v[i].interp = i % 3;
		v[i].looping = 1;
		v[i].phase = (uint64_t)(v[i].start + 10) << 32;
		v[i].phase_incr = 0.5 + 0.11 * (i % 13);
		v[i].line[0] = i % LINES;
		v[i].line[1] = (i + 1) % LINES;
		v[i].line[2] = v[i].line[3] = -1;
		v[i].cur_amp[0] = v[i].tgt_amp[0] = 0.3;
		v[i].cur_amp[1] = v[i].tgt_amp[1] = 0.2;
		v[i].custom.type = SPEQ_SYNTH_IIR_OFF;
		f->type = SPEQ_SYNTH_IIR_LOWPASS;
		f->last_fres = 4000 + 300 * i;
		f->last_q = 1.5;
		f->amp = 1e-5;
		sk_coeffs(sctab, 0, TAB_N, TAB_N, (float)f->last_fres, (float)f->last_q, 1, 0, &f->a1,
			  &f->a2, &f->b02, &f->b1);
	}
}

/* One round of up to KMAX blocks; out gets K x LINES x 64 floats. 0 and *K, or < 0 (declined:
 * the voices are unchanged) */
static int round_on(struct speq_synth *s, struct speq_synth_voice *v, unsigned blocks, float *o,
		    unsigned *K)
{
	const float *out, *hist;
	unsigned b, i;
	int ret;

	*K = speq_synth_begin_v2(s, NV, blocks, sctab, TAB_N);
	if (!*K)
		return -EAGAIN;
	for (b = 0; b < *K; b++)
		for (i = 0; i < NV; i++) {
			ret = speq_synth_add_v2(s, i, b, &v[i]);
			if (ret)
				return ret;
			if (b == 1 && i % 5 == 0)	/* the control part changes a gain */
				v[i].tgt_amp[0] = 0.1 + 0.01 * i;
		}
	if ((ret = speq_synth_render_v2(s, &out, &hist)))
		return ret;
	memcpy(o, out, *K * LINES * SK_BUF * sizeof(float));
	for (i = 0; i < NV; i++) {
		v[i].custom.hist1 = hist[4 * i];
		v[i].custom.hist2 = hist[4 * i + 1];
		v[i].res.hist1 = hist[4 * i + 2];
		v[i].res.hist2 = hist[4 * i + 3];
	}
	return 0;
}

struct synth_run {
	struct speq_synth *ref, *s;	/* ppe and spu */
	struct speq_synth_voice v[NV];
	unsigned long rounds, bad, declined;
	double maxrel;
	unsigned gap_us;		/* between rounds */
};

/* A round on the reference from a copy of the voices, then on s: same output and state */
static void synth_step(struct synth_run *r)
{
	static __thread struct speq_synth_voice vr[NV];
	static __thread float oa[KMAX * LINES * SK_BUF], ob[KMAX * LINES * SK_BUF];
	unsigned Ka, Kb, k;
	float peak = 0;

	memcpy(vr, r->v, sizeof(vr));
	if (round_on(r->ref, vr, KMAX, oa, &Ka)) {
		r->bad++;
		return;
	}
	if (round_on(r->s, r->v, Ka, ob, &Kb) || Kb != Ka) {
		r->declined++;	/* the hook would render it itself: take the reference's */
		memcpy(r->v, vr, sizeof(vr));
		return;
	}
	r->rounds++;
	for (k = 0; k < Ka * LINES * SK_BUF; k++)
		if (fabsf(oa[k]) > peak)
			peak = fabsf(oa[k]);
	for (k = 0; k < Ka * LINES * SK_BUF; k++) {
		double rel = peak > 0 ? fabs(oa[k] - ob[k]) / peak : fabs(ob[k]);

		if (rel > r->maxrel)
			r->maxrel = rel;
	}
	for (k = 0; k < NV; k++)
		if (vr[k].phase != r->v[k].phase || vr[k].has_looped != r->v[k].has_looped)
			r->bad++;
	if (r->maxrel > 1e-5)
		r->bad++;
}

static void *synth_thread(void *arg)
{
	struct synth_run *r = arg;

	while (!stop) {
		synth_step(r);
		if (r->gap_us)
			usleep(r->gap_us);
	}
	return NULL;
}

/* --- skinning: a matrix palette, one index a vertex, positions --- */

#define SKN	3000
#define NPAL	64

struct svert {
	float pos[3];
	int32_t idx;
};

struct skin_run {
	struct speq_skin *ref, *s;
	unsigned long batches, bad, declined, eio;
	unsigned frame;
};

static struct svert sv[SKN] __attribute__((aligned(16)));

static void skin_inputs(void)
{
	unsigned i;

	for (i = 0; i < SKN; i++) {
		sv[i].pos[0] = sinf(i * 0.1f);
		sv[i].pos[1] = cosf(i * 0.07f);
		sv[i].pos[2] = i * 0.001f;
		sv[i].idx = i * 7 % NPAL;
	}
}

static void skin_mesh(struct speq_skin_mesh *m, const float *pal)
{
	memset(m, 0, sizeof(*m));
	m->nverts = SKN;
	m->palette_fmt = SPEQ_SKIN_MAT;
	m->influence = SPEQ_SKIN_IDX_S32;
	m->tan_in = SPEQ_SKIN_TAN_NONE;
	m->palette[0] = pal;
	m->npal[0] = NPAL;
	m->src[0] = sv;
	m->src_stride[0] = sizeof(struct svert);
	m->pos = (struct speq_skin_field){ 0, 0, 0 };
	m->norm = m->tan = m->weights = (struct speq_skin_field){ SPEQ_SKIN_NONE, 0, 0 };
	m->index = (struct speq_skin_field){ 12, 0, 0 };
	m->dst_stride = 16;
	m->dst_pos = 0;
	m->dst_norm = m->dst_tan = SPEQ_SKIN_NONE;
}

/* 0, or the spu handle's error (the reference's must work) */
static int skin_step(struct skin_run *r)
{
	static __thread float pal[NPAL * 12] __attribute__((aligned(16)));
	struct speq_skin_mesh m;
	void *oa, *ob;
	unsigned i;
	int ret;

	r->frame++;
	for (i = 0; i < NPAL * 12; i++)
		pal[i] = (i % 12 == i / 12 % 3 * 5 ? 1.0f : 0.05f) * (1 + 0.01f * (r->frame % 50));
	skin_mesh(&m, pal);
	if (speq_skin_begin_v1(r->ref, SKN * 16, SKN) || speq_skin_add_v1(r->ref, &m) ||
	    speq_skin_end_v1(r->ref, &oa)) {
		r->bad++;
		return 0;
	}
	if ((ret = speq_skin_begin_v1(r->s, SKN * 16, SKN))) {
		r->declined++;
		return ret;
	}
	ret = speq_skin_add_v1(r->s, &m);
	if (!ret)
		ret = speq_skin_end_v1(r->s, &ob);
	else
		speq_skin_end_v1(r->s, &ob);
	if (ret) {
		if (ret == -EIO)
			r->eio++;
		else
			r->declined++;
		return ret;
	}
	r->batches++;
	if (memcmp(oa, ob, SKN * 16))
		r->bad++;
	return 0;
}

static void *skin_thread(void *arg)
{
	struct skin_run *r = arg;

	while (!stop) {
		skin_step(r);
		usleep(500);
	}
	return NULL;
}

/* --- DXT1 on its bulk queue --- */

#define DW	256

struct dxt_run {
	unsigned long images, bad;
	double us;
};

static uint8_t img[DW * DW * 4] __attribute__((aligned(16)));
static uint8_t ref_blocks[DW * DW / 2];

static void dxt_step(struct dxt_run *r)
{
	static __thread uint8_t out[DW * DW / 2] __attribute__((aligned(128)));
	double t0 = spq_now_us();

	if (speq_s3tc_compress_v1(SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 4, img, DW, DW, out, 2 * DW) ||
	    memcmp(out, ref_blocks, sizeof(out)))
		r->bad++;
	r->us += spq_now_us() - t0;
	r->images++;
}

static void *dxt_thread(void *arg)
{
	struct dxt_run *r = arg;

	while (!stop)
		dxt_step(r);
	return NULL;
}

/* --- a flood of long jobs in the pool --- */

#define FLOOD	8

struct flood {
	struct speq *q;
	unsigned spin_us;
	double dl_us;			/* deadline from submit */
	unsigned long jobs;
};

static void *flood_thread(void *arg)
{
	static struct speq_done d[FLOOD] __attribute__((aligned(128)));
	struct flood *f = arg;
	struct speq_job j;
	unsigned i;

	memset(d, 0, sizeof(d));
	memset(&j, 0, sizeof(j));
	j.op = POOL_OP_TEST_SPIN;
	j.arg[0] = f->spin_us;
	while (!stop) {
		for (i = 0; i < FLOOD; i++)
			if (!(__atomic_load_n(&d[i].pending, __ATOMIC_ACQUIRE) & ~SPEQ_WAITERS)) {
				memset(&d[i], 0, sizeof(d[i]));
				if (!spq_submit_dl(f->q, &j, 1, &d[i], spq_deadline(f->dl_us)))
					f->jobs++;
			}
		usleep(f->spin_us / 20 + 1);
	}
	for (i = 0; i < FLOOD; i++)
		spq_wait_ex(f->q, &d[i], 0, 60000000);
	return NULL;
}

static void run_for(double secs, void *(**fn)(void *), void **args, unsigned n)
{
	pthread_t th[8];
	unsigned i;

	stop = 0;
	for (i = 0; i < n; i++)
		pthread_create(&th[i], NULL, fn[i], args[i]);
	usleep(secs * 1e6);
	stop = 1;
	for (i = 0; i < n; i++)
		pthread_join(th[i], NULL);
}

static struct speq_synth *new_synth(int mode)
{
	struct speq_synth_params p = { SPEQ_SYNTH_ABI, sizeof(struct speq_synth_voice), LINES, NV,
				       cubic, linear };
	struct speq_synth *s = NULL;

	spq_synth_tune(mode, -1, -1, -1);
	if (speq_synth_open_v2(&s, &p))
		return NULL;
	return s;
}

static struct speq_skin *new_skin(int mode)
{
	struct speq_skin *s = NULL;

	spq_skin_tune(mode, -1, -1, -1);
	if (speq_skin_open_v1(&s, SPEQ_SKIN_ABI, sizeof(struct speq_skin_mesh)))
		return NULL;
	return s;
}

int main(int argc, char **argv)
{
	struct synth_run sr;
	struct skin_run kr;
	struct dxt_run dr, d0;
	struct spq_synth_stats st;
	struct spq_stats ps0, ps;
	struct flood fl;
	struct speq *pool, *dxtq;
	void *(*fn[4])(void *);
	void *args[4];
	double calib, timeout, secs, dxt_alone;
	unsigned i, k;
	int err;

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (!getenv("SPEQ_FAKE")) {
		fprintf(stderr, "%s: run with SPEQ_FAKE=1\n", argv[0]);
		return 2;
	}
	(void)argc;
	tables();
	skin_inputs();
	for (i = 0; i < sizeof(img); i++)
		img[i] = (uint8_t)(i * 2654435761u >> 13) | (i % 4 == 3 ? 0x80 : 0);
	dxt1_ref_image(img, 4, DW, DW, ref_blocks, 2 * DW, 1);

	/* DXT first, as a game loads textures before music and models: 4 of 6 */
	spq_s3tc_mode(SPQ_S3TC_SPU);
	memset(&d0, 0, sizeof(d0));
	dxt_step(&d0);
	dxtq = spq_s3tc_queue();
	check(dxtq && speq_nworkers(dxtq) == 4 && !d0.bad, "DXT's bulk queue: 4 workers, = reference");

	memset(&sr, 0, sizeof(sr));
	memset(&kr, 0, sizeof(kr));
	sr.ref = new_synth(SPQ_SYNTH_PPE);
	sr.s = new_synth(SPQ_SYNTH_SPU);
	kr.ref = new_skin(SPQ_SKIN_PPE);
	kr.s = new_skin(SPQ_SKIN_SPU);
	pool = spq_pool(&err);
	check(sr.ref && sr.s && kr.ref && kr.s && pool, "synth and skinning open (spu and ppe paths)");
	if (!sr.ref || !sr.s || !kr.ref || !kr.s || !pool)
		return 1;
	check(speq_nworkers(pool) == 2 && speq_nworkers(dxtq) + speq_nworkers(pool) == 6,
	      "one deadline pool of 2 for both: 6 contexts on 6 SPEs");
	voices(sr.v);

	/* calibrate: the idle pool's round trip for a round */
	for (i = 0; i < 20; i++)
		synth_step(&sr);
	spq_synth_stats(sr.s, &st);
	calib = st.spu ? st.wait_us / st.spu : 1000;
	timeout = calib * 20 > 2000 ? calib * 20 : 2000;
	secs = timeout * 400 / 1e6 > 1.5 ? timeout * 400 / 1e6 : 1.5;
	if (secs > 20)
		secs = 20;
	printf("calibration: a round waits %.0f us on the idle pool; synth timeout %.0f us, "
	       "phases of %.1f s\n", calib, timeout, secs);
	spq_synth_tune(-1, (long)timeout, 0, -1);
	spq_skin_tune(-1, (long)timeout * 4, -1, -1);

	/* DXT alone, for its speed */
	memset(&dr, 0, sizeof(dr));
	fn[0] = dxt_thread;
	args[0] = &dr;
	run_for(secs / 3, fn, args, 1);
	dxt_alone = dr.us / dr.images;

	/* 1. all three at once */
	memset(&dr, 0, sizeof(dr));
	sr.gap_us = 300;
	fn[0] = synth_thread;
	args[0] = &sr;
	fn[1] = skin_thread;
	args[1] = &kr;
	fn[2] = dxt_thread;
	args[2] = &dr;
	sr.rounds = sr.bad = sr.declined = 0;
	run_for(secs, fn, args, 3);
	spq_synth_stats(sr.s, &st);
	printf("sharing: synth %lu rounds (%lu declined, %lu timeouts, mean wait %.0f us, max %.0f), "
	       "max rel %.2g; skin %lu batches (%lu declined); DXT %lu images, %.0f us each "
	       "(alone %.0f)\n", sr.rounds, sr.declined, st.timeouts,
	       st.spu ? st.wait_us / st.spu : 0, st.wait_max, sr.maxrel, kr.batches, kr.declined,
	       dr.images, dr.us / dr.images, dxt_alone);
	check(sr.rounds > 20 && !sr.bad && sr.declined <= sr.rounds / 50,
	      "synth rounds on the pool = the PPE path (within 1e-5 of the peak), on the SPEs");
	check(kr.batches > 20 && !kr.bad && kr.declined <= kr.batches / 50,
	      "skinning batches on the pool = the PPE path, on the SPEs");
	check(dr.images > 5 && !dr.bad, "DXT1 images on the bulk queue meanwhile = reference");

	/* 2. a flood of long jobs with later deadlines: the rounds go first */
	fl.q = pool;
	fl.spin_us = timeout / 5;
	fl.dl_us = timeout * 50;
	fl.jobs = 0;
	memset(&dr, 0, sizeof(dr));
	speq_synth_close_v2(sr.s);	/* a fresh one: its counters are this phase's */
	sr.s = new_synth(SPQ_SYNTH_SPU);
	sr.rounds = sr.bad = sr.declined = 0;
	sr.maxrel = 0;
	sr.gap_us = 1000;
	fn[0] = flood_thread;
	args[0] = &fl;
	fn[1] = synth_thread;
	args[1] = &sr;
	fn[2] = dxt_thread;
	args[2] = &dr;
	run_for(secs, fn, args, 3);
	spq_synth_stats(sr.s, &st);
	printf("flood (%lu jobs of %u us, deadlines %.0f us out): synth %lu rounds, %lu declined, "
	       "%lu timeouts, mean wait %.0f us, max %.0f; DXT %lu images, %.0f us each\n", fl.jobs,
	       fl.spin_us, fl.dl_us, sr.rounds, sr.declined, st.timeouts,
	       st.spu ? st.wait_us / st.spu : 0, st.wait_max, dr.images,
	       dr.images ? dr.us / dr.images : 0);
	check(sr.s && fl.jobs > 20 && sr.rounds > 20 && !sr.bad && st.timeouts <= sr.rounds / 50,
	      "a flood of long jobs with later deadlines: the synth's rounds still finish in time");
	check(dr.images > 5 && !dr.bad && dr.us / dr.images < 3 * dxt_alone + 2000,
	      "... DXT's bulk queue unaffected");

	/* 2b. the same flood with earlier deadlines: the rounds wait behind it */
	{
		struct synth_run s2;
		struct spq_synth_stats st2;

		memset(&s2, 0, sizeof(s2));
		s2.ref = sr.ref;
		s2.s = new_synth(SPQ_SYNTH_SPU);
		memcpy(s2.v, sr.v, sizeof(s2.v));
		s2.gap_us = 1000;
		fl.dl_us = 0;
		fl.jobs = 0;
		fn[0] = flood_thread;
		args[0] = &fl;
		fn[1] = synth_thread;
		args[1] = &s2;
		run_for(secs / 2, fn, args, 2);
		spq_synth_stats(s2.s, &st2);
		printf("flood with earlier deadlines: synth %lu rounds, %lu timeouts, %lu backoffs, "
		       "%lu redone on the PPE\n", st2.rounds, st2.timeouts, st2.backoffs, st2.redone);
		check(s2.s && !s2.bad && st2.timeouts >= 3 && st2.backoffs >= 1,
		      "... with earlier deadlines it goes first: the rounds time out (redone on the "
		      "PPE, right), the synth backs off");
		speq_synth_close_v2(s2.s);
	}

	/* 3. a fault in a skinning job while the synth plays */
	spq_stats(pool, &ps0);
	memset(&dr, 0, sizeof(dr));
	sr.rounds = sr.bad = sr.declined = 0;
	spq_synth_stats(sr.s, &st);
	k = st.errors;
	spq_skin_fault = 1;
	kr.eio = 0;
	fn[0] = synth_thread;
	args[0] = &sr;
	fn[1] = skin_thread;
	args[1] = &kr;
	kr.batches = kr.bad = 0;
	run_for(secs / 2, fn, args, 2);
	spq_stats(pool, &ps);
	spq_synth_stats(sr.s, &st);
	printf("fault: skin %lu -EIO, %lu batches after; pool faults %lu, spawns %lu -> %lu, "
	       "alive %u; synth %lu rounds, %lu errors\n", kr.eio, kr.batches,
	       ps.faults - ps0.faults, ps0.spawns, ps.spawns, ps.alive, sr.rounds, st.errors - k);
	check(kr.eio == 1 && ps.faults > ps0.faults && ps.alive == 2,
	      "faulting skinning jobs: their batch -EIO, the pool keeps 2 workers");
	check(kr.batches > 10 && !kr.bad && sr.rounds > 10 && !sr.bad && st.errors == k,
	      "... the next batches and the synth's rounds go on, right");

	speq_synth_close_v2(sr.s);
	speq_synth_close_v2(sr.ref);
	speq_skin_close_v1(kr.s);
	speq_skin_close_v1(kr.ref);
	printf("%s: %d failure%s\n", failures ? "FAIL" : "ok", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
