/* idx-split-test: Mesa 0072 (nv30_idx_split.h): on the PS3 an indexed draw whose indices pass ps3rsx's
 * per-binding cap (1 MiB) is drawn as pieces. Parts:
 *   model: nv30_idx_split and nv30_idx_convert against a model of GL's primitives. For every mode,
 *     random counts, restart on and off (restart indices at random places, runs of them, at the ends),
 *     U8/U16/U32 and small piece sizes (many cuts): the primitives of the pieces, each drawn alone as
 *     its mode (fans, polygons and loops as the converted list), are the draw's primitives, in order,
 *     each with its winding and the vertex its flat colour comes from (last and first conventions);
 *   cap: draws past the cap at their real size: a 2 MiB U32 triangle list (midprim's 524032 indices),
 *     a U16 list over 1 MiB, a U32 strip and a U16 fan crossing the cap, a restarted strip: every
 *     piece, bound 256-aligned at or below its first index, spans at most 1 MiB with the kernel's skew
 *     (ps3rsx's ib_close: the copy starts (first * size) % 256 bytes early), and the model holds.
 * The fix-off runs (host-tests.sh) build this against nv30_idx_split.h with a rule taken out.
 *   idx-split-test [model|cap] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nv30/nv30_idx_split.h"

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; if (fails < 20) { printf("FAIL %s:%d: ", \
	__FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* --- the model: a draw's primitives as (provoking vertex first, then the rest in winding order) -- */
struct prim { uint32_t v[4]; unsigned n; };
struct plist { struct prim *p; size_t n, cap; };

static void padd(struct plist *l, unsigned n, const uint32_t *v, unsigned prov)
{
	struct prim q;

	if (l->n == l->cap) {
		l->cap = l->cap ? 2 * l->cap : 1024;
		l->p = realloc(l->p, l->cap * sizeof(*l->p));
	}
	/* rotate so the provoking vertex is first: triangles and quads keep their cyclic order,
	 * a line has none */
	q.n = n;
	for (unsigned i = 0; i < n; i++)
		q.v[i] = v[(prov + i) % n];
	l->p[l->n++] = q;
}

/* GL's primitives of a run (no restart inside) of n vertices v, mode, convention first */
static void run_prims(struct plist *l, unsigned mode, const uint32_t *v, unsigned n, bool first)
{
	uint32_t t[4];

	switch (mode) {
	case MESA_PRIM_POINTS:
		for (unsigned i = 0; i < n; i++)
			padd(l, 1, v + i, 0);
		break;
	case MESA_PRIM_LINES:
		for (unsigned i = 0; i + 1 < n; i += 2)
			padd(l, 2, v + i, first ? 0 : 1);
		break;
	case MESA_PRIM_LINE_STRIP:
	case MESA_PRIM_LINE_LOOP:
		for (unsigned i = 0; i + 1 < n; i++)
			padd(l, 2, v + i, first ? 0 : 1);
		if (mode == MESA_PRIM_LINE_LOOP && n >= 2) {
			t[0] = v[n - 1]; t[1] = v[0];
			padd(l, 2, t, first ? 0 : 1);
		}
		break;
	case MESA_PRIM_TRIANGLES:
		for (unsigned i = 0; i + 2 < n; i += 3)
			padd(l, 3, v + i, first ? 0 : 2);
		break;
	case MESA_PRIM_TRIANGLE_STRIP:
		for (unsigned i = 0; i + 2 < n; i++) {
			if (i & 1) { t[0] = v[i + 1]; t[1] = v[i]; } else { t[0] = v[i]; t[1] = v[i + 1]; }
			t[2] = v[i + 2];
			padd(l, 3, t, 2);	/* (pieces keep their mode: one convention is enough) */
		}
		break;
	case MESA_PRIM_TRIANGLE_FAN:
		for (unsigned i = 1; i + 1 < n; i++) {
			t[0] = v[0]; t[1] = v[i]; t[2] = v[i + 1];
			padd(l, 3, t, first ? 1 : 2);	/* GL: vertex i + 1 (first: i), counting from 1 */
		}
		break;
	case MESA_PRIM_POLYGON:
		for (unsigned i = 1; i + 1 < n; i++) {
			t[0] = v[0]; t[1] = v[i]; t[2] = v[i + 1];
			padd(l, 3, t, 0);	/* vertex 1 either way */
		}
		break;
	case MESA_PRIM_QUADS:
		for (unsigned i = 0; i + 3 < n; i += 4)
			padd(l, 4, v + i, 3);
		break;
	case MESA_PRIM_QUAD_STRIP:
		for (unsigned i = 0; i + 3 < n; i += 2) {
			t[0] = v[i]; t[1] = v[i + 1]; t[2] = v[i + 3]; t[3] = v[i + 2];
			padd(l, 4, t, 2);
		}
		break;
	}
}

/* the draw's primitives: runs between restart indices, each its own primitive */
static void draw_prims(struct plist *l, unsigned mode, const void *idx, unsigned isz, unsigned count,
		       bool restart, uint32_t rst, bool first)
{
	uint32_t *v = malloc(sizeof(*v) * (count + 1));
	unsigned n = 0;

	for (unsigned i = 0; i <= count; i++) {
		if (i == count || (restart && nv30_idx_get(idx, isz, i) == rst)) {
			run_prims(l, mode, v, n, first);
			n = 0;
		} else {
			v[n++] = nv30_idx_get(idx, isz, i);
		}
	}
	free(v);
}

static bool same(const struct plist *a, const struct plist *b, size_t *at)
{
	size_t i;

	for (i = 0; i < a->n && i < b->n; i++)
		if (a->p[i].n != b->p[i].n || memcmp(a->p[i].v, b->p[i].v, a->p[i].n * 4))
			break;
	*at = i;
	return a->n == b->n && i == a->n;
}

/* --- the pieces ------------------------------------------------------------------------------ */
struct pieces {
	const void *idx;
	unsigned isz, mode, max, n, bad_len, bad_bytes, bad_order, next;
	bool restart, first;
	uint32_t rst;
	struct plist got;
};

static void piece(void *ctx, unsigned start, unsigned count)
{
	struct pieces *p = ctx;
	uint64_t pos = (uint64_t)start * p->isz;

	p->n++;
	if (!count || count > p->max)
		p->bad_len++;
	/* bound at the 256-byte step at or below it, as nv30_draw_elements binds it: the kernel's
	 * copy starts (pos % 256) bytes before the piece */
	if ((pos & 255) + (uint64_t)count * p->isz > NV30_PS3_IDX_BINDING_MAX ||
	    nv30_idx_copy_bytes(pos & 255, count, p->isz) > NV30_PS3_IDX_SUBMIT_BYTES)
		p->bad_bytes++;	/* (and alone in a submission, its copy within the file's share) */
	if (start > p->next)	/* a gap: indices no piece draws */
		p->bad_order++;
	p->next = start + count;
	draw_prims(&p->got, p->mode, (const uint8_t *)p->idx + pos, p->isz, count, p->restart, p->rst,
		   p->first);
}

/* one draw through nv30_idx_split (after nv30_idx_convert for a fan, polygon or loop) against the
 * model; max: the piece size in indices (0: the PS3's) */
static void one(const char *what, unsigned mode, const void *idx, unsigned isz, unsigned count,
		bool restart, uint32_t rst, bool first, unsigned max)
{
	struct plist want = { 0 };
	struct pieces p = { .isz = isz, .mode = mode, .restart = restart, .rst = rst, .first = first };
	uint32_t *conv = NULL;
	unsigned n, cnt = count;
	size_t at;

	draw_prims(&want, mode, idx, isz, count, restart, rst, first);
	p.idx = idx;
	if (nv30_idx_convertible(mode)) {
		conv = malloc(4 * (size_t)nv30_idx_convert_max(mode, count) + 4);
		cnt = nv30_idx_convert(mode, count, restart, rst, idx, isz, first, conv);
		CHECK(cnt <= nv30_idx_convert_max(mode, count), "%s: converted past its room", what);
		p.idx = conv;
		p.isz = 4;
		p.mode = nv30_idx_convert_mode(mode);
		p.restart = false;	/* the restart indices are gone */
	}
	p.max = max ? max : NV30_PS3_IDX_PIECE_BYTES / p.isz;
	if (cnt) {
		n = nv30_idx_split(p.mode, cnt, p.max, p.restart, rst, p.idx, p.isz, piece, &p);
		CHECK(n == p.n && n > 0, "%s: %u pieces returned, %u drawn", what, n, p.n);
	}
	CHECK(!p.bad_len && !p.bad_bytes && !p.bad_order, "%s: %u pieces over the size, %u over the kernel's "
	      "cap, %u after a gap", what, p.bad_len, p.bad_bytes, p.bad_order);
	CHECK(!cnt || p.next == cnt, "%s: the pieces end at %u of %u", what, p.next, cnt);
	CHECK(same(&want, &p.got, &at), "%s: %zu primitives drawn, %zu wanted, first difference at %zu", what,
	      p.got.n, want.n, at);
	free(want.p);
	free(p.got.p);
	free(conv);
}

static const unsigned modes[] = { MESA_PRIM_POINTS, MESA_PRIM_LINES, MESA_PRIM_LINE_LOOP, MESA_PRIM_LINE_STRIP,
	MESA_PRIM_TRIANGLES, MESA_PRIM_TRIANGLE_STRIP, MESA_PRIM_TRIANGLE_FAN, MESA_PRIM_QUADS,
	MESA_PRIM_QUAD_STRIP, MESA_PRIM_POLYGON };
static const char *const names[] = { "points", "lines", "line loop", "line strip", "triangles",
	"triangle strip", "triangle fan", "quads", "quad strip", "polygon" };

static uint32_t rnd(void)
{
	static uint64_t s = 0x9e3779b97f4a7c15ull;

	s ^= s << 13; s ^= s >> 7; s ^= s << 17;
	return (uint32_t)(s >> 16);
}

static void fill(void *idx, unsigned isz, unsigned count, bool restart, uint32_t rst, unsigned rate)
{
	for (unsigned i = 0; i < count; i++) {
		uint32_t v = rnd() % 1000;

		if (restart && rate && rnd() % rate == 0)
			v = rst;
		if (isz == 1) ((uint8_t *)idx)[i] = (uint8_t)v;
		else if (isz == 2) ((uint16_t *)idx)[i] = (uint16_t)v;
		else ((uint32_t *)idx)[i] = v;
	}
}

static void part_model(void)
{
	static uint8_t buf[4 * 4096];
	char what[128];

	for (int it = 0; it < 4000; it++) {
		unsigned k = rnd() % 10, isz = (unsigned[]){ 1, 2, 4 }[rnd() % 3], count = 1 + rnd() % 600;
		bool restart = rnd() & 1, first = rnd() & 1;
		uint32_t rst = isz == 1 ? 0xff : isz == 2 ? 0xffff : (rnd() & 1 ? 0xffffffffu : 7);
		unsigned rate = (unsigned[]){ 0, 3, 10, 50 }[rnd() % 4], max = 8 + rnd() % 60;

		fill(buf, isz, count, restart, rst, rate);
		if (restart && rate && (rnd() & 1))	/* restarts at the ends, runs of them */
			for (unsigned i = 0; i < 3 && i < count; i++) {
				unsigned at = (unsigned[]){ 0, count - 1, count / 2 }[i];

				if (isz == 1) buf[at] = (uint8_t)rst;
				else if (isz == 2) ((uint16_t *)buf)[at] = (uint16_t)rst;
				else ((uint32_t *)buf)[at] = rst;
			}
		snprintf(what, sizeof(what), "model %d: %s, %u x U%u, restart %d (1/%u), max %u%s", it, names[k],
			 count, 8 * isz, restart, rate, max, first ? ", first" : "");
		one(what, modes[k], buf, isz, count, restart, rst, first, max);
	}
}

static void part_cap(void)
{
	const unsigned n32 = 524032, n16 = 600000;
	uint32_t *i32 = malloc(4ull * n32);
	uint16_t *i16 = malloc(2ull * n16);

	for (unsigned i = 0; i < n32; i++)
		i32[i] = i % 3;			/* midprim's elements draw */
	one("2 MiB U32 triangles (midprim)", MESA_PRIM_TRIANGLES, i32, 4, n32, false, 0, false, 0);
	for (unsigned i = 0; i < n16; i++)
		i16[i] = (uint16_t)(i % 4000);
	one("1.14 MiB U16 triangles", MESA_PRIM_TRIANGLES, i16, 2, n16 - n16 % 3, false, 0, false, 0);
	for (unsigned i = 0; i < n32; i++)
		i32[i] = i % 5000;
	one("2 MiB U32 strip", MESA_PRIM_TRIANGLE_STRIP, i32, 4, n32, false, 0, false, 0);
	one("2 MiB U32 strip, one count odd", MESA_PRIM_TRIANGLE_STRIP, i32, 4, n32 - 1, false, 0, false, 0);
	one("1.14 MiB U16 fan", MESA_PRIM_TRIANGLE_FAN, i16, 2, n16, false, 0, false, 0);
	one("1.14 MiB U16 fan, first", MESA_PRIM_TRIANGLE_FAN, i16, 2, n16, false, 0, true, 0);
	one("2 MiB U32 line loop", MESA_PRIM_LINE_LOOP, i32, 4, n32, false, 0, false, 0);
	/* restart: a strip restarted every 1000 indices, and one long strip restarted once past the cap */
	for (unsigned i = 0; i < n32; i++)
		i32[i] = i % 1000 == 999 ? 0xffffffffu : i % 5000;
	one("2 MiB U32 strip restarted every 1000", MESA_PRIM_TRIANGLE_STRIP, i32, 4, n32, true, 0xffffffffu, false, 0);
	for (unsigned i = 0; i < n32; i++)
		i32[i] = i == 300001 ? 0xffffffffu : i % 5000;
	one("2 MiB U32 strip restarted once, at an odd place", MESA_PRIM_TRIANGLE_STRIP, i32, 4, n32, true,
	    0xffffffffu, false, 0);
	for (unsigned i = 0; i < n16; i++)
		i16[i] = i % 777 == 776 ? 0xffff : (uint16_t)(i % 4000);
	one("1.14 MiB U16 triangles restarted every 777", MESA_PRIM_TRIANGLES, i16, 2, n16, true, 0xffff, false, 0);
	one("1.14 MiB U16 fans restarted every 777", MESA_PRIM_TRIANGLE_FAN, i16, 2, n16, true, 0xffff, false, 0);
	free(i32);
	free(i16);
}

int main(int argc, char **argv)
{
	const char *part = argc > 1 ? argv[1] : "all";

	if (!strcmp(part, "model") || !strcmp(part, "all"))
		part_model();
	if (!strcmp(part, "cap") || !strcmp(part, "all"))
		part_cap();
	printf("idx-split-test %s: %d checks, %d failed\n", part, checks, fails);
	return fails != 0;
}
