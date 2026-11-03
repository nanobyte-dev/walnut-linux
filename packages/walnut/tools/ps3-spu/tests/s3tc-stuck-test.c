/* s3tc-stuck-test: round 3 MS4. A stuck SPE must not hang the texture upload. The fake backend
 * runs a test-only dxt.spu whose op never ends (until the test lets it), and
 *   wait:  speq_s3tc_compress_v1 (SPEQ_S3TC=auto, SPEQ_S3TC_TIMEOUT_MS=300) returns, with the bytes
 *          the PPE encoder makes, in bounded time; the queue is given up (the next image goes
 *          to the PPE at once, no timeout); letting the op end afterwards (a late job completing
 *          on the call's leaked done word) does no harm; mode spu behaves the same
 *   stall: spq_submit_dl on a ring that is full and not moving (SPEQ_SUBMIT_STALL_MS=300) gives
 *          the queue up: the call fails -EIO in bounded time, what was queued and not taken is
 *          failed, only the running job stays pending, and the next submit fails at once.
 * Without the fixes both hang: alarm() ends the test with a failure. Exits nonzero on any failure. */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dxt-job.h"
#include "dxt-ref.h"
#include "speq-int.h"
#include "speq-spu.h"

static int failures;
static volatile int release;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	fflush(stdout);
	if (!ok)
		failures++;
}

/* the stuck SPE: a job that ends only when the test says so */
static int op_stuck(const struct speq_job *job)
{
	(void)job;
	while (!release)
		usleep(20000);
	return 0;
}

SPEQ_PROGRAM(dxt, "dxt.spu", DXT_NOPS) = {
	[DXT_OP_ROWS] = op_stuck,
};

static void on_alarm(int s)
{
	(void)s;
	printf("FAIL hang: the test did not end in time\n");
	fflush(stdout);
	_exit(1);
}

static double ms_since(double t0)
{
	return (spq_now_us() - t0) / 1000.0;
}

#define W	256
#define H	256

static int test_wait(int spu_mode)
{
	static uint8_t src[W * H * 3], dst[W * H / 2], want[W * H / 2];
	struct spq_s3tc_stats st;
	unsigned i;
	double t0;
	int ret;

	for (i = 0; i < sizeof(src); i++)
		src[i] = (uint8_t)(i * 7 + (i >> 8) * 13);
	dxt1_ref_image(src, 3, W, H, want, 2 * W, 0);

	memset(dst, 0xaa, sizeof(dst));
	t0 = spq_now_us();
	ret = speq_s3tc_compress_v1(SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 3, src, W, H, dst, 2 * W);
	check(ret == 0, "the call returns 0: the image was redone on the PPE");
	check(ms_since(t0) > 250 && ms_since(t0) < 5000, "it took about the timeout (300 ms), not for ever");
	check(!memcmp(dst, want, sizeof(dst)), "the bytes are the PPE encoder's");
	spq_s3tc_stats(&st);
	check(st.timeouts == 1 && st.redone == 1 && st.spu == 0, "stats: one timeout, one image redone");

	memset(dst, 0xaa, sizeof(dst));
	t0 = spq_now_us();
	ret = speq_s3tc_compress_v1(SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 3, src, W, H, dst, 2 * W);
	/* auto: the PPE encoder's bytes; spu: the error of a broken queue (speq.h), dst untouched,
	 * Mesa's own encoder then */
	if (spu_mode)
		check(ret == -EIO && dst[0] == 0xaa, "the next image gets -EIO, dst untouched (SPEQ_S3TC=spu)");
	else
		check(ret == 0 && !memcmp(dst, want, sizeof(dst)), "the next image is right");
	check(ms_since(t0) < 200, "...and does not wait: the queue is given up");
	spq_s3tc_stats(&st);
	check(st.timeouts == 1, "...with no second timeout");

	release = 1;	/* the stuck job ends late: it completes on the call's leaked done word */
	usleep(300000);
	memset(dst, 0xaa, sizeof(dst));
	ret = speq_s3tc_compress_v1(SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 3, src, W, H, dst, 2 * W);
	if (spu_mode)
		check(ret == -EIO, "a late job ending changes nothing");
	else
		check(ret == 0 && !memcmp(dst, want, sizeof(dst)), "a late job ending changes nothing");
	return 0;
}

static int test_stall(void)
{
	struct speq *q;
	struct speq_job jobs[70];
	struct speq_done *d;
	unsigned i;
	double t0;
	int ret;

	check(speq_open(&q, "dxt.spu", 1, 0) == 0, "open a queue of one stuck worker");
	if (posix_memalign((void **)&d, 128, sizeof(*d)))
		return 1;
	memset(d, 0, sizeof(*d));
	memset(jobs, 0, sizeof(jobs));
	for (i = 0; i < 70; i++)
		jobs[i].op = DXT_OP_ROWS;
	t0 = spq_now_us();
	ret = speq_submit(q, jobs, 70, d);	/* 64 fit, one runs (stuck), the rest wait for room */
	check(ret == -EIO, "a submit that finds the ring full and still fails -EIO");
	check(ms_since(t0) > 250 && ms_since(t0) < 5000, "...after about the stall time (300 ms)");
	check((d->pending & ~SPEQ_WAITERS) == 1, "the queued jobs nobody took are failed, the running one stays pending");
	t0 = spq_now_us();
	ret = speq_submit(q, jobs, 1, d);
	check(ret == -EIO && ms_since(t0) < 100, "the next submit fails at once: the queue is given up");
	release = 1;
	return 0;
}

int main(int argc, char **argv)
{
	signal(SIGALRM, on_alarm);
	alarm(60);
	if (setenv("SPEQ_FAKE", "1", 1) || argc < 2) {
		fprintf(stderr, "usage: s3tc-stuck-test wait|stall\n");
		return 2;
	}
	if (!strcmp(argv[1], "wait")) {
		setenv("SPEQ_S3TC", "auto", 1);
		setenv("SPEQ_S3TC_TIMEOUT_MS", "300", 1);
		test_wait(0);
	} else if (!strcmp(argv[1], "wait-spu")) {
		setenv("SPEQ_S3TC", "spu", 1);
		setenv("SPEQ_S3TC_TIMEOUT_MS", "300", 1);
		test_wait(1);
	} else if (!strcmp(argv[1], "stall")) {
		setenv("SPEQ_SUBMIT_STALL_MS", "300", 1);
		test_stall();
	} else
		return 2;
	/* the stuck fake contexts are threads: leave without joining them */
	fflush(stdout);
	_exit(failures ? 1 : 0);
}
