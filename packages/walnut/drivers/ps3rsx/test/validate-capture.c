// SPDX-License-Identifier: GPL-2.0
/*
 * Every push of a mesa-shim capture (mesa-shim/wrap.c's dump: per push the big-endian words
 * 'PUSH', submit, push index, word count, then the words) through the validator, in order, one
 * channel state per file, as ps3rsx would see them:
 *   validate-capture FILE...		each rejected push (submit, push, the validator's reason)
 *					and a count per file. A rejected submit leaves the channel
 *					state as it was before it, as in the driver (which cancels it
 *					or refuses the channel; the rest is checked anyway).
 *   validate-capture --gpu FILE...	a .gpu dump (what ps3rsx sent to the GPU: the accepted
 *					pushes and its own ENDs) as one stream, nothing rolled back:
 *					every word must pass; prints the draws (BEGINs) in it.
 *   validate-capture --state FILE	the 3D state the GPU holds after a .gpu dump (shadow.c):
 *					"method value" per line, to compare two runs with diff.
 * The exit status is 1 if anything was refused. Built by the Makefile next to validate-test.
 */
#include "shim.h"
#define PS3RSX_H	/* the driver header: not in userspace */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static char last_warn[256];
#define pr_warn(...) snprintf(last_warn, sizeof(last_warn), __VA_ARGS__)
#define pr_warn_ratelimited pr_warn
#define RESTORE_MAX_WORDS	(16 * 1024)	/* as in ps3rsx.h */

struct ps3rsx_vstate {
	u32 bound[8];
	u8 cls[8];
	bool prim_open;
};
bool ps3rsx_known_object(u32 handle);
int ps3rsx_validate(struct ps3rsx_vstate *vs, const u32 *w, u32 nwords);
void ps3rsx_validate_init(void);

#include "../src/validate.c"
#include "../src/shadow.c"

static u32 be32(const unsigned char *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

/* The words of every record of a dump, in order (one stream); *n words */
static u32 *read_all(const char *path, u32 *n)
{
	FILE *f = fopen(path, "rb");
	unsigned char hdr[16];
	u32 *w = NULL, cap = 0;

	*n = 0;
	if (!f) {
		perror(path);
		return NULL;
	}
	while (fread(hdr, 1, 16, f) == 16) {
		u32 k = be32(hdr + 12);

		if (be32(hdr) != 0x50555348) {
			printf("%s: bad record after %u words\n", path, *n);
			break;
		}
		if (*n + k > cap) {
			cap = (*n + k) * 2;
			w = realloc(w, cap * 4 + 4);
		}
		if (fread(&w[*n], 4, k, f) != k) {
			printf("%s: short record\n", path);
			break;
		}
		for (u32 i = 0; i < k; i++)
			w[*n + i] = be32((unsigned char *)&w[*n + i]);
		*n += k;
	}
	fclose(f);
	return w;
}

/* A .gpu dump as one stream: 0 if every word passes */
static int check_gpu(const char *path)
{
	struct ps3rsx_vstate vs;
	u32 n, i = 0, begins = 0;
	u32 *w = read_all(path, &n);
	int r;

	if (!w)
		return -1;
	memset(&vs, 0, sizeof(vs));
	last_warn[0] = 0;
	r = ps3rsx_validate(&vs, w, n);
	/* the draws: BEGIN_END with a primitive, on the 3D object's subchannel (any here) */
	while (i < n) {
		u32 h = w[i], k = (h >> 18) & 0x7ff;

		if (!(h & 0xe0030003) && (h & 0x1ffc) == 0x1808 && k == 1 && i + 1 < n && w[i + 1])
			begins++;
		i += 1 + (h ? k : 0);
	}
	printf("%s: %u words as one stream, %u draws: %s%s", path, n, begins,
	       r ? "REFUSED: " : "every word passes\n", r ? last_warn : "");
	free(w);
	return r ? 1 : 0;
}

/* The 3D state after a .gpu dump, one "method value" line per register set */
static int print_state(const char *path)
{
	struct ps3rsx_shadow *sh = ps3rsx_shadow_new();
	u32 n, *w = read_all(path, &n);

	if (!w || !sh)
		return -1;
	ps3rsx_shadow_push(sh, w, n);
	for (u32 r = 0x180 / 4; r < NREG; r++)
		if (test_bit(r, sh->reg3d_set))
			printf("%04x %08x\n", r * 4, sh->reg3d[r]);
	printf("vp slots %u, constants %u\n", bitmap_weight(sh->vp_set, VP_SLOTS),
	       bitmap_weight(sh->cst_set, CONST_SLOTS));
	ps3rsx_shadow_free(sh);
	free(w);
	return 0;
}

static int check_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	struct ps3rsx_vstate vs, before;
	unsigned char hdr[16];
	u32 *w = NULL, cap = 0, cur = ~0u, pushes = 0, submits = 0, refused = 0;
	unsigned long long words = 0;
	bool bad_submit = false;

	if (!f) {
		perror(path);
		return -1;
	}
	memset(&vs, 0, sizeof(vs));
	before = vs;
	while (fread(hdr, 1, 16, f) == 16) {
		u32 magic = be32(hdr), sub = be32(hdr + 4), idx = be32(hdr + 8), n = be32(hdr + 12);

		if (magic != 0x50555348) {
			printf("%s: bad record after %u pushes\n", path, pushes);
			break;
		}
		if (n > cap) {
			cap = n;
			w = realloc(w, cap * 4 + 4);
		}
		if (fread(w, 4, n, f) != n) {
			printf("%s: short push %u.%u\n", path, sub, idx);
			break;
		}
		for (u32 k = 0; k < n; k++)
			w[k] = be32((unsigned char *)&w[k]);
		if (sub != cur) {
			if (bad_submit)
				vs = before;	/* as if the refused submit never ran */
			before = vs;
			bad_submit = false;
			cur = sub;
			submits++;
		}
		pushes++;
		words += n;
		last_warn[0] = 0;
		if (ps3rsx_validate(&vs, w, n)) {
			refused++;
			bad_submit = true;
			printf("%s: submit %u push %u (%u words) REFUSED: %s", path, sub, idx, n, last_warn);
		}
	}
	fclose(f);
	free(w);
	printf("%s: %u submits, %u pushes, %llu words, %u refused\n", path, submits, pushes, words,
	       refused);
	return refused;
}

int main(int argc, char **argv)
{
	int bad = 0;

	ps3rsx_validate_init();
	ps3rsx_shadow_init();
	if (argc > 2 && !strcmp(argv[1], "--state"))
		return print_state(argv[2]) ? 1 : 0;
	if (argc > 1 && !strcmp(argv[1], "--gpu")) {
		for (int i = 2; i < argc; i++)
			if (check_gpu(argv[i]))
				bad = 1;
		return bad;
	}
	for (int i = 1; i < argc; i++)
		if (check_file(argv[i]))
			bad = 1;
	return bad;
}
