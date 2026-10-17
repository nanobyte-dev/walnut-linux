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
	check(dlsym(h, "speq_wait_timeout") && dlsym(h, "speq_tune") && dlsym(h, "speq_nworkers"),
	      "speq_wait_timeout, speq_tune, speq_nworkers exported");
	check(dlsym(h, "speq_synth_open_v2") && dlsym(h, "speq_synth_begin_v2") &&
	      dlsym(h, "speq_synth_add_v2") && dlsym(h, "speq_synth_render_v2") &&
	      dlsym(h, "speq_synth_close_v2") && !dlsym(h, "speq_synth_open_v1"),
	      "speq_synth_*_v2 exported (fluidsynth's hook), v1 gone");
	check(!dlsym(h, "dxt1_ref_block") && !dlsym(h, "dxt1_ref_image") &&
	      !dlsym(h, "spq_fail_job") && !dlsym(h, "spq_spufs") && !dlsym(h, "spq_synth_tune"),
	      "internals hidden");
	{
		/* no spufs here: the synth can't open its queue (auto), the ppe path needs none */
		int (*sopen)(struct speq_synth **, const struct speq_synth_params *) =
			(int (*)(struct speq_synth **, const struct speq_synth_params *))
			dlsym(h, "speq_synth_open_v2");
		void (*sclose)(struct speq_synth *) =
			(void (*)(struct speq_synth *))dlsym(h, "speq_synth_close_v2");
		static double tab[1024] __attribute__((aligned(16)));
		struct speq_synth_params p = { SPEQ_SYNTH_ABI, sizeof(struct speq_synth_voice), 4, 64,
					       tab, tab };	/* tab: zeros, only copied */
		struct speq_synth *s = (struct speq_synth *)&s;
		const char *m = getenv("SPEQ_SYNTH");

		if (sopen && sclose) {
			ret = sopen(&s, &p);
			if (m && !strcmp(m, "ppe"))
				check(ret == 0 && s, "SPEQ_SYNTH=ppe: a synth without a queue");
			else if (m && !strcmp(m, "off"))
				check(ret == -ENOTSUP && !s, "SPEQ_SYNTH=off: -ENOTSUP");
			else
				check(ret < 0 && ret != -EPROTO && !s, "synth without spufs: an error");
			sclose(s);
		}
	}
	{
		/* the skinning hook's calls (Sauerbraten's), and without spufs: auto can't open
		 * its queue, ppe needs none, off declines */
		int (*kopen)(struct speq_skin **, unsigned, unsigned) =
			(int (*)(struct speq_skin **, unsigned, unsigned))dlsym(h, "speq_skin_open_v1");
		void (*kclose)(struct speq_skin *) =
			(void (*)(struct speq_skin *))dlsym(h, "speq_skin_close_v1");
		struct speq_skin *k = (struct speq_skin *)&k;
		const char *m = getenv("SPEQ_SKIN");

		check(kopen && kclose && dlsym(h, "speq_skin_begin_v1") &&
		      dlsym(h, "speq_skin_add_v1") && dlsym(h, "speq_skin_end_v1") &&
		      !dlsym(h, "spq_skin_tune"), "speq_skin_*_v1 exported (Sauerbraten's hook)");
		if (kopen && kclose) {
			check(kopen(&k, SPEQ_SKIN_ABI + 1, sizeof(struct speq_skin_mesh)) == -EPROTO &&
			      !k, "skin of another ABI: -EPROTO");
			ret = kopen(&k, SPEQ_SKIN_ABI, sizeof(struct speq_skin_mesh));
			if (m && !strcmp(m, "ppe"))
				check(ret == 0 && k, "SPEQ_SKIN=ppe: a skin without a queue");
			else if (m && !strcmp(m, "off"))
				check(ret == -ENOTSUP && !k, "SPEQ_SKIN=off: -ENOTSUP");
			else
				check(ret < 0 && ret != -EPROTO && !k, "skin without spufs: an error");
			kclose(k);
		}
	}
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
	if (mode && *mode && strcmp(mode, "ppe") && strcmp(mode, "auto")) {	/* off; spu: no queue */
		check(f(SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 4, src, 128, 64, a, 0) < 0,
		      "SPEQ_S3TC set: declined");
	} else {
		dxt1_ref_image(src, 4, 128, 64, a, 256, 1);
		check(!f(SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 4, src, 128, 64, b, 256) &&
		      !memcmp(a, b, sizeof(a)), "RGBA DXT1 through dlsym = reference");
		dxt1_ref_image(src, 3, 128, 64, a, 256, 0);
		check(!f(SPEQ_GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 3, src, 128, 64, b, 0) &&
		      !memcmp(a, b, sizeof(a)), "RGB DXT1 from RGB through dlsym = reference");
		dxt5_ref_image(src, 4, 64, 64, a, 256);	/* Mesa 0032's call: 64x64 is a's size */
		check(!f(SPEQ_GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 4, src, 64, 64, b, 256) &&
		      !memcmp(a, b, sizeof(a)), "RGBA DXT5 through dlsym = reference");
	}
	printf("%s: %d failure%s\n", failures ? "FAIL" : "ok", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
