// SPDX-License-Identifier: GPL-2.0
/*
 * Every push of a mesa-shim capture (mesa-shim/wrap.c's dump: per push the big-endian words
 * 'PUSH', submit, push index, word count, then the words) through the validator, in order, one
 * channel state per file, as ps3rsx would see them: validate-capture FILE... prints each
 * rejected push (submit, push, the validator's reason) and a count per file; the exit status is
 * 1 if any push was refused. A refused submit leaves the channel state as it was before it (the
 * driver also refuses the channel from then on; the rest is checked anyway, to see everything).
 * Built by the Makefile next to validate-test.
 */
#include "shim.h"
#define PS3RSX_H	/* the driver header: not in userspace */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static char last_warn[256];
#define pr_warn(...) snprintf(last_warn, sizeof(last_warn), __VA_ARGS__)

struct ps3rsx_vstate {
	u32 bound[8];
	u8 cls[8];
	bool prim_open;
};
bool ps3rsx_known_object(u32 handle);
int ps3rsx_validate(struct ps3rsx_vstate *vs, const u32 *w, u32 nwords);
void ps3rsx_validate_init(void);

#include "../src/validate.c"

static u32 be32(const unsigned char *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
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
	for (int i = 1; i < argc; i++)
		if (check_file(argv[i]))
			bad = 1;
	return bad;
}
