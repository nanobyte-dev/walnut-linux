/* speq_s3tc_compress_v1: the one entry point Mesa's S3TC texstore calls (through dlopen, before
 * its own tx_compress_dxt1 or tx_compress_dxt5). The policy lives here, so tuning never needs a
 * Mesa rebuild. Formats: RGB and RGBA DXT1, and RGBA DXT5 (Mesa's choice for generic compressed
 * RGBA: Teeworlds, TORCS, any game asking for compressed alpha textures); DXT5 can be sent back
 * to Mesa's encoder alone with SPEQ_S3TC_DXT5=off.
 *
 * Paths (SPEQ_S3TC):
 * - auto (default): the SPEs (dxt.spu) for images of at least SPEQ_S3TC_MIN pixels (default
 *   64x64), when the queue opens; the PPE encoder (lib/dxt-ref.c) otherwise.
 * - spu: the SPEs for every image they can take (8-aligned dst and stride, DXT5 16-aligned), any
 *   size; the PPE for the rest. A queue that doesn't open is returned as the error.
 * - ppe: the PPE encoder. off: -ENOTSUP (Mesa's encoder).
 * Sources: RGBA (4 bytes a pixel) and RGB (3: Mesa's GL_RGB uploads to RGB DXT1, Sauerbraten's
 * and other games' textures without alpha; Mesa converts luminance, BGR and the rest to one of
 * the two first, rows tightly packed). dxt.spu expands RGB to RGBA in local store (doc/dxt-rgb.md).
 * SPEQ_S3TC_RGB=ppe sends RGB sources to the PPE encoder whatever SPEQ_S3TC says (the A/B
 * switch: where RGB sources always went before), off to Mesa's encoder; auto (default) as RGBA.
 * Both encoders give the same bytes. SPEQ_DEBUG=1: one line per call on stderr.
 *
 * The queue opens on the first SPU call, once per process, as a bulk queue (a worker per SPE
 * but the two left to audio, speq.h SPEQ_OPEN_BULK: 4 of 6), and stays
 * open (idle workers park after 10 ms and free their contexts after 5 s: queue.c). If it
 * can't open, or its workers die for good, the process uses the PPE from then on.
 * An image is cut into about one job per worker, whole block rows, at least JOB_MIN_PX pixels
 * each. Before the submit the PPE touches each destination page (a store) and each source page
 * (a load): an SPU put into a page the process hasn't touched costs 40-87 us, served on the
 * worker's thread, against a few for the PPE. If any job fails, the whole image is redone on
 * the PPE and the call still succeeds; MAX_FAILS images in a row (a destination the SPEs can't
 * write, say) and the process stays on the PPE. Calls from several threads are safe: each has
 * its own done word. */
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
#define MAX_FAILS	3		/* images failed on the SPEs in a row: the PPE from then on */

static pthread_once_t once = PTHREAD_ONCE_INIT;
static int mode = SPQ_S3TC_AUTO, debug, dxt5 = 1, rgb = SPQ_S3TC_AUTO;
static unsigned long min_px = MIN_PX;

static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
static struct speq *queue;	/* set once (release), never closed */
static int qerr;		/* the open's error, or -EIO once the queue broke */
static long page;
static int fails;		/* SPU images failed in a row */

int spq_s3tc_fault;
static struct spq_s3tc_stats stats;

#define LOAD(p)		__atomic_load_n((p), __ATOMIC_ACQUIRE)
#define INC(p)		__atomic_fetch_add((p), 1, __ATOMIC_RELAXED)

static void init(void)
{
	const char *s = getenv("SPEQ_S3TC"), *d = getenv("SPEQ_DEBUG"), *m = getenv("SPEQ_S3TC_MIN");
	const char *five = getenv("SPEQ_S3TC_DXT5"), *r = getenv("SPEQ_S3TC_RGB");
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
	if (!r || !*r || !strcmp(r, "auto"))
		rgb = SPQ_S3TC_AUTO;
	else if (!strcmp(r, "ppe"))
		rgb = SPQ_S3TC_PPE;
	else if (!strcmp(r, "off") || !strcmp(r, "0"))
		rgb = SPQ_S3TC_OFF;
	else
		fprintf(stderr, "speq: SPEQ_S3TC_RGB=%s unknown, using auto\n", r);
	debug = d && *d && strcmp(d, "0");
	dxt5 = !five || !*five || (strcmp(five, "0") && strcmp(five, "off"));
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

int spq_s3tc_rgb(int m)
{
	pthread_once(&once, init);
	if (m == SPQ_S3TC_OFF || m == SPQ_S3TC_PPE || m == SPQ_S3TC_AUTO)
		rgb = m;
	return rgb;
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
		ret = speq_open(&q, S3TC_PROG, 0, SPEQ_OPEN_BULK);
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

/* The SPU path: 0, or -errno with the image to be redone on the PPE. jmode: DXT_JOB_*, comps
 * 3 or 4 */
static int spu_encode(struct speq *q, const uint8_t *src, unsigned comps, unsigned w, unsigned h,
		      uint8_t *dst, unsigned stride, unsigned jmode, unsigned *njobs)
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
		jobs[i].arg[DXT_ARG_MODE] = jmode | (comps == 3 ? DXT_JOB_SRC_RGB : 0);
	}
	touch(dst, rows, jmode == DXT_JOB_DXT5 ? 4 * w : 2 * w, stride, src,
	      (size_t)4 * comps * w * rows);
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
	const char *why = "", *path = "ppe", *name;
	double t0 = 0, open_us = -1;
	unsigned njobs = 0, rowb, align, jmode;
	int ret = 0, punch, is5, use_spu = 0, err = 0, spu_ret = 0;
	struct speq *q = NULL;
	char extra[64] = "";

	pthread_once(&once, init);
	if (debug)
		t0 = spq_now_us();
	punch = fmt == SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
	is5 = fmt == SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
	name = is5 ? "dxt5" : punch ? "dxt1a" : "dxt1";
	rowb = is5 ? 4 * w : 2 * w;	/* bytes per block row */
	align = is5 ? 16 : 8;		/* the SPEs' dst and stride: whole blocks in quadwords */
	jmode = is5 ? DXT_JOB_DXT5 : punch ? DXT_JOB_RGBA : DXT_JOB_RGB;
	if (fmt != SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT && !punch && !is5)
		ret = -ENOTSUP;	/* DXT3: Mesa's encoder */
	else if (is5 && !dxt5)
		ret = -ENOTSUP;	/* SPEQ_S3TC_DXT5=off */
	else if (srccomps != 3 && srccomps != 4)
		ret = -ENOTSUP;
	else if (w % 4 || h % 4)
		ret = -ENOTSUP;	/* partial blocks: Mesa's encoder */
	else if (mode == SPQ_S3TC_OFF)
		ret = -ENOTSUP;
	else if (srccomps == 3 && rgb == SPQ_S3TC_OFF)
		ret = -ENOTSUP;	/* SPEQ_S3TC_RGB=off */
	else if (!w || !h)
		ret = 0;
	else if (!src || !dst)
		ret = -EINVAL;
	if (ret || !w || !h) {
		path = "-";
		goto out;
	}
	if (dst_stride < rowb)	/* as tx_compress_dxt1/5: packed rows */
		dst_stride = rowb;

	if (mode == SPQ_S3TC_PPE)
		why = " (SPEQ_S3TC=ppe)";
	else if (srccomps == 3 && rgb == SPQ_S3TC_PPE)
		why = " (SPEQ_S3TC_RGB=ppe)";
	else if (((uintptr_t)dst | dst_stride) & (align - 1))
		why = is5 ? " (dst not 16-aligned)" : " (dst not 8-aligned)";
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
		spu_ret = spu_encode(q, src, srccomps, w, h, dst, dst_stride, jmode, &njobs);
		__atomic_fetch_add(&stats.jobs, njobs, __ATOMIC_RELAXED);
		if (!spu_ret) {
			__atomic_store_n(&fails, 0, __ATOMIC_RELAXED);
			INC(&stats.spu);
			path = "spu";
			goto out;
		}
		/* any failure: the whole image again on the PPE; Mesa never sees it */
		INC(&stats.redone);
		if (__atomic_add_fetch(&fails, 1, __ATOMIC_RELAXED) >= MAX_FAILS) {
			queue_broke(spu_ret);
			if (debug)
				fprintf(stderr, "speq: s3tc: %d images in a row failed on the SPEs, "
					"the PPE from now on\n", MAX_FAILS);
		}
		snprintf(extra, sizeof(extra), " (spu failed: %s, redone)", strerror(-spu_ret));
	}
	INC(&stats.ppe);
	if (is5)
		dxt5_ref_image(src, srccomps, w, h, dst, dst_stride);
	else
		dxt1_ref_image(src, srccomps, w, h, dst, dst_stride, punch);
out:
	if (debug) {
		char o[32] = "";

		if (open_us >= 0)
			snprintf(o, sizeof(o), ", open %.0f us", open_us);
		fprintf(stderr, "speq: s3tc %s %ux%u comps %u stride %u: %s%s%s, %u jobs%s, %.0f us%s%s\n",
			name, w, h, srccomps, dst_stride, path, why, extra, njobs,
			o, spq_now_us() - t0, ret ? ": " : "", ret ? strerror(-ret) : "");
	}
	return ret;
}
