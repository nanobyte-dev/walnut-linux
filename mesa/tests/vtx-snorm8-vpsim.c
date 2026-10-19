/* vtx-snorm8-vpsim: NV30_VTX_SNORM8's vertex program prologue as the translator emits it
 * (Mesa's nvfx_vertprog.c, built from build/work/mesa) run on a small interpreter of NV40
 * vertex program code: every program is translated twice, without the prologue and with it for
 * a set of inputs (1 to 4 components), and run on the same vertices, the plain one given
 * util_format's unpacked floats and the other the raw bytes the GPU fetches (U8_USCALED, missing
 * components 0, 0, 1). The outputs must match bit for bit (IEEE single precision; the RSX's own
 * rounding is the PS3's part). Two programs: plain arithmetic on the inputs, and inputs read
 * with swizzles, negated, as abs and twice in one instruction. Also checked: four instructions
 * more per converted input, the same input mask (VP_ATTRIB_EN), the program ending where END is.
 * The interpreter knows the vector slot's MOV MUL ADD MAD DP3 DP4 MIN MAX SLT SGE and refuses
 * anything else. Built and run by tools/run-tests.sh mesa-vtx-snorm8, with the TGSI parser, the
 * generated headers it needs and vtx-snorm8-stubs.c for the few Mesa utilities it calls. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "tgsi/tgsi_text.h"
#include "tgsi/tgsi_scan.h"
#include "nv30/nv30_state.h"
#include "nv30/nv30-40_3d.xml.h"
#include "nv30/nv30_vtx_snorm8.h"

bool _nvfx_vertprog_translate(uint16_t oclass, struct nv30_vertprog *vp);

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct sim {
	float in[16][4], temp[64][4], out[32][4], c[512][4];
};

static void rd(struct sim *s, uint32_t sr, int pos, uint32_t w0, unsigned in_idx, unsigned c_idx, float v[4])
{
	unsigned type = sr & 3, t = (sr >> 2) & 0x1f;
	const float *r;
	switch (type) {
	case 1: r = s->temp[t]; break;
	case 2: r = s->in[in_idx]; break;
	case 3: r = s->c[c_idx]; break;
	default: r = s->in[in_idx]; break;  /* none reads as input */
	}
	unsigned sw[4] = { (sr >> 14) & 3, (sr >> 12) & 3, (sr >> 10) & 3, (sr >> 8) & 3 };
	for (int i = 0; i < 4; i++) {
		float x = r[sw[i]];
		if (w0 & (1u << (21 + pos))) x = fabsf(x);
		if (sr & (1 << 16)) x = -x;
		v[i] = x;
	}
}

static int run(struct sim *s, const struct nv30_vertprog *vp, const uint32_t (*code)[4], unsigned n)
{
	for (unsigned pc = 0; pc < n; pc++) {
		const uint32_t *w = code[pc];
		unsigned vop = (w[1] >> 22) & 0x1f, sop = (w[1] >> 27) & 0x1f;
		unsigned in_idx = (w[1] >> 8) & 0xf, c_idx = (w[1] >> 12) & 0x1ff;
		uint32_t sr0 = ((w[1] & 0xff) << 9) | ((w[2] >> 23) & 0x1ff);
		uint32_t sr1 = (w[2] >> 6) & 0x1ffff;
		uint32_t sr2 = ((w[2] & 0x3f) << 11) | ((w[3] >> 21) & 0x7ff);
		float a[4], b[4], c[4], r[4];

		if (sop) { printf("scalar op %u at %u: not simulated\n", sop, pc); return -1; }
		if (w[0] & ((1 << 13) | (1 << 26) | (1 << 27))) { printf("cond/sat/indexed at %u\n", pc); return -1; }
		rd(s, sr0, 0, w[0], in_idx, c_idx, a);
		rd(s, sr1, 1, w[0], in_idx, c_idx, b);
		rd(s, sr2, 2, w[0], in_idx, c_idx, c);
		switch (vop) {
		case 0x01: memcpy(r, a, 16); break;
		case 0x02: for (int i = 0; i < 4; i++) r[i] = a[i] * b[i]; break;
		case 0x03: for (int i = 0; i < 4; i++) r[i] = a[i] + c[i]; break;
		case 0x04: for (int i = 0; i < 4; i++) r[i] = a[i] * b[i] + c[i]; break;
		case 0x05: r[0] = r[1] = r[2] = r[3] = a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; break;
		case 0x07: r[0] = r[1] = r[2] = r[3] = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; break;
		case 0x09: for (int i = 0; i < 4; i++) r[i] = a[i] < b[i] ? a[i] : b[i]; break;
		case 0x0a: for (int i = 0; i < 4; i++) r[i] = a[i] > b[i] ? a[i] : b[i]; break;
		case 0x0b: for (int i = 0; i < 4; i++) r[i] = a[i] < b[i] ? 1.0f : 0.0f; break;
		case 0x0c: for (int i = 0; i < 4; i++) r[i] = a[i] >= b[i] ? 1.0f : 0.0f; break;
		default: printf("vector op %u at %u: not simulated\n", vop, pc); return -1;
		}
		unsigned mask = (w[3] >> 13) & 0xf, dt = (w[0] >> 15) & 0x3f;
		for (int i = 0; i < 4; i++) {
			if (!(mask & (8 >> i))) continue;
			if (dt != 0x3f) s->temp[dt][i] = r[i];
			if (w[0] & (1u << 30)) s->out[(w[3] >> 2) & 31][i] = r[i];
		}
		if (w[3] & 1) return (int)pc + 1;
	}
	return (int)n;
}

static int translate(const char *text, uint64_t key, struct nv30_vertprog *vp, uint32_t (**code)[4])
{
	static struct tgsi_token toks[1024];
	memset(vp, 0, sizeof(*vp));
	if (!tgsi_text_translate(text, toks, 1024)) { printf("tgsi text\n"); return -1; }
	vp->pipe.tokens = toks;
	tgsi_scan_shader(toks, &vp->info);
	for (int i = 0; i < 10; i++) vp->texcoord[i] = 0xffff;
	vp->snorm8 = nv30_snorm8_key_for_inputs(key, vp->info.file_mask[TGSI_FILE_INPUT]);
	if (!_nvfx_vertprog_translate(0x4097, vp)) return -1;
	*code = malloc(vp->nr_insns * 16);
	memcpy(*code, vp->insns, vp->nr_insns * 16);
	struct nv30_shader_reloc *rel = vp->const_relocs.data;
	for (unsigned i = 0; i < vp->const_relocs.size / sizeof(*rel); i++) {
		(*code)[rel[i].location][1] &= ~0x0001ff000;
		(*code)[rel[i].location][1] |= (rel[i].target & 0x1ff) << 12;
	}
	if (getenv("VPSIM_DUMP"))
		for (unsigned i = 0; i < vp->nr_insns; i++)
			printf("%3u: %08x %08x %08x %08x  vop %u sop %u\n", i, (*code)[i][0], (*code)[i][1], (*code)[i][2], (*code)[i][3],
			       ((*code)[i][1] >> 22) & 0x1f, ((*code)[i][1] >> 27) & 0x1f);
	return (int)vp->nr_insns;
}

static void consts(struct sim *s, const struct nv30_vertprog *vp, const float user[16][4])
{
	for (unsigned k = 0; k < vp->nr_consts; k++)
		memcpy(s->c[k], vp->consts[k].index >= 0 ? user[vp->consts[k].index] : vp->consts[k].value, 16);
}

static const char *progs[] = {
"VERT\n"
"DCL IN[0]\nDCL IN[1]\nDCL IN[2]\n"
"DCL OUT[0], POSITION\nDCL OUT[1], COLOR\nDCL OUT[2], COLOR[1]\n"
"DCL CONST[0..7]\nDCL TEMP[0..1]\n"
"IMM[0] FLT32 { 0.5, 0.25, 2.0, 1.0}\n"
"  0: DP4 OUT[0].x, IN[0], CONST[0]\n"
"  1: DP4 OUT[0].y, IN[0], CONST[1]\n"
"  2: DP4 OUT[0].z, IN[0], CONST[2]\n"
"  3: DP4 OUT[0].w, IN[0], CONST[3]\n"
"  4: MAD TEMP[0], IN[1], CONST[4], IN[2]\n"
"  5: DP3 TEMP[1].x, IN[1], CONST[5]\n"
"  6: MUL TEMP[1].yzw, IN[2], IN[1]\n"
"  7: ADD OUT[1], TEMP[0], TEMP[1]\n"
"  8: MAX OUT[2], IN[2], IMM[0].xxxx\n"
"  9: END\n",
/* inputs read with swizzles, negated, twice in one instruction, and as abs */
"VERT\n"
"DCL IN[0]\nDCL IN[1]\nDCL IN[2]\nDCL IN[3]\n"
"DCL OUT[0], POSITION\nDCL OUT[1], COLOR\nDCL OUT[2], COLOR[1]\n"
"DCL CONST[0..7]\nDCL TEMP[0..2]\n"
"  0: MOV OUT[0], IN[0]\n"
"  1: MUL TEMP[0], IN[1].zyxw, -IN[1].wwxy\n"
"  2: ADD TEMP[1], IN[3].xxyy, |IN[2]|\n"
"  3: MAD TEMP[2], IN[2].wzyx, CONST[6], -IN[3]\n"
"  4: SGE TEMP[2].x, IN[1].yyyy, CONST[7]\n"
"  5: ADD OUT[1], TEMP[0], TEMP[1]\n"
"  6: MIN OUT[2], TEMP[2], IN[3]\n"
"  7: END\n",
};

int main(void)
{
	float user[16][4];
	srand(1);
	for (int i = 0; i < 16; i++) for (int j = 0; j < 4; j++) user[i][j] = (float)(rand() % 2001 - 1000) / 250.0f;

	/* keys: which inputs are SNORM8, with how many components */
	const uint64_t keys[] = {
		nv30_snorm8_key_set(nv30_snorm8_key_set(0, 1, 3), 2, 4),
		nv30_snorm8_key_set(0, 1, 4),
		nv30_snorm8_key_set(0, 2, 1),
		nv30_snorm8_key_set(nv30_snorm8_key_set(nv30_snorm8_key_set(0, 1, 2), 2, 3), 3, 4),
		nv30_snorm8_key_set(nv30_snorm8_key_set(0, 0, 4), 3, 1),
	};
	for (unsigned p = 0; p < sizeof(progs) / sizeof(progs[0]); p++) {
		for (unsigned k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
			struct nv30_vertprog vpa, vpb;
			uint32_t (*ca)[4], (*cb)[4];
			int na = translate(progs[p], 0, &vpa, &ca);
			int nb = translate(progs[p], keys[k], &vpb, &cb);
			CHECK(na > 0 && nb > 0, "prog %u key %llx: translation %d %d", p, (unsigned long long)keys[k], na, nb);
			if (na <= 0 || nb <= 0) continue;
			unsigned extra = 0;
			for (unsigned i = 0; i < 16; i++) extra += nv30_snorm8_key_nc(vpb.snorm8, i) ? 4 : 0;
			CHECK((unsigned)nb == (unsigned)na + extra, "prog %u key %llx: %d instructions, %d + %u expected", p,
			      (unsigned long long)keys[k], nb, na, extra);
			CHECK(vpa.ir == vpb.ir, "prog %u: input mask %x vs %x", p, vpa.ir, vpb.ir);
			for (int iter = 0; iter < 3000; iter++) {
				struct sim sa, sb;
				memset(&sa, 0, sizeof(sa)); memset(&sb, 0, sizeof(sb));
				consts(&sa, &vpa, user); consts(&sb, &vpb, user);
				for (unsigned i = 0; i < 16; i++) {
					unsigned nc = nv30_snorm8_key_nc(vpb.snorm8, i);
					uint8_t by[4];
					for (int j = 0; j < 4; j++) {
						by[j] = (uint8_t)rand();
						if (iter < 256) by[j] = (uint8_t)(iter + 37 * j);
					}
					for (int j = 0; j < 4; j++) {
						float def = j == 3 ? 1.0f : 0.0f;
						if (nc) {
							/* B: the fetch's raw bytes; A: util_format's unpack */
							sb.in[i][j] = (unsigned)j < nc ? (float)by[j] : def;
							sa.in[i][j] = (unsigned)j < nc ? fmaxf(-1.0f, (float)((int8_t)by[j] * (1.0f / 0x7f))) : def;
						} else {
							sa.in[i][j] = sb.in[i][j] = (float)(rand() % 2001 - 1000) / 100.0f;
						}
					}
				}
				int ra = run(&sa, &vpa, ca, na), rb = run(&sb, &vpb, cb, nb);
				CHECK(ra == na - 1 && rb == nb - 1, "prog %u: ran %d of %d, %d of %d", p, ra, na, rb, nb);
				for (int o = 0; o < 32; o++)
					CHECK(!memcmp(sa.out[o], sb.out[o], 16), "prog %u key %llx iter %d out %d: %a %a %a %a vs %a %a %a %a",
					      p, (unsigned long long)keys[k], iter, o, sa.out[o][0], sa.out[o][1], sa.out[o][2],
					      sa.out[o][3], sb.out[o][0], sb.out[o][1], sb.out[o][2], sb.out[o][3]);
				if (fails > 20) goto done;
			}
			free(ca); free(cb);
		}
	}
done:
	printf("vpsim: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
