/* point-smooth-test: nv30's round smooth points (Mesa patch "nv30: GL_POINT_SMOOTH points are
 * round", NV30_POINT_SMOOTH). nv30_point_smooth.c's TGSI transform and nvfx_fragprog.c are
 * compiled from build/work/mesa.
 *   1. The variant of a fragment program runs on Mesa's TGSI interpreter (tgsi_exec, 2x2 quads,
 *      so DDX is what a GPU computes) over the square a point of size S covers, its point
 *      coordinate interpolated across it as the point sprite's replacement makes it. Sizes 1 to
 *      64, centred on and between pixels. Every pixel centre farther than S/2 from the point's
 *      centre must be killed (the corners of the square), pixels a pixel inside the edge must keep
 *      the program's colour and alpha, rgb never changes, the edge's alpha falls with the
 *      distance, the summed coverage is the circle's area (within the edge), and the coverage
 *      is the same in all four quadrants. The plain program covers the whole square (the bug).
 *   2. The NV40 translation (nvfx) of the variant: it translates, the point coordinate gets one
 *      free texture coordinate with its replacement bit (point_sprite_control), the program's
 *      own texture coordinates are where they were, KIL is flagged in FP_CONTROL, and it costs
 *      at most 24 instruction slots more (21 now: the 15 instructions
 *      and their inline constants). A program using all ten texture coordinates gets no variant
 *      (the translation fails: the driver keeps the plain program).
 *   3. No variant for programs without COLOR[0], or with RET (the transform can't follow it).
 * (tools/run-tests.sh mesa-point-smooth; skipped without build/work/mesa with the patch) */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "tgsi/tgsi_text.h"
#include "tgsi/tgsi_scan.h"
#include "tgsi/tgsi_exec.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_dump.h"
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

void _nvfx_fragprog_translate(uint16_t oclass, struct nv30_fragprog *fp);

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* --- the Mesa utilities the TGSI code and nvfx call ------------------------------------------- */
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

/* --- 1. the variant on tgsi_exec ---------------------------------------------------------------- */
static const float in_color[4] = { 0.8f, 0.4f, 0.2f, 0.75f };

struct pix { int alive; float c[4]; };

/* runs tokens over the S x S square around (cx, cy) (pixel centres at .5), pcoord in input
 * slot pc_in (-1: none); result in a (2*W) x (2*W) image around the point */
#define W 40
static struct pix img[2 * W][2 * W];

static void run_point(const struct tgsi_token *tokens, float S, float cx, float cy, int pc_in,
		      const struct tgsi_shader_info *info)
{
	struct tgsi_exec_machine *m = tgsi_exec_machine_create(MESA_SHADER_FRAGMENT);
	struct tgsi_interp_coef coef[PIPE_MAX_SHADER_INPUTS];
	int color_out = -1;

	memset(coef, 0, sizeof(coef));
	for (unsigned i = 0; i < info->num_inputs; i++) {
		if (info->input_semantic_name[i] == TGSI_SEMANTIC_COLOR)
			memcpy(coef[i].a0, in_color, sizeof(in_color));
	}
	if (pc_in >= 0) {
		/* pc = (X - (cx - S/2)) / S, the sample position X */
		coef[pc_in].a0[0] = (S / 2 - cx) / S; coef[pc_in].dadx[0] = 1 / S;
		coef[pc_in].a0[1] = (S / 2 - cy) / S; coef[pc_in].dady[1] = 1 / S;
		coef[pc_in].a0[3] = 1;
	}
	for (unsigned i = 0; i < info->num_outputs; i++)
		if (info->output_semantic_name[i] == TGSI_SEMANTIC_COLOR && info->output_semantic_index[i] == 0)
			color_out = i;
	m->InterpCoefs = coef;
	m->flatshade_color = true;
	tgsi_exec_machine_bind_shader(m, tokens, NULL, NULL, NULL);

	memset(img, 0, sizeof(img));
	int x0 = (int)floorf(cx) - W, y0 = (int)floorf(cy) - W;
	for (int qy = 0; qy < 2 * W; qy += 2)
		for (int qx = 0; qx < 2 * W; qx += 2) {
			/* a quad the point's square touches (a pixel centre inside it) */
			int any = 0;
			for (int j = 0; j < 4; j++) {
				float X = x0 + qx + (j & 1) + 0.5f, Y = y0 + qy + (j >> 1) + 0.5f;
				if (fabsf(X - cx) <= S / 2 && fabsf(Y - cy) <= S / 2) any = 1;
			}
			if (!any)
				continue;
			m->QuadPos.xyzw[0].f[0] = x0 + qx + 0.5f;
			m->QuadPos.xyzw[1].f[0] = y0 + qy + 0.5f;
			m->NonHelperMask = 0xf;
			unsigned alive = tgsi_exec_machine_run(m, 0) & 0xf;
			for (int j = 0; j < 4; j++) {
				float X = x0 + qx + (j & 1) + 0.5f, Y = y0 + qy + (j >> 1) + 0.5f;
				struct pix *p = &img[qy + (j >> 1)][qx + (j & 1)];
				if (!(fabsf(X - cx) <= S / 2 && fabsf(Y - cy) <= S / 2))
					continue;	/* a helper pixel: not part of the point */
				p->alive = (alive >> j) & 1;
				for (int c = 0; c < 4; c++)
					p->c[c] = m->Outputs[color_out].xyzw[c].f[j];
			}
		}
	tgsi_exec_machine_destroy(m);
}

static int find_pcoord(const struct tgsi_shader_info *info)
{
	for (unsigned i = 0; i < info->num_inputs; i++)
		if (info->input_semantic_name[i] == TGSI_SEMANTIC_PCOORD)
			return i;
	return -1;
}

static void check_round(const char *name, const struct tgsi_token *plain, const struct tgsi_token *v,
			const float expect[4])
{
	static const float sizes[] = { 1, 2, 3, 3.5f, 5, 8, 13.25f, 21, 40, 64 };
	static const float offs[][2] = { { 0, 0 }, { 0.5f, 0.5f }, { 0.25f, 0.7f } };
	struct tgsi_shader_info pi, vi;
	tgsi_scan_shader(plain, &pi);
	tgsi_scan_shader(v, &vi);
	int pc = find_pcoord(&vi);
	CHECK(pc >= 0 && find_pcoord(&pi) < 0, "%s: the variant reads one new PCOORD input", name);

	for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
		for (unsigned o = 0; o < 3; o++) {
			float S = sizes[s], r = S / 2;
			float cx = 100 + offs[o][0], cy = 50 + offs[o][1];
			int x0 = (int)floorf(cx) - W, y0 = (int)floorf(cy) - W;
			int square = 0, outside_alive = 0, inside_wrong = 0, rgb_wrong = 0, ramp_wrong = 0;
			double cover = 0, quad[4] = { 0 };

			run_point(plain, S, cx, cy, -1, &pi);
			for (int y = 0; y < 2 * W; y++)
				for (int x = 0; x < 2 * W; x++) {
					float X = x0 + x + 0.5f, Y = y0 + y + 0.5f;
					if (fabsf(X - cx) <= r && fabsf(Y - cy) <= r)
						square += img[y][x].alive && img[y][x].c[3] == expect[3];
				}
			run_point(v, S, cx, cy, pc, &vi);
			int insq = 0;
			for (int y = 0; y < 2 * W; y++)
				for (int x = 0; x < 2 * W; x++) {
					float X = x0 + x + 0.5f, Y = y0 + y + 0.5f;
					if (!(fabsf(X - cx) <= r && fabsf(Y - cy) <= r))
						continue;
					insq++;
					struct pix *p = &img[y][x];
					float d = hypotf(X - cx, Y - cy);
					float a = p->alive ? p->c[3] : 0;
					if (d > r + 1e-4f && p->alive && p->c[3] > 0)
						outside_alive++;
					if (d <= r - 1 && (!p->alive || fabsf(p->c[3] - expect[3]) > 1e-5f))
						inside_wrong++;
					if (p->alive && (fabsf(p->c[0] - expect[0]) > 1e-6f || fabsf(p->c[1] - expect[1]) > 1e-6f ||
							 fabsf(p->c[2] - expect[2]) > 1e-6f))
						rgb_wrong++;
					/* the edge: alpha = clamp(r - d, 0, 1) * colour alpha */
					float want = fminf(fmaxf(r - d, 0), 1) * expect[3];
					if (p->alive && fabsf(a - want) > 1e-4f)
						ramp_wrong++;
					cover += a / expect[3];
					quad[(X >= cx) + 2 * (Y >= cy)] += (X == cx || Y == cy) ? 0 : a;
				}
			CHECK(square == insq, "%s S=%g: the plain program covers the square (%d of %d)", name, S, square, insq);
			CHECK(!outside_alive, "%s S=%g at +%g,%g: %d pixels outside the circle drawn", name, S,
			      offs[o][0], offs[o][1], outside_alive);
			CHECK(!inside_wrong, "%s S=%g at +%g,%g: %d pixels inside the circle lost or not opaque", name, S,
			      offs[o][0], offs[o][1], inside_wrong);
			CHECK(!rgb_wrong, "%s S=%g: rgb changed in %d pixels", name, S, rgb_wrong);
			CHECK(!ramp_wrong, "%s S=%g at +%g,%g: %d edge pixels off clamp(r - d)", name, S,
			      offs[o][0], offs[o][1], ramp_wrong);
			if (S >= 3) {
				/* clamp(r - d, 0, 1) over the plane: pi (r - 1)^2 + the ring, r - 1/3 for its mean
				 * radius ~ pi (r^2 - r + 1/3) for r >= 1 */
				double area = M_PI * (r * r - r + 1.0 / 3);
				CHECK(fabs(cover - area) <= 0.5 * r + 1, "%s S=%g at +%g,%g: coverage %.2f, the circle's %.2f", name, S,
				      offs[o][0], offs[o][1], cover, area);
				CHECK(cover < 0.95 * insq, "%s S=%g: coverage %.1f of the square's %d: not round", name, S, cover, insq);
			}
			if (o == 1 && S >= 2) {	/* centred between pixels: the four quadrants alike */
				double mn = fmin(fmin(quad[0], quad[1]), fmin(quad[2], quad[3]));
				double mx = fmax(fmax(quad[0], quad[1]), fmax(quad[2], quad[3]));
				CHECK(mx - mn < 1e-3, "%s S=%g: quadrants %.3f %.3f %.3f %.3f", name, S, quad[0], quad[1], quad[2], quad[3]);
			}
			if (S == 1) {
				int any = 0;
				for (int y = 0; y < 2 * W; y++)
					for (int x = 0; x < 2 * W; x++)
						any += img[y][x].alive && img[y][x].c[3] > 0;
				CHECK(any || o != 1, "%s S=1 at +%g,%g: the point vanished", name, offs[o][0], offs[o][1]);
			}
		}
}

/* --- 2. the NV40 translation -------------------------------------------------------------------- */
static struct nv30_fragprog *translate(const struct tgsi_token *t)
{
	struct nv30_fragprog *fp = calloc(1, sizeof(*fp));
	fp->pipe.tokens = t;
	tgsi_scan_shader(t, &fp->info);
	_nvfx_fragprog_translate(NV40_3D_CLASS, fp);
	return fp;
}

static void check_nv40(const char *name, const struct tgsi_token *plain, const struct tgsi_token *v)
{
	struct nv30_fragprog *p = translate(plain), *q = translate(v);
	CHECK(p->translated && q->translated, "%s: translated (plain %d, variant %d)", name, p->translated, q->translated);
	if (!p->translated || !q->translated)
		return;
	int pcslot = -1, moved = 0;
	for (int i = 0; i < 10; i++) {
		if (q->texcoord[i] == 0xfffe && p->texcoord[i] == 0xffff)
			pcslot = i;
		else if (q->texcoord[i] != p->texcoord[i])
			moved++;
	}
	CHECK(pcslot >= 0 && pcslot < 8, "%s: the point coordinate in a free texture coordinate 0-7 (%d)", name, pcslot);
	CHECK(!moved, "%s: %d of the program's texture coordinates moved", name, moved);
	CHECK(p->point_sprite_control == 0 && pcslot >= 0 &&
	      q->point_sprite_control == (NV30_3D_POINT_SPRITE_COORD_REPLACE_0 << pcslot),
	      "%s: point_sprite_control %#x for slot %d", name, q->point_sprite_control, pcslot);
	CHECK(pcslot < 0 || (q->texcoords == (p->texcoords | (1u << pcslot)) &&
			     q->vp_or == (p->vp_or | (0x00004000u << pcslot))),
	      "%s: texcoords %#x vp_or %#x (plain %#x %#x)", name, q->texcoords, q->vp_or, p->texcoords, p->vp_or);
	CHECK(q->fp_control & NV30_3D_FP_CONTROL_USES_KIL, "%s: FP_CONTROL without USES_KIL", name);
	CHECK(q->insn_len > p->insn_len && q->insn_len - p->insn_len <= 24 * 4,
	      "%s: %u instructions, plain %u", name, q->insn_len / 4, p->insn_len / 4);
	printf("  %-12s NV40 instruction slots: plain %u, smooth %u; point coordinate in texcoord %d\n", name,
	       p->insn_len / 4, q->insn_len / 4, pcslot);
}

static const char *ff_color =	/* Quake II's particles: fixed function, no texture */
	"FRAG\n"
	"DCL IN[0], COLOR, COLOR\n"
	"DCL OUT[0], COLOR\n"
	"  0: MOV OUT[0], IN[0]\n"
	"  1: END\n";
static const char *ff_tex =	/* texture times colour, a texture coordinate as GENERIC */
	"FRAG\n"
	"DCL IN[0], COLOR, COLOR\n"
	"DCL IN[1], GENERIC[0], PERSPECTIVE\n"
	"DCL OUT[0], COLOR\n"
	"DCL SAMP[0]\n"
	"DCL SVIEW[0], 2D, FLOAT\n"
	"DCL TEMP[0]\n"
	"  0: TEX TEMP[0], IN[1], SAMP[0], 2D\n"
	"  1: MUL OUT[0], TEMP[0], IN[0]\n"
	"  2: END\n";
static const char *arith =	/* temporaries and immediates of its own, a saturated write */
	"FRAG\n"
	"DCL IN[0], COLOR, COLOR\n"
	"DCL OUT[0], COLOR\n"
	"DCL TEMP[0..1]\n"
	"IMM[0] FLT32 { 0.5, 2.0, 0.25, 1.0 }\n"
	"  0: MUL TEMP[0], IN[0], IMM[0].xxxx\n"
	"  1: ADD TEMP[1], TEMP[0], TEMP[0]\n"
	"  2: MOV_SAT OUT[0].xyz, TEMP[1]\n"
	"  3: MUL OUT[0].w, IN[0].wwww, IMM[0].wwww\n"
	"  4: END\n";
static const char *mrt =	/* COLOR[1] is left alone */
	"FRAG\n"
	"DCL IN[0], COLOR, COLOR\n"
	"DCL OUT[0], COLOR\n"
	"DCL OUT[1], COLOR[1]\n"
	"  0: MOV OUT[0], IN[0]\n"
	"  1: MOV OUT[1], IN[0]\n"
	"  2: END\n";
static const char *full_tc =	/* every texture coordinate taken: nothing left for the point's */
	"FRAG\n"
	"DCL IN[0], GENERIC[0], PERSPECTIVE\nDCL IN[1], GENERIC[1], PERSPECTIVE\n"
	"DCL IN[2], GENERIC[2], PERSPECTIVE\nDCL IN[3], GENERIC[3], PERSPECTIVE\n"
	"DCL IN[4], GENERIC[4], PERSPECTIVE\nDCL IN[5], GENERIC[5], PERSPECTIVE\n"
	"DCL IN[6], GENERIC[6], PERSPECTIVE\nDCL IN[7], GENERIC[7], PERSPECTIVE\n"
	"DCL IN[8], GENERIC[8], PERSPECTIVE\nDCL IN[9], GENERIC[9], PERSPECTIVE\n"
	"DCL OUT[0], COLOR\nDCL TEMP[0]\n"
	"  0: ADD TEMP[0], IN[0], IN[1]\n  1: ADD TEMP[0], TEMP[0], IN[2]\n  2: ADD TEMP[0], TEMP[0], IN[3]\n"
	"  3: ADD TEMP[0], TEMP[0], IN[4]\n  4: ADD TEMP[0], TEMP[0], IN[5]\n  5: ADD TEMP[0], TEMP[0], IN[6]\n"
	"  6: ADD TEMP[0], TEMP[0], IN[7]\n  7: ADD TEMP[0], TEMP[0], IN[8]\n  8: ADD OUT[0], TEMP[0], IN[9]\n"
	"  9: END\n";
static const char *depth_only =
	"FRAG\n"
	"DCL IN[0], COLOR, COLOR\n"
	"DCL OUT[0], POSITION\n"
	"  0: MOV OUT[0].z, IN[0].xxxx\n"
	"  1: END\n";
static const char *with_ret =
	"FRAG\n"
	"DCL IN[0], COLOR, COLOR\n"
	"DCL OUT[0], COLOR\n"
	"  0: MOV OUT[0], IN[0]\n"
	"  1: RET\n"
	"  2: END\n";

int main(void)
{
	struct { const char *name, *text; float expect[4]; int exec; } progs[] = {
		{ "ff-color", ff_color, { 0.8f, 0.4f, 0.2f, 0.75f }, 1 },
		{ "ff-texture", ff_tex, { 0 }, 0 },
		{ "arith", arith, { 0.8f, 0.4f, 0.2f, 0.75f }, 1 },
		{ "mrt", mrt, { 0.8f, 0.4f, 0.2f, 0.75f }, 1 },
	};

	for (unsigned i = 0; i < sizeof(progs) / sizeof(progs[0]); i++) {
		struct tgsi_token *t = parse(progs[i].text);
		struct tgsi_token *v = nv30_point_smooth_tokens(t);
		CHECK(v != NULL, "%s: no variant", progs[i].name);
		if (!v)
			continue;
		if (getenv("DUMP"))
			tgsi_dump(v, 0);
		if (progs[i].exec)
			check_round(progs[i].name, t, v, progs[i].expect);
		check_nv40(progs[i].name, t, v);
		free(v);
		free(t);
	}

	/* MRT: COLOR[1] keeps the colour with its alpha */
	{
		struct tgsi_token *t = parse(mrt), *v = nv30_point_smooth_tokens(t);
		struct tgsi_shader_info vi;
		tgsi_scan_shader(v, &vi);
		struct tgsi_exec_machine *m = tgsi_exec_machine_create(MESA_SHADER_FRAGMENT);
		struct tgsi_interp_coef coef[PIPE_MAX_SHADER_INPUTS] = { 0 };
		int pc = find_pcoord(&vi);
		memcpy(coef[0].a0, in_color, sizeof(in_color));
		coef[pc].a0[0] = 0.0f; coef[pc].dadx[0] = 1 / 8.0f;	/* a quad at the left edge of an 8 pixel point */
		coef[pc].a0[1] = 0.0f; coef[pc].dady[1] = 1 / 8.0f;
		m->InterpCoefs = coef;
		m->flatshade_color = true;
		tgsi_exec_machine_bind_shader(m, v, NULL, NULL, NULL);
		m->QuadPos.xyzw[0].f[0] = 0.5f; m->QuadPos.xyzw[1].f[0] = 3.5f;
		m->NonHelperMask = 0xf;
		tgsi_exec_machine_run(m, 0);
		CHECK(m->Outputs[1].xyzw[3].f[0] == in_color[3] && m->Outputs[0].xyzw[3].f[0] < in_color[3],
		      "mrt: COLOR[1].w %g (want %g), COLOR[0].w %g (want less)", m->Outputs[1].xyzw[3].f[0], in_color[3],
		      m->Outputs[0].xyzw[3].f[0]);
		tgsi_exec_machine_destroy(m);
		free(v);
		free(t);
	}

	/* no texture coordinate left: the variant doesn't translate (nv30 keeps the plain program) */
	{
		struct tgsi_token *t = parse(full_tc), *v = nv30_point_smooth_tokens(t);
		CHECK(v != NULL, "full-tc: the transform itself works");
		if (v) {
			struct nv30_fragprog *p = translate(t), *q = translate(v);
			CHECK(p->translated && !q->translated, "full-tc: plain %d, variant %d (want 1, 0)", p->translated, q->translated);
			free(v);
		}
		free(t);
	}

	/* nothing to do */
	{
		struct tgsi_token *t = parse(depth_only);
		CHECK(nv30_point_smooth_tokens(t) == NULL, "depth-only: a variant without COLOR[0]");
		free(t);
		t = parse(with_ret);
		CHECK(nv30_point_smooth_tokens(t) == NULL, "with-ret: a variant of a program with RET");
		free(t);
	}

	printf("point-smooth-test: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
