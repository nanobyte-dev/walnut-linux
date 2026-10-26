/* minmax-vmx-test: NV30_MINMAX_VMX's index scans (Mesa patch "vbo: index array min/max in
 * AltiVec", src/mesa/vbo/vbo_minmax_vmx.h) against the scalar loops of
 * vbo_get_minmax_index_mapped, copied below: 16- and 32-bit indices, every start alignment, counts
 * 0 to 300, with and without primitive restart (restart index 0, 5, 0xfff0, 0xffff, 0x10000,
 * 0xffffffff), arrays of nothing but restart indices, random and narrow value ranges.
 * On the host GCC's generic vectors run as SSE2; tools/run-tests.sh also builds it for ppc64 with
 * -mcpu=cell in the deb builder's cross compiler and runs it under qemu-ppc64 (AltiVec), when
 * docker and the image are there.
 *   cc -I build/work/mesa/src/mesa/vbo mesa/tests/minmax-vmx-test.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vbo_minmax_vmx.h"

static int fails, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; if (fails < 20) { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* vbo_get_minmax_index_mapped's loops (Mesa 26.2.4) */
static void ref16(const uint16_t *us_indices, unsigned count, bool restart, unsigned restartIndex, unsigned *mn, unsigned *mx)
{
	unsigned max_us = 0, min_us = ~0U;
	if (restart) {
		for (unsigned i = 0; i < count; i++) {
			if (us_indices[i] != restartIndex) {
				if (us_indices[i] > max_us) max_us = us_indices[i];
				if (us_indices[i] < min_us) min_us = us_indices[i];
			}
		}
	} else {
		for (unsigned i = 0; i < count; i++) {
			if (us_indices[i] > max_us) max_us = us_indices[i];
			if (us_indices[i] < min_us) min_us = us_indices[i];
		}
	}
	*mn = min_us; *mx = max_us;
}

static void ref32(const uint32_t *ui_indices, unsigned count, bool restart, unsigned restartIndex, unsigned *mn, unsigned *mx)
{
	unsigned max_ui = 0, min_ui = ~0U;
	for (unsigned i = 0; i < count; i++) {
		if (!restart || ui_indices[i] != restartIndex) {
			if (ui_indices[i] > max_ui) max_ui = ui_indices[i];
			if (ui_indices[i] < min_ui) min_ui = ui_indices[i];
		}
	}
	*mn = min_ui; *mx = max_ui;
}

static unsigned rnd(void)
{
	static uint64_t s = 0x9e3779b97f4a7c15ull;
	s ^= s << 13; s ^= s >> 7; s ^= s << 17;
	return (unsigned)(s >> 16);
}

int main(void)
{
	static uint16_t a16[400] __attribute__((aligned(16)));
	static uint32_t a32[400] __attribute__((aligned(16)));
	const unsigned ridx[] = { 0, 5, 0xfff0, 0xffff, 0x10000, 0xffffffffu };

	for (int round = 0; round < 400; round++) {
		/* value ranges: anything, narrow, the top of the range, with restart indices sprinkled */
		const unsigned kind = round % 5;
		for (unsigned i = 0; i < 400; i++) {
			unsigned v = rnd();
			switch (kind) {
			case 0: break;
			case 1: v = 1000 + v % 37; break;
			case 2: v = 0xffffu - v % 9; break;
			case 3: v = v % 3 ? 0xffffu : v; break;
			case 4: v = v % 2 ? 5 : 0xffffffffu - v % 4; break;
			}
			a16[i] = (uint16_t)v;
			a32[i] = kind == 2 ? 0xffffffffu - v % 9 : v;
		}
		for (unsigned off = 0; off < 8; off++) {
			for (unsigned count = 0; count <= 300; count += (count < 40 ? 1 : 13)) {
				for (unsigned r = 0; r <= sizeof(ridx) / sizeof(ridx[0]); r++) {
					const bool restart = r < sizeof(ridx) / sizeof(ridx[0]);
					const unsigned ri = restart ? ridx[r] : 0;
					unsigned m0, x0, m1, x1;

					ref16(a16 + off, count, restart, ri, &m0, &x0);
					vbo_minmax_u16_vmx(a16 + off, count, restart, ri, &m1, &x1);
					CHECK(m0 == m1 && x0 == x1, "u16 off %u count %u restart %d/%x: %x %x, vmx %x %x",
					      off, count, restart, ri, m0, x0, m1, x1);
					ref32(a32 + off, count, restart, ri, &m0, &x0);
					vbo_minmax_u32_vmx(a32 + off, count, restart, ri, &m1, &x1);
					CHECK(m0 == m1 && x0 == x1, "u32 off %u count %u restart %d/%x: %x %x, vmx %x %x",
					      off, count, restart, ri, m0, x0, m1, x1);
				}
			}
		}
	}

	/* nothing but restart indices */
	for (unsigned i = 0; i < 400; i++) { a16[i] = 0xffff; a32[i] = 0xffffffffu; }
	for (unsigned count = 0; count <= 64; count++) {
		unsigned m, x;
		vbo_minmax_u16_vmx(a16 + 1, count, true, 0xffff, &m, &x);
		CHECK(m == ~0u && x == 0, "u16 all restart, count %u: %x %x", count, m, x);
		vbo_minmax_u32_vmx(a32 + 3, count, true, 0xffffffffu, &m, &x);
		CHECK(m == ~0u && x == 0, "u32 all restart, count %u: %x %x", count, m, x);
		vbo_minmax_u16_vmx(a16, count, false, 0, &m, &x);
		CHECK(count ? m == 0xffff && x == 0xffff : m == ~0u && x == 0, "u16 all 0xffff, count %u: %x %x", count, m, x);
	}

	printf("minmax-vmx: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
