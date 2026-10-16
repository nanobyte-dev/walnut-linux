/* dlopen-check <libspeq.so.0>: loads the library the way Mesa's hook does (dlopen, dlsym of
 * speq_s3tc_compress_v1) and checks that it gives the reference encoder's bytes, that only the
 * speq_* API is exported, and that speq_open fails cleanly where there is no spufs. */
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dxt-ref.h"
#include "speq.h"

typedef int (*s3tc_fn)(unsigned, unsigned, const void *, unsigned, unsigned, void *, unsigned);

static int failures;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		failures++;
}

int main(int argc, char **argv)
{
	static uint8_t src[128 * 64 * 4], a[128 * 64 / 2], b[128 * 64 / 2];
	unsigned (*abi)(void);
	int (*open_q)(struct speq **, const char *, unsigned, unsigned);
	struct speq *q = (struct speq *)&q;
	s3tc_fn f;
	void *h;
	unsigned i, rng = 1;
	int ret;
	const char *mode = getenv("SPEQ_S3TC");

	if (argc != 2) {
		fprintf(stderr, "usage: %s <libspeq.so.0>\n", argv[0]);
		return 2;
	}
	h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (!h) {
		fprintf(stderr, "%s\n", dlerror());
		return 1;
	}
	f = (s3tc_fn)dlsym(h, "speq_s3tc_compress_v1");
	abi = (unsigned (*)(void))dlsym(h, "speq_abi_version");
	open_q = (int (*)(struct speq **, const char *, unsigned, unsigned))dlsym(h, "speq_open");
	check(f && abi && open_q, "speq_s3tc_compress_v1, speq_abi_version, speq_open exported");
	check(!dlsym(h, "dxt1_ref_block") && !dlsym(h, "dxt1_ref_image") &&
	      !dlsym(h, "spq_fail_job") && !dlsym(h, "spq_spufs"), "internals hidden");
	if (!f || !abi || !open_q)
		return 1;
	check(abi() == 2, "speq_abi_version() == 2");
	/* no spufs here (or no such program): an error the caller falls back on, never a queue */
	ret = open_q(&q, "no-such-program.spu", 0, 0);
	check((ret == -ENOENT || ret == -EBUSY) && !q, "speq_open without spufs or program: -ENOENT");

	for (i = 0; i < sizeof(src); i++) {
		rng = rng * 1103515245 + 12345;
		src[i] = (i / 4 % 128 + (rng >> 16 & 31)) & 0xff;
	}
	if (mode && *mode && strcmp(mode, "ppe")) {
		check(f(SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 4, src, 128, 64, a, 0) < 0,
		      "SPEQ_S3TC set: declined");
	} else {
		dxt1_ref_image(src, 4, 128, 64, a, 256, 1);
		check(!f(SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 4, src, 128, 64, b, 256) &&
		      !memcmp(a, b, sizeof(a)), "RGBA DXT1 through dlsym = reference");
		dxt1_ref_image(src, 3, 128, 64, a, 256, 0);
		check(!f(SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 3, src, 128, 64, b, 0) &&
		      !memcmp(a, b, sizeof(a)), "RGB DXT1 from RGB through dlsym = reference");
	}
	printf("%s: %d failure%s\n", failures ? "FAIL" : "ok", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
