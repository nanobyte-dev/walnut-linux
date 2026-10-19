// SPDX-License-Identifier: GPL-2.0
/*
 * The validator's vertex format checks (validate.c's vtxfmt_ok(), fast path and check_method()):
 * every type and size in every VTXFMT slot, as one write, an incrementing burst and a
 * non-incrementing one, against the rule spelled out here independently: type 0 is refused
 * (the RSX lacks B8G8R8A8_UNORM), type 7 (UB256) only with 4 components or disabled (size 0;
 * 3 components raised graph exception 0x106 with Mesa 0043, docs/RSX.md), everything else
 * passes. Plus the words Mesa sends: its float, short and byte formats, 0043's UB256 x4.
 * Built in userspace with inc/ (kernel stand-ins) by the Makefile here.
 */
#include "shim.h"
#define PS3RSX_H	/* the driver header: not in userspace */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define pr_warn(...) do { if (verbose) printf(__VA_ARGS__); } while (0)
static int verbose;

struct ps3rsx_vstate {
	u32 bound[8];
	u8 cls[8];
	bool prim_open;
};
bool ps3rsx_known_object(u32 handle);
int ps3rsx_validate(struct ps3rsx_vstate *vs, const u32 *w, u32 nwords);
void ps3rsx_validate_init(void);

#ifndef VALIDATE_C
#define VALIDATE_C "../src/validate.c"
#endif
#include VALIDATE_C

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static bool expect_ok(u32 data)
{
	unsigned type = data & 0xf, size = (data >> 4) & 0xf;

	if (type == 0)
		return false;
	if (type == 7)
		return size == 0 || size == 4;
	return true;
}

#define HDR(n, m) (((u32)(n) << 18) | (7u << 13) | (m))
#define NONINC 0x40000000u

/* bind the 3D object on subchannel 7, then the words */
static int push(const u32 *words, u32 n)
{
	struct ps3rsx_vstate vs;
	u32 w[64];

	memset(&vs, 0, sizeof(vs));
	w[0] = HDR(1, 0);
	w[1] = 0x31337000;
	memcpy(&w[2], words, n * 4);
	return ps3rsx_validate(&vs, w, n + 2);
}

int main(int argc, char **argv)
{
	verbose = argc > 1;
	ps3rsx_validate_init();

	for (u32 slot = 0; slot < 16; slot++) {
		const u32 m = 0x1740 + 4 * slot;

		for (u32 type = 0; type < 16; type++)
			for (u32 size = 0; size < 16; size++)
				for (u32 hi = 0; hi < 3; hi++) {
					const u32 d = type | size << 4 |
						      (hi == 0 ? 0 : hi == 1 ? 0x2000 : 0x12345600);
					const bool ok = expect_ok(d);
					u32 one[2] = { HDR(1, m), d };
					u32 ni[4] = { NONINC | HDR(3, m), 0x2022, d, 0x2022 };
					u32 inc[4] = { HDR(3, 0x1740), 0x2022, 0x2022, 0x2022 };
					int r;

					r = push(one, 2);
					CHECK((r == 0) == ok, "slot %u data %08x: %d, expected %s", slot, d, r,
					      ok ? "accept" : "refuse");
					r = push(ni, 4);
					CHECK((r == 0) == ok, "non-incrementing slot %u data %08x: %d", slot, d, r);
					if (slot < 14) {
						inc[0] = HDR(3, m);
						inc[2] = d;	/* the middle word: slot + 1 */
						r = push(inc, 4);
						CHECK((r == 0) == ok, "burst at slot %u data %08x: %d",
						      slot + 1, d, r);
					}
				}
	}

	/* what Mesa sends: Sauerbraten's world vertex (stride 32) with 0043 fixed, and as it was */
	const u32 mesa_ok[] = { 0x2032, 0x2022, 0x2025, 0x2047, 0x2047, 0x0002, 0x2044, 0x2034, 0x1047 };
	const u32 mesa_bad[] = { 0x2037, 0x2027, 0x2017, 0x2000 };
	u32 w[1 + 16];

	w[0] = HDR(9, 0x1740);
	memcpy(&w[1], mesa_ok, sizeof(mesa_ok));
	CHECK(push(w, 10) == 0, "Mesa's formats with 0043 fixed refused");
	for (unsigned i = 0; i < ARRAY_SIZE(mesa_bad); i++) {
		memcpy(&w[1], mesa_ok, sizeof(mesa_ok));
		w[1 + 3] = mesa_bad[i];
		CHECK(push(w, 10) != 0, "%08x in a burst accepted", mesa_bad[i]);
	}

	printf("validate-test: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
