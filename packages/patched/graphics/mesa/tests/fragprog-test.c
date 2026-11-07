/* fragprog-test PART: nvfx_fragprog.c's translation of two things, on the host (nvfx_fragprog.c is compiled from
 * build/work/mesa, TGSI text parsed by Mesa's own parser).
 *   txb   texture2D with a bias, TGSI TXB (0045, 0065): with nv30_fragprog::txb_hw off (the PS3's default) the
 *         instruction is a TEX; on (anywhere else, as the stock driver) the hardware's TXB, and both
 *         variants have the same length
 *   rep   a loop (0067): REP's end is the first instruction slot after the loop, also when the loop's last
 *         instruction reads a constant (its constant slot follows it, and is data): every offset the loop and the
 *         program name is an instruction slot of a walk through the program
 *   end   a program that closes an IF or a loop at its end (0068): END only on the appended NOP, so every branch
 *         target is at or before the program's first END (the checker's strict rule); a program that does not keeps
 *         END on its last instruction
 * (host-tests.sh fragprog) */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "tgsi/tgsi_text.h"
#include "tgsi/tgsi_scan.h"
#include "tgsi/tgsi_parse.h"
#include "util/u_cpu_detect.h"
#include "util/u_debug.h"
#include "util/half_float.h"
#include "util/log.h"
#include "util/u_printf.h"
#include "util/format/u_format.h"
#include "tgsi/tgsi_sanity.h"
#include "nv30/nv30_state.h"
#include "nv30/nv30-40_3d.xml.h"
#include "nv_object.xml.h"
#include "nv30/nvfx_shader.h"

void _nvfx_fragprog_translate(uint16_t oclass, struct nv30_fragprog *fp);

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

const char *debug_get_option_cached(const char *name, const char *dfault) { const char *v = getenv(name); return v ? v : dfault; }
bool debug_parse_bool_option(const char *str, bool dfault) { if (!str) return dfault; return !strcmp(str, "1") || !strcmp(str, "true") || !strcmp(str, "y"); }
bool debug_get_bool_option(const char *name, bool dfault) { return debug_parse_bool_option(getenv(name), dfault); }
void _debug_vprintf(const char *format, va_list ap) { vfprintf(stderr, format, ap); }
double _mesa_strtod(const char *s, char **end) { return strtod(s, end); }
float _mesa_strtof(const char *s, char **end) { return strtof(s, end); }
uint16_t _mesa_float_to_half_slow(float f) { (void)f; abort(); }
float _mesa_half_to_float_slow(uint16_t h) { (void)h; abort(); }
struct _util_cpu_caps_state_t _util_cpu_caps_state = { .detect_done = 1 };
void _util_cpu_detect_once(void) { }
const struct util_format_description *util_format_description(enum pipe_format f) { (void)f; return NULL; }
size_t u_printf_length(const char *f, va_list ap) { return vsnprintf(NULL, 0, f, ap); }
bool tgsi_sanity_check(const struct tgsi_token *t) { (void)t; return true; }
void mesa_log(enum mesa_log_level l, const char *tag, const char *f, ...)
{ va_list ap; va_start(ap, f); (void)l; fprintf(stderr, "%s: ", tag); vfprintf(stderr, f, ap); va_end(ap); }

static struct tgsi_token *parse(const char *text)
{
	struct tgsi_token *t = calloc(4096, sizeof(*t));
	if (!tgsi_text_translate(text, t, 4096)) {
		printf("TGSI does not parse:\n%s\n", text);
		exit(1);
	}
	return t;
}

static struct nv30_fragprog *translate(const struct tgsi_token *t, bool txb_hw)
{
	struct nv30_fragprog *fp = calloc(1, sizeof(*fp));
	fp->pipe.tokens = t;
	fp->txb_hw = txb_hw;
	tgsi_scan_shader(t, &fp->info);
	_nvfx_fragprog_translate(NV40_3D_CLASS, fp);
	return fp;
}

static unsigned opcode(const struct nv30_fragprog *fp, unsigned slot)
{
	return (fp->insn[slot * 4] & NVFX_FP_OP_OPCODE_MASK) >> NVFX_FP_OP_OPCODE_SHIFT;
}

static void part_txb(void)
{
	static const char text[] =
		"FRAG\n"
		"DCL IN[0], GENERIC[0], PERSPECTIVE\n"
		"DCL OUT[0], COLOR\n"
		"DCL SAMP[0]\n"
		"DCL SVIEW[0], 2D, FLOAT\n"
		"  0: TXB OUT[0], IN[0], SAMP[0], 2D\n"
		"  1: END\n";
	struct tgsi_token *t = parse(text);
	struct nv30_fragprog *off = translate(t, false), *on = translate(t, true);

	CHECK(off->translated && on->translated, "translated: %d %d", off->translated, on->translated);
	CHECK(opcode(off, 0) == NVFX_FP_OP_OPCODE_TEX, "txb_hw off: opcode %#x, want TEX (%#x)", opcode(off, 0),
	      NVFX_FP_OP_OPCODE_TEX);
	CHECK(opcode(on, 0) == NVFX_FP_OP_OPCODE_TXB, "txb_hw on: opcode %#x, want TXB (%#x)", opcode(on, 0),
	      NVFX_FP_OP_OPCODE_TXB);
	CHECK(off->insn_len == on->insn_len, "lengths %u %u", off->insn_len, on->insn_len);
}

/* walks the program the way the GPU does: one slot an instruction, and a constant slot after one that reads
 * a constant; returns a bitmap of the instruction slots (1 bit per 4 dwords, up to 64) */
static uint64_t walk(const struct nv30_fragprog *fp)
{
	uint64_t slots = 0;
	unsigned s = 0;

	while (s * 4 < fp->insn_len && s < 64) {
		const uint32_t *hw = &fp->insn[s * 4];
		unsigned consts = 0;

		slots |= 1ull << s;
		if (!(hw[2] & NV40_FP_OP_OPCODE_IS_BRANCH)) {
			/* source types: bits 0-1 of hw[1], hw[2], hw[3]: 2 is a constant */
			for (int i = 1; i <= 3; i++)
				consts |= (hw[i] & 3) == 2;
		}
		s += 1 + consts;
		if (hw[0] & 1)
			break;
	}
	return slots;
}

static void check_loop(const char *name, const char *text, unsigned rep_slot, unsigned end_slot)
{
	struct tgsi_token *t = parse(text);
	struct nv30_fragprog *fp = translate(t, false);
	uint64_t slots;
	unsigned rep = ~0u, n = 0;

	CHECK(fp->translated, "%s: not translated", name);
	slots = walk(fp);
	/* a program that ends inside the loop has its END on the loop's last instruction and the appended NOP+END
	 * after it (the last slot): the loop ends there, and it is an instruction slot too */
	slots |= 1ull << (fp->insn_len / 4 - 1);
	for (unsigned s = 0; s * 4 < fp->insn_len; s++) {
		const uint32_t *hw = &fp->insn[s * 4];
		if ((hw[2] & NV40_FP_OP_OPCODE_IS_BRANCH) && ((hw[0] >> NVFX_FP_OP_OPCODE_SHIFT) & 0x3f) == NV40_FP_OP_BRA_OPCODE_REP) {
			unsigned end = hw[3] & 0x7fffffff;

			rep = s;
			n++;
			CHECK(s == rep_slot && end == end_slot * 4, "%s: the loop at slot %u ends at dword %u, want slot %u and dword %u "
			      "(the first slot after the body and its constant)", name, s, end, rep_slot, end_slot * 4);
			CHECK(s * 4 < end, "%s: the loop ends before it begins", name);
			CHECK(end % 4 == 0 && (slots >> (end / 4) & 1), "%s: the loop at slot %u ends at dword %u, which is not an "
			      "instruction slot of the program", name, s, end);
		}
	}
	CHECK(n == 1 && rep != ~0u, "%s: %u loops found", name, n);
}

static void part_rep(void)
{
	/* the loop's last instruction reads a constant: its slot is followed by the constant's */
	/* (rep slot, end slot) in 16-byte slots: MOV+constant take 0-1, REP 2, ADD+constant 3-4, so the end is slot 5 */
	check_loop("constant last",
		"FRAG\n"
		"DCL OUT[0], COLOR\n"
		"DCL CONST[0]\n"
		"DCL TEMP[0]\n"
		"  0: MOV TEMP[0], CONST[0]\n"
		"  1: BGNLOOP\n"
		"  2:   ADD TEMP[0], TEMP[0], CONST[0]\n"
		"  3: ENDLOOP\n"
		"  4: MOV OUT[0], TEMP[0]\n"
		"  5: END\n", 2, 5);
	/* the last instruction reads no constant */
	check_loop("temp last",
		"FRAG\n"
		"DCL IN[0], GENERIC[0], PERSPECTIVE\n"
		"DCL OUT[0], COLOR\n"
		"DCL TEMP[0]\n"
		"  0: MOV TEMP[0], IN[0]\n"
		"  1: BGNLOOP\n"
		"  2:   ADD TEMP[0], TEMP[0], TEMP[0]\n"
		"  3: ENDLOOP\n"
		"  4: MOV OUT[0], TEMP[0]\n"
		"  5: END\n", 1, 3);
	/* a loop at the end of the program: the end is the NOP+END appended after it */
	check_loop("loop last",
		"FRAG\n"
		"DCL OUT[0], COLOR\n"
		"DCL CONST[0]\n"
		"DCL TEMP[0]\n"
		"  0: MOV OUT[0], CONST[0]\n"
		"  1: BGNLOOP\n"
		"  2:   ADD OUT[0], OUT[0], CONST[0]\n"
		"  3: ENDLOOP\n"
		"  4: END\n", 2, 5);
}

/* the slot of the first instruction with END set, walking as the GPU does */
static unsigned first_end(const struct nv30_fragprog *fp)
{
	unsigned s = 0;

	while (s * 4 < fp->insn_len) {
		const uint32_t *hw = &fp->insn[s * 4];
		unsigned consts = 0;

		if (hw[0] & 1)
			return s;
		if (!(hw[2] & NV40_FP_OP_OPCODE_IS_BRANCH))
			for (int i = 1; i <= 3; i++)
				consts |= (hw[i] & 3) == 2;
		s += 1 + consts;
	}
	return ~0u;
}

static void check_end(const char *name, const char *text, bool structured)
{
	struct tgsi_token *t = parse(text);
	struct nv30_fragprog *fp = translate(t, false);
	unsigned last = fp->insn_len / 4 - 1, end;

	CHECK(fp->translated, "%s: not translated", name);
	end = first_end(fp);
	if (structured)
		CHECK(end == last, "%s: the first END is slot %u, want the appended NOP+END (slot %u)", name, end, last);
	else
		CHECK(end == last - 1, "%s: the first END is slot %u, want the last instruction (slot %u)", name, end, last - 1);
	for (unsigned s = 0; s * 4 < fp->insn_len; s++) {
		const uint32_t *hw = &fp->insn[s * 4];
		if (!(hw[2] & NV40_FP_OP_OPCODE_IS_BRANCH))
			continue;
		unsigned op = (hw[0] >> NVFX_FP_OP_OPCODE_SHIFT) & 0x3f;
		if (op == NV40_FP_OP_BRA_OPCODE_IF)
			CHECK((hw[2] & 0x7fffffff) <= end * 4 && (hw[3] & 0x7fffffff) <= end * 4,
			      "%s: IF at slot %u jumps to %u and %u, past the first END (dword %u)", name, s, hw[2] & 0x7fffffff,
			      hw[3] & 0x7fffffff, end * 4);
		else if (op == NV40_FP_OP_BRA_OPCODE_REP)
			CHECK((hw[3] & 0x7fffffff) <= end * 4, "%s: REP at slot %u ends at %u, past the first END (dword %u)", name, s,
			      hw[3] & 0x7fffffff, end * 4);
	}
}

static void part_end(void)
{
	check_end("if/else last",
		"FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\nDCL TEMP[0]\n"
		"  0: SGT TEMP[0].x, IN[0].xxxx, IN[0].yyyy\n  1: IF TEMP[0].xxxx\n  2:   MOV OUT[0], IN[0]\n  3: ELSE\n"
		"  4:   MUL OUT[0], IN[0], IN[0]\n  5: ENDIF\n  6: END\n", true);
	check_end("if last",
		"FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\nDCL TEMP[0]\n"
		"  0: MOV OUT[0], IN[0]\n  1: SGT TEMP[0].x, IN[0].xxxx, IN[0].yyyy\n  2: IF TEMP[0].xxxx\n"
		"  3:   MUL OUT[0], IN[0], IN[0]\n  4: ENDIF\n  5: END\n", true);
	check_end("loop last",
		"FRAG\nDCL OUT[0], COLOR\nDCL CONST[0]\n"
		"  0: MOV OUT[0], CONST[0]\n  1: BGNLOOP\n  2:   ADD OUT[0], OUT[0], CONST[0]\n  3: ENDLOOP\n  4: END\n", true);
	/* a program that closes its structures before the last instruction keeps END there */
	check_end("mov last",
		"FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\nDCL TEMP[0]\n"
		"  0: SGT TEMP[0].x, IN[0].xxxx, IN[0].yyyy\n  1: IF TEMP[0].xxxx\n  2:   MUL TEMP[0], IN[0], IN[0]\n"
		"  3: ENDIF\n  4: MOV OUT[0], IN[0]\n  5: END\n", false);
	check_end("plain", "FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\n  0: MOV OUT[0], IN[0]\n  1: END\n", false);
}

/* the golden set: programs of the shapes games send, translated with the PS3's setting (txb_hw off); `dump`
 * prints each program's words so two builds of nvfx_fragprog.c can be compared (host-tests.sh fragprog golden) */
static const char *const golden_progs[] = {
	"FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\nDCL SAMP[0]\nDCL SVIEW[0], 2D, FLOAT\n"
	"  0: TEX OUT[0], IN[0], SAMP[0], 2D\n  1: END\n",
	"FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\nDCL SAMP[0]\nDCL SVIEW[0], 2D, FLOAT\n"
	"  0: TXB OUT[0], IN[0], SAMP[0], 2D\n  1: END\n",
	"FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\nDCL SAMP[0]\nDCL SVIEW[0], 2D, FLOAT\nDCL TEMP[0]\n"
	"  0: TXP TEMP[0], IN[0], SAMP[0], 2D\n  1: MUL OUT[0], TEMP[0], IN[0]\n  2: END\n",
	"FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\nDCL CONST[0]\nDCL TEMP[0]\n"
	"  0: MAD TEMP[0], IN[0], CONST[0], IN[0]\n  1: MOV OUT[0], TEMP[0]\n  2: END\n",
	"FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\nDCL TEMP[0]\n"
	"  0: MOV TEMP[0], IN[0]\n  1: BGNLOOP\n  2:   ADD TEMP[0], TEMP[0], TEMP[0]\n  3: ENDLOOP\n  4: MOV OUT[0], TEMP[0]\n  5: END\n",
	"FRAG\nDCL OUT[0], COLOR\nDCL CONST[0]\nDCL TEMP[0]\n"
	"  0: MOV TEMP[0], CONST[0]\n  1: BGNLOOP\n  2:   ADD TEMP[0], TEMP[0], CONST[0]\n  3: ENDLOOP\n  4: MOV OUT[0], TEMP[0]\n  5: END\n",
	"FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\nDCL TEMP[0]\n"
	"  0: MOV TEMP[0], IN[0]\n  1: SGT TEMP[0].x, IN[0].xxxx, IN[0].yyyy\n  2: IF TEMP[0].xxxx\n  3:   ADD TEMP[0], TEMP[0], IN[0]\n"
	"  4: ELSE\n  5:   MUL TEMP[0], TEMP[0], IN[0]\n  6: ENDIF\n  7: MOV OUT[0], TEMP[0]\n  8: END\n",
	"FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\nDCL TEMP[0]\n"
	"  0: KILL_IF IN[0]\n  1: MOV OUT[0], IN[0]\n  2: END\n",
};

static void part_dump(void)
{
	for (unsigned i = 0; i < sizeof(golden_progs) / sizeof(golden_progs[0]); i++) {
		struct tgsi_token *t = parse(golden_progs[i]);
		struct nv30_fragprog *fp = translate(t, false);

		printf("prog %u translated %d len %u\n", i, fp->translated, fp->insn_len);
		for (unsigned w = 0; w < fp->insn_len; w++)
			printf("%u %04x %08x\n", i, w, fp->insn[w]);
	}
}

int main(int argc, char **argv)
{
	const char *part = argc > 1 ? argv[1] : "";

	if (!strcmp(part, "dump")) {
		part_dump();
		return 0;
	}

	if (!strcmp(part, "txb"))
		part_txb();
	else if (!strcmp(part, "rep"))
		part_rep();
	else if (!strcmp(part, "end"))
		part_end();
	else {
		printf("usage: %s txb|rep|end\n", argv[0]);
		return 2;
	}
	printf("%s %s: %d checks, %d failed\n", fails ? "FAIL" : "ok", part, checks, fails);
	return fails != 0;
}
