/* vtx-snorm8-test: NV30_VTX_SNORM8 on the host (Mesa patch "signed 8-bit vertex attributes
 * normalized in the vertex program", src/gallium/drivers/nouveau/nv30/nv30_vtx_snorm8.h).
 * The GPU fetches R8.._SNORM attributes as unsigned bytes (U8_USCALED: 0..255, the missing
 * components 0, 0, 1) and four vertex program instructions normalize them; u_vbuf converted
 * them on the CPU with util_format's unpack before. Checked here, for the four formats and
 * every byte in every component, bit for bit:
 *  - the prologue (SGE, MAD on all four components; MUL, MAX on the attribute's own) in IEEE
 *    single precision against util_format's generated unpack functions (taken from
 *    u_format_table.py's output by tools/run-tests.sh into snorm8-ref.h);
 *  - constant attributes (stride 0), emitted as their raw bytes, through the same prologue;
 *  - the per-input key: set, get, and kept to the inputs a program declares.
 * The RSX's own multiply is not checked here: if it rounds differently from IEEE, a component
 * can be one ulp off (the PS3 check is the picture, docs/TESTS.md).
 *   tools/run-tests.sh mesa-vtx-snorm8 (skipped without build/work/mesa) */
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "nv30/nv30_vtx_snorm8.h"

#define MAX2(a, b) ((a) > (b) ? (a) : (b))
#include "snorm8-ref.h"

static int fails, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef void (*unpack_fn)(void *restrict, const uint8_t *restrict, unsigned);

static const struct {
	const char *name;
	unsigned nc;
	unpack_fn unpack;
} fmts[] = {
	{ "R8_SNORM", 1, util_format_r8_snorm_unpack_rgba_float },
	{ "R8G8_SNORM", 2, util_format_r8g8_snorm_unpack_rgba_float },
	{ "R8G8B8_SNORM", 3, util_format_r8g8b8_snorm_unpack_rgba_float },
	{ "R8G8B8A8_SNORM", 4, util_format_r8g8b8a8_snorm_unpack_rgba_float },
};

/* what the vertex program has in the input's temp after the prologue */
static void prologue(const float in[4], unsigned nc, float out[4])
{
	for (unsigned c = 0; c < 4; c++) {
		volatile float s = in[c] >= 128.0f ? 1.0f : 0.0f;  /* SGE, all components */
		volatile float t = s * -256.0f + in[c];             /* MAD, all components */
		if (c < nc)
			t = nv30_snorm8_vp(in[c]);                  /* MUL, MAX on the attribute's */
		out[c] = t;
	}
}

/* the fetch: U8_USCALED of nc bytes, the rest as the vertex attribute defaults */
static void fetch(const uint8_t *b, unsigned nc, float in[4])
{
	const float def[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	for (unsigned c = 0; c < 4; c++)
		in[c] = c < nc ? (float)b[c] : def[c];
}

static void check_bytes(unsigned f, const uint8_t b[4], const char *what)
{
	float ref[4], in[4], got[4];

	fmts[f].unpack(ref, b, 1);
	fetch(b, fmts[f].nc, in);
	prologue(in, fmts[f].nc, got);
	CHECK(!memcmp(ref, got, sizeof(ref)), "%s %s bytes %02x %02x %02x %02x: unpack %a %a %a %a, program %a %a %a %a",
	      fmts[f].name, what, b[0], b[1], b[2], b[3], ref[0], ref[1], ref[2], ref[3],
	      got[0], got[1], got[2], got[3]);

	/* a constant attribute: VTX_ATTR_nF of the raw bytes, then the same program */
	for (unsigned c = 0; c < fmts[f].nc; c++)
		in[c] = nv30_snorm8_raw(b[c]);
	prologue(in, fmts[f].nc, got);
	CHECK(!memcmp(ref, got, sizeof(ref)), "%s %s constant bytes %02x..: unpack and program differ",
	      fmts[f].name, what, b[0]);
}

int main(void)
{
	/* every byte value in every component, the others varied too */
	for (unsigned f = 0; f < 4; f++) {
		for (unsigned v = 0; v < 256; v++) {
			for (unsigned c = 0; c < 4; c++) {
				uint8_t b[4] = { 0x00, 0x7f, 0x80, 0xff };
				b[c] = (uint8_t)v;
				check_bytes(f, b, "one");
			}
			uint8_t same[4] = { (uint8_t)v, (uint8_t)v, (uint8_t)v, (uint8_t)v };
			check_bytes(f, same, "all");
			uint8_t mix[4] = { (uint8_t)v, (uint8_t)(255 - v), (uint8_t)(v * 7 + 3), (uint8_t)(v ^ 0x80) };
			check_bytes(f, mix, "mix");
		}
	}

	/* the ends of the range: -128 clamps to -1, -127 is -1 exactly, 127 is 1 */
	CHECK(nv30_snorm8_vp(128.0f) == -1.0f, "-128 -> %a", nv30_snorm8_vp(128.0f));
	CHECK(nv30_snorm8_vp(129.0f) == -1.0f, "-127 -> %a", nv30_snorm8_vp(129.0f));
	CHECK(nv30_snorm8_vp(127.0f) == 1.0f, "127 -> %a", nv30_snorm8_vp(127.0f));
	CHECK(nv30_snorm8_vp(0.0f) == 0.0f && !signbit(nv30_snorm8_vp(0.0f)), "0 -> %a", nv30_snorm8_vp(0.0f));

	/* the key: 3 bits per input, independent */
	uint64_t key = 0;
	for (unsigned i = 0; i < NV30_SNORM8_MAX_ATTRIBS; i++)
		key = nv30_snorm8_key_set(key, i, (i % 5));
	for (unsigned i = 0; i < NV30_SNORM8_MAX_ATTRIBS; i++)
		CHECK(nv30_snorm8_key_nc(key, i) == i % 5, "key attr %u: %u", i, nv30_snorm8_key_nc(key, i));
	CHECK(key >> (3 * NV30_SNORM8_MAX_ATTRIBS) == 0, "key bits above the 16 inputs: %llx", (unsigned long long)key);
	for (unsigned i = 0; i < NV30_SNORM8_MAX_ATTRIBS; i++) {
		for (unsigned nc = 0; nc <= 4; nc++) {
			uint64_t k = nv30_snorm8_key_set(key, i, nc);
			CHECK(nv30_snorm8_key_nc(k, i) == nc, "set attr %u to %u", i, nc);
			for (unsigned j = 0; j < NV30_SNORM8_MAX_ATTRIBS; j++)
				if (j != i)
					CHECK(nv30_snorm8_key_nc(k, j) == j % 5, "set attr %u changed attr %u", i, j);
		}
	}
	/* kept to the declared inputs */
	for (uint32_t inputs = 0; inputs < 0x10000; inputs += 0x111) {
		uint64_t k = nv30_snorm8_key_for_inputs(key, inputs);
		for (unsigned i = 0; i < NV30_SNORM8_MAX_ATTRIBS; i++)
			CHECK(nv30_snorm8_key_nc(k, i) == ((inputs >> i & 1) ? i % 5 : 0), "inputs %x attr %u", inputs, i);
	}
	CHECK(nv30_snorm8_key_for_inputs(key, 0) == 0, "no inputs, key 0");
	CHECK(nv30_snorm8_key_for_inputs(key, 0xffffffffu) == key, "all inputs, the key");

	printf("vtx-snorm8: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
