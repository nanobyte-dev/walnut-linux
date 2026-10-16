/* speq_s3tc_compress_v1: the one entry point Mesa's S3TC texstore calls (through dlopen, before
 * its own tx_compress_dxt1). The policy lives here, so tuning never needs a Mesa rebuild.
 *
 * Paths (SPEQ_S3TC):
 * - auto (default): the SPEs (dxt.spu) for RGBA sources of at least SPEQ_S3TC_MIN pixels
 *   (default 64x64), when the queue opens; the PPE encoder (lib/dxt-ref.c) otherwise.
 * - spu: the SPEs for every image they can take (RGBA source, 8-aligned dst and stride), any
 *   size; the PPE for the rest. A queue that doesn't open is returned as the error.
 * - ppe: the PPE encoder. off: -ENOTSUP (Mesa's encoder).
 * Both encoders give the same bytes. SPEQ_DEBUG=1: one line per call on stderr.
 *
 * The queue opens on the first SPU call, once per process, with a worker per SPE, and stays
 * open (idle workers park after 10 ms and free their contexts after 5 s: queue.c). If it
 * can't open, or its workers die for good, the process uses the PPE from then on.
 * An image is cut into about one job per worker, whole block rows, at least JOB_MIN_PX pixels
 * each. Before the submit the PPE touches each destination page (a store) and each source page
 * (a load): an SPU put into a page the process hasn't touched costs 40-87 us, served on the
 * worker's thread, against a few for the PPE. If any job fails, the whole image is redone on
 * the PPE and the call still succeeds. Calls from several threads are safe: each has its own
 * done word. */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dxt-job.h"
#include "dxt-ref.h"
#include "speq-int.h"

#define S3TC_PROG	"dxt.spu"
#define MIN_PX		(64 * 64)	/* auto: smaller images stay on the PPE (a mip chain's tail) */
#define JOB_MIN_PX	16384		/* ~45 us of one SPE: dispatch is 3-10 us, p99 65 us */
#define BAD_EA		0x1000		/* spq_s3tc_fault: an unmapped EA (a DATA_STORAGE fault) */

static pthread_once_t once = PTHREAD_ONCE_INIT;
static int mode = SPQ_S3TC_AUTO, debug;
static unsigned long min_px = MIN_PX;

static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
static struct speq *queue;	/* set once (release), never closed */
static int qerr;		/* the open's error, or -EIO once the queue broke */
static long page;

int spq_s3tc_fault;
static struct spq_s3tc_stats stats;

#define LOAD(p)		__atomic_load_n((p), __ATOMIC_ACQUIRE)
#define INC(p)		__atomic_fetch_add((p), 1, __ATOMIC_RELAXED)

static void init(void)
{
	const char *s = getenv("SPEQ_S3TC"), *d = getenv("SPEQ_DEBUG"), *m = getenv("SPEQ_S3TC_MIN");
	unsigned long a, b;
	char x;

	if (!s || !*s || !strcmp(s, "auto"))
		mode = SPQ_S3TC_AUTO;
	else if (!strcmp(s, "off"))
		mode = SPQ_S3TC_OFF;
	else if (!strcmp(s, "ppe"))
		mode = SPQ_S3TC_PPE;
	else if (!strcmp(s, "spu"))
		mode = SPQ_S3TC_SPU;
	else
		fprintf(stderr, "speq: SPEQ_S3TC=%s unknown, using auto\n", s);
	/* SPEQ_S3TC_MIN: WxH or a pixel count */
	if (m && sscanf(m, "%lu%c%lu", &a, &x, &b) == 3 && (x == 'x' || x == 'X'))
		min_px = a * b;
	else if (m && *m)
		min_px = strtoul(m, NULL, 0);
	debug = d && *d && strcmp(d, "0");
	page = sysconf(_SC_PAGESIZE);
	if (page <= 0)
		page = 4096;
}

int spq_s3tc_mode(int m)
{
	pthread_once(&once, init);
	if (m >= SPQ_S3TC_OFF && m <= SPQ_S3TC_AUTO)
		mode = m;
	return mode;
}

void spq_s3tc_stats(struct spq_s3tc_stats *s)
{
	s->spu = LOAD(&stats.spu);
	s->ppe = LOAD(&stats.ppe);
	s->redone = LOAD(&stats.redone);
	s->jobs = LOAD(&stats.jobs);
}

struct speq *spq_s3tc_queue(void)
{
	return LOAD(&queue);
}

/* The queue, opened on first use; NULL with *err once it can't be had */
static struct speq *get_queue(int *err, double *open_us)
{
	struct speq *q = LOAD(&queue);
	double t0;
	int ret;

	if (q && !LOAD(&qerr))
		return q;
	pthread_mutex_lock(&qlock);
	if (!queue && !qerr) {
		t0 = spq_now_us();
		ret = speq_open(&q, S3TC_PROG, 0, 0);
		*open_us = spq_now_us() - t0;
		if (ret)
			qerr = ret;
		else
			__atomic_store_n(&queue, q, __ATOMIC_RELEASE);
	}
	q = qerr ? NULL : queue;
	*err = qerr;
	pthread_mutex_unlock(&qlock);
	return q;
}

static void queue_broke(int err)
{
	pthread_mutex_lock(&qlock);
	if (!qerr)
		qerr = err;
	pthread_mutex_unlock(&qlock);
}

/* One store per destination page (in the rows, never the padding between them), one load per
 * source page */
static void touch(uint8_t *dst, unsigned rows, unsigned rowbytes, unsigned stride,
		  const uint8_t *src, size_t srclen)
{
	uintptr_t mask = ~(uintptr_t)(page - 1), last = 0, p, end;
	const volatile uint8_t *s;
	uint8_t sink = 0;
	unsigned r;

	for (r = 0; r < rows; r++) {
		p = (uintptr_t)dst + (size_t)r * stride;
		end = p + rowbytes;
		for (; p < end; p = (p & mask) + page)
			if ((p & mask) != last) {
				*(volatile uint8_t *)p = 0;
				last = p & mask;
			}
	}
	for (p = (uintptr_t)src; p < (uintptr_t)src + srclen; p = (p & mask) + page) {
		s = (const volatile uint8_t *)p;
		sink += *s;
	}
	(void)sink;
}

/* The SPU path: 0, or -errno with the image to be redone on the PPE */
static int spu_encode(struct speq *q, const uint8_t *src, unsigned w, unsigned h, uint8_t *dst,
		      unsigned stride, int punch, unsigned *njobs)
{
	struct speq_job jobs[SPEQ_MAX_WORKERS];
	struct speq_done done __attribute__((aligned(128)));
	unsigned rows = h / 4, nw = q->nworkers ? q->nworkers : 1, per, minrows, n, i, bad;
	int ret, err;

	per = (rows + nw - 1) / nw;
	minrows = (JOB_MIN_PX + 4 * w - 1) / (4 * w);
	if (per < minrows)
		per = minrows;
	n = (rows + per - 1) / per;
	*njobs = n;
	memset(jobs, 0, sizeof(jobs));
	bad = __atomic_exchange_n(&spq_s3tc_fault, 0, __ATOMIC_RELAXED);
	for (i = 0; i < n; i++) {
		jobs[i].op = DXT_OP_ROWS;
		jobs[i].arg[DXT_ARG_SRC] = (uintptr_t)src;
		jobs[i].arg[DXT_ARG_WIDTH] = w;
		jobs[i].arg[DXT_ARG_ROW0] = i * per;
		jobs[i].arg[DXT_ARG_ROWS] = i + 1 < n ? per : rows - i * per;
		jobs[i].arg[DXT_ARG_DST] = bad && i == (bad - 1 < n ? bad - 1 : n - 1) ?
					   BAD_EA : (uintptr_t)dst;
		jobs[i].arg[DXT_ARG_STRIDE] = stride;
		jobs[i].arg[DXT_ARG_MODE] = punch ? DXT_JOB_RGBA : DXT_JOB_RGB;
	}
	touch(dst, rows, 2 * w, stride, src, (size_t)16 * w * rows);
	memset(&done, 0, sizeof(done));
	ret = speq_submit(q, jobs, n, &done);
	if (ret == -EINVAL)
		return ret;	/* nothing queued */
	/* even after a failed submit: jobs it queued may run, and write dst */
	err = speq_wait(q, &done);
	/* every worker gave up, or this is a fork child: the PPE from now on (one job's failure
	 * isn't that: its context is recreated) */
	if (ret == -EIO || __atomic_load_n(&q->dead, __ATOMIC_ACQUIRE))
		queue_broke(-EIO);
	return ret ? ret : err;
}

int speq_s3tc_compress_v1(unsigned fmt, unsigned srccomps, const void *src, unsigned w,
			  unsigned h, void *dst, unsigned dst_stride)
{
	const char *why = "", *path = "ppe";
	double t0 = 0, open_us = -1;
	unsigned njobs = 0;
	int ret = 0, punch, use_spu = 0, err = 0, spu_ret = 0;
	struct speq *q = NULL;
	char extra[64] = "";

	pthread_once(&once, init);
	if (debug)
		t0 = spq_now_us();
	punch = fmt == SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
	if (fmt != SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT && !punch)
		ret = -ENOTSUP;	/* DXT3/5: later */
	else if (srccomps != 3 && srccomps != 4)
		ret = -ENOTSUP;
	else if (w % 4 || h % 4)
		ret = -ENOTSUP;	/* partial blocks: Mesa's encoder */
	else if (mode == SPQ_S3TC_OFF)
		ret = -ENOTSUP;
	else if (!w || !h)
		ret = 0;
	else if (!src || !dst)
		ret = -EINVAL;
	if (ret || !w || !h) {
		path = "-";
		goto out;
	}
	if (dst_stride < 2 * w)	/* as tx_compress_dxt1: packed rows */
		dst_stride = 2 * w;

	if (mode == SPQ_S3TC_PPE)
		why = " (SPEQ_S3TC=ppe)";
	else if (srccomps != 4)
		why = " (RGB source)";
	else if (((uintptr_t)dst | dst_stride) & 7)
		why = " (dst not 8-aligned)";
	else if (mode == SPQ_S3TC_AUTO && (unsigned long)w * h < min_px)
		why = " (small)";
	else if (!(q = get_queue(&err, &open_us))) {
		if (mode == SPQ_S3TC_SPU) {
			ret = err;
			path = "-";
			snprintf(extra, sizeof(extra), " (no queue: %s)", strerror(-err));
			goto out;
		}
		snprintf(extra, sizeof(extra), " (no queue: %s)", strerror(-err));
	} else
		use_spu = 1;

	if (use_spu) {
		spu_ret = spu_encode(q, src, w, h, dst, dst_stride, punch, &njobs);
		__atomic_fetch_add(&stats.jobs, njobs, __ATOMIC_RELAXED);
		if (!spu_ret) {
			INC(&stats.spu);
			path = "spu";
			goto out;
		}
		/* any failure: the whole image again on the PPE; Mesa never sees it */
		INC(&stats.redone);
		snprintf(extra, sizeof(extra), " (spu failed: %s, redone)", strerror(-spu_ret));
	}
	INC(&stats.ppe);
	dxt1_ref_image(src, srccomps, w, h, dst, dst_stride, punch);
out:
	if (debug) {
		char o[32] = "";

		if (open_us >= 0)
			snprintf(o, sizeof(o), ", open %.0f us", open_us);
		fprintf(stderr, "speq: s3tc %s %ux%u comps %u stride %u: %s%s%s, %u jobs%s, %.0f us%s%s\n",
			punch ? "dxt1a" : "dxt1", w, h, srccomps, dst_stride, path, why, extra, njobs,
			o, spq_now_us() - t0, ret ? ": " : "", ret ? strerror(-ret) : "");
	}
	return ret;
}
