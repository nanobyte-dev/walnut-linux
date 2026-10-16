/* speq_s3tc_compress_v1: the one entry point Mesa's S3TC texstore calls (through dlopen, before
 * its own tx_compress_dxt1). The policy lives here, so tuning never needs a Mesa rebuild:
 * SPEQ_S3TC=off|ppe|spu (default ppe), SPEQ_DEBUG=1 for one line per call on stderr.
 * Anything not offloaded returns -ENOTSUP and Mesa encodes it itself. */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dxt-ref.h"
#include "speq.h"

enum { S3TC_OFF, S3TC_PPE, S3TC_SPU };

static pthread_once_t once = PTHREAD_ONCE_INIT;
static int mode = S3TC_PPE, debug;
static const char *const mode_name[] = { "off", "ppe", "spu" };

static void init(void)
{
	const char *s = getenv("SPEQ_S3TC"), *d = getenv("SPEQ_DEBUG");

	if (s && !strcmp(s, "off"))
		mode = S3TC_OFF;
	else if (s && !strcmp(s, "spu"))
		mode = S3TC_SPU;
	else if (s && strcmp(s, "ppe") && *s)
		fprintf(stderr, "speq: SPEQ_S3TC=%s unknown, using ppe\n", s);
	debug = d && *d && strcmp(d, "0");
}

static double now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

int speq_s3tc_compress_v1(unsigned fmt, unsigned srccomps, const void *src, unsigned w,
			  unsigned h, void *dst, unsigned dst_stride)
{
	double t0 = 0;
	int ret = 0;

	pthread_once(&once, init);
	if (debug)
		t0 = now_us();
	if (fmt != SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT &&
	    fmt != SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT)
		ret = -ENOTSUP;	/* DXT3/5: later */
	else if (srccomps != 3 && srccomps != 4)
		ret = -ENOTSUP;
	else if (w % 4 || h % 4)
		ret = -ENOTSUP;	/* partial blocks: Mesa's encoder */
	else if (mode == S3TC_OFF)
		ret = -ENOTSUP;
	else if (!w || !h)
		ret = 0;
	else if (!src || !dst)
		ret = -EINVAL;
	else if (mode == S3TC_SPU)
		ret = -ENOSYS;	/* milestone D */
	else {
		if (dst_stride < 2 * w)	/* as tx_compress_dxt1: packed rows */
			dst_stride = 2 * w;
		dxt1_ref_image(src, srccomps, w, h, dst, dst_stride,
			       fmt == SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT);
	}
	if (debug)
		fprintf(stderr, "speq: s3tc fmt 0x%x comps %u %ux%u stride %u %s: %s, %.0f us\n",
			fmt, srccomps, w, h, dst_stride, mode_name[mode],
			ret ? strerror(-ret) : "done", now_us() - t0);
	return ret;
}
