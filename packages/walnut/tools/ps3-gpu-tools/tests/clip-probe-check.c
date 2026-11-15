/*
 * clip-probe-check: rsx-clip-probe's cases on the host, no GPU. For every case:
 * - its own safety check passes (reach and predictions inside the BO, quads in the viewport);
 * - its push passes the shipping validator (ps3rsx's validate.c from this tree, linked in from
 *   clip-probe-validator.c) on a fresh channel holding the probe's BOs;
 * - the same push with the colour target moved to its BO's end, or (extents above 1 KiB) to 1 KiB
 *   before it, is refused (the extent check really sees it; a gap follows the BO), and the
 *   variants the probe leaves out are refused;
 * - its predictions are pairwise different and none is empty (each outcome names one of them);
 * - the push fits the 64 KiB push BO;
 * - the readback (analyse()) on a simulated GPU: BOs written exactly as each prediction says give
 *   that prediction's tag, nothing written gives "nothing", one stray dword gives "OUTSIDE" and
 *   stops the run, a zeta write gives "ZETA-WRITTEN" and stops it;
 * - the swizzled cases send Mesa's pitch 4096, P2s and S4096 run last, in that order;
 * - the driver identity check: only ps3rsx's name and description pass (real nouveau, amdgpu,
 *   i915, a truncated or longer description are refused before any driver ioctl).
 */
#define CLIP_PROBE_LIB
#include "../src/rsx-clip-probe.c"
#include "clip-probe-check.h"

/* GPU offsets: GART BOs from 0x400000 (past the 2 MiB ring reserve), the FP in VRAM */
#define C_BO	0x00400000ull
#define Z_BO	(C_BO + BO_SIZE + MIB)	/* gaps: nothing joins the colour BO */
#define PUSH_BO	(Z_BO + BO_SIZE + MIB)
#define FP_BO	0x00900000ull

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); \
	printf("\n"); } } while (0)

static uint32_t buf[16384];
static uint32_t *cm, *zm;

/* the BOs as the probe fills them, then the dwords of bm written by the "GPU" */
static void gpu(const uint64_t *bm)
{
	fill(cm);
	fill(zm);
	for (uint32_t i = 0; bm && i < NDW; i++)
		if (bm[i / 64] >> (i & 63) & 1)
			cm[i] = 0xffff00ff;
}

/* analyse() with its printing out of the way: the tag and the stop flag */
static const char *quiet_analyse(const struct pcase *c, bool *stop)
{
	const char *r;

	fflush(stdout);
	FILE *keep = stdout;

	stdout = fopen("/dev/null", "w");
	r = analyse(c, cm, zm, false, stop);
	fclose(stdout);
	stdout = keep;
	return r;
}

static int run(const struct pcase *c, uint64_t c_lo, const char **why)
{
	size_t n = build(c, c_lo, Z_BO + LO, FP_BO, buf);

	CHECK(n <= 16384, "%s: push of %zu words past the push BO", c->name, n);
	return vcheck(buf, n, why);
}

int main(void)
{
	static const struct vbo bos[] = {
		{ false, C_BO, BO_SIZE }, { false, Z_BO, BO_SIZE }, { false, PUSH_BO, 65536 },
		{ true, FP_BO, 4096 },
	};
	uint64_t *a = malloc(NBW * 8), *b = malloc(NBW * 8);
	const char *why;

	vinit(bos, 4);
	for (int i = 0; i < NCASES; i++) {
		const struct pcase *c = &cases[i];
		const char *bad = selfcheck(c);
		int rc;

		CHECK(!bad, "%s: selfcheck: %s", c->name, bad ? bad : "");
		rc = run(c, C_BO + LO, &why);
		CHECK(rc == 0, "%s: refused by the validator (%d: %s)", c->name, rc, why);
		rc = run(c, C_BO + BO_SIZE, &why);
		CHECK(rc != 0, "%s: accepted with its target at the BO's end", c->name);
		if (validator_extent(c, fmt_bpp(c->fmt)) > 1024) {
			rc = run(c, C_BO + BO_SIZE - 1024, &why);
			CHECK(rc != 0, "%s: accepted with its target 1 KiB before the BO's end",
			      c->name);
		}
		for (int k = 0; k < c->nh; k++) {
			hyp_bitmap(c, &c->h[k], a);
			CHECK(bm_count(a) > 0, "%s: prediction %s is empty", c->name, c->h[k].tag);
			for (int j = 0; j < k; j++) {
				hyp_bitmap(c, &c->h[j], b);
				CHECK(memcmp(a, b, NBW * 8), "%s: predictions %s and %s write the same "
				      "dwords", c->name, c->h[j].tag, c->h[k].tag);
			}
		}
	}

	/* the readback on a simulated GPU */
	cm = malloc(BO_SIZE);
	zm = malloc(BO_SIZE);
	CHECK(cm && zm && !analysis_init(), "readback buffers");
	for (int i = 0; i < NCASES; i++) {
		const struct pcase *c = &cases[i];
		const char *r;
		bool stop;

		for (int k = 0; k < c->nh; k++) {
			hyp_bitmap(c, &c->h[k], a);
			gpu(a);
			r = quiet_analyse(c, &stop);
			CHECK(!strcmp(r, c->h[k].tag) && !stop, "%s: written as %s, read back as %s%s",
			      c->name, c->h[k].tag, r, stop ? " (stop)" : "");
		}
		gpu(NULL);
		r = quiet_analyse(c, &stop);
		CHECK(!strcmp(r, "nothing") && !stop, "%s: nothing written, read back as %s", c->name, r);
		hyp_bitmap(c, &c->h[0], a);
		a[(LO + 5 * MIB) / 4 / 64] |= 1;	/* a stray dword 5 MiB past lo */
		gpu(a);
		r = quiet_analyse(c, &stop);
		CHECK(!strcmp(r, "OUTSIDE") && stop, "%s: a stray write read back as %s", c->name, r);
		gpu(NULL);
		zm[LO / 4 + 3] = 0;
		r = quiet_analyse(c, &stop);
		CHECK(!strcmp(r, "ZETA-WRITTEN") && stop, "%s: a zeta write read back as %s", c->name, r);
	}
	free(cm);
	free(zm);

	/* the swizzled cases: Mesa's pitch, and last */
	for (int i = 0; i < NCASES; i++)
		if (cases[i].fmt != LIN)
			CHECK(cases[i].pitch == 4096, "%s: swizzled pitch %u, not Mesa's 4096",
			      cases[i].name, cases[i].pitch);
	CHECK(case_index("P2s") == NCASES - 2 && case_index("S4096") == NCASES - 1,
	      "P2s and S4096 aren't the last two cases");

	/* the identity check (setup() runs it on DRM_IOCTL_VERSION's answer, before any driver ioctl) */
	static const struct { const char *name, *desc; bool ok; } ids[] = {
		{ "nouveau", "PS3 RSX (nouveau ABI on lv1)", true },
		{ "nouveau", "nVidia Riva/TNT/GeForce/Quadro/Tesla/Tegra K1+", false },
		{ "amdgpu", "AMD GPU", false },
		{ "i915", "Intel Graphics", false },
		{ "ps3rsx", "PS3 RSX (nouveau ABI on lv1)", false },
		{ "nouveau", "PS3 RSX (nouveau ABI on lv1", false },
		{ "nouveau", "PS3 RSX (nouveau ABI on lv1) ", false },
		{ "nouveaux", "PS3 RSX (nouveau ABI on lv1)", false },
		{ "", "", false },
	};
	for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
		CHECK(identity_ok(ids[i].name, strlen(ids[i].name), ids[i].desc,
				  strlen(ids[i].desc)) == ids[i].ok, "identity \"%s\" \"%s\": %s",
		      ids[i].name, ids[i].desc, ids[i].ok ? "refused" : "accepted");
	/* the driver's own description, from its source */
	{
		FILE *f = fopen(DRV_C, "r");
		char line[256];
		bool found = false;

		while (f && fgets(line, sizeof(line), f))
			found |= strstr(line, ".desc = \"" PS3RSX_DESC "\"") != NULL;
		if (f)
			fclose(f);
		CHECK(found, "%s has no .desc = \"%s\"", DRV_C, PS3RSX_DESC);
	}

	/* the variants left out: refused by the shipping validator */
	struct pcase v = cases[case_index("P2s")];

	v.wx1 = 15;	/* Mesa's full 16-wide window clip with the offset 8 */
	CHECK(run(&v, C_BO + LO, &why) != 0, "P2s with window clip 16: accepted");
	printf("left out: P2s with window clip 16: %s\n", why);
	v = cases[case_index("P1a")];
	v.pitch = 0x10040;	/* P4 */
	CHECK(run(&v, C_BO + LO, &why) != 0, "pitch 0x10040: accepted");
	printf("left out: P4, pitch 0x10040: %s\n", why);
	v = cases[case_index("S4096")];
	v.wx1 = 1;	/* window clip 2 pixels at offset 4095: one column past the block */
	CHECK(run(&v, C_BO + LO, &why) != 0, "S4096 with window clip 2: accepted");
	printf("left out: S4096 with window clip 2: %s\n", why);

	free(a);
	free(b);
	printf("clip-probe-check: %d cases, %d checks, %d failed\n", NCASES, checks, fails);
	return fails != 0;
}
