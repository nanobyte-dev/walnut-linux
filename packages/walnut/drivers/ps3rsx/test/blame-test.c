// SPDX-License-Identifier: GPL-2.0
/* Host test of src/blame.h (the recovery's choice of the trapping commit): make, then $(O)/blame-test */
#include <stdio.h>
#include <string.h>
#include "blame.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
	struct ps3rsx_blame b;
	const struct ps3rsx_blame_ent *e;
	unsigned int s;

	/* 2026-11-01 W1b: X, glxgears, the smoke's trapping push (ref one short of it), then mpv */
	memset(&b, 0, sizeof(b));
	ps3rsx_blame_add(&b, 0x5e008bec, 0, (unsigned long long)(0) + 100, 3007, "Xorg");
	ps3rsx_blame_add(&b, 0x5e008bed, 1, (unsigned long long)(1) + 100, 3308, "glxgears");
	ps3rsx_blame_add(&b, 0x5e008bee, 4, (unsigned long long)(4) + 100, 3464, "rsxdrm-smoke");
	ps3rsx_blame_add(&b, 0x5e008bef, 5, (unsigned long long)(5) + 100, 3464, "rsxdrm-smoke");
	ps3rsx_blame_add(&b, 0x5e008bf0, 3, (unsigned long long)(3) + 100, 3332, "vo");
	e = ps3rsx_blame_find(&b, 0x5e008bee);
	CHECK(e && e->chan == 5 && !strcmp(e->comm, "rsxdrm-smoke"), "W1b: got %d %s", e ? e->chan : -9, e ? e->comm : "");
	/* all passed: nobody */
	CHECK(!ps3rsx_blame_find(&b, 0x5e008bf0), "all passed");
	/* a kernel commit first: chan -1 (nobody refused) */
	memset(&b, 0, sizeof(b));
	ps3rsx_blame_add(&b, 10, -1, 0, 77, "kworker/0:1");
	ps3rsx_blame_add(&b, 11, 2, (unsigned long long)(2) + 100, 88, "neverball");
	e = ps3rsx_blame_find(&b, 9);
	CHECK(e && e->chan == -1, "kernel commit");
	/* wrap of the sequence numbers */
	memset(&b, 0, sizeof(b));
	ps3rsx_blame_add(&b, 0xfffffffe, 1, (unsigned long long)(1) + 100, 1, "a");
	ps3rsx_blame_add(&b, 0xffffffff, 2, (unsigned long long)(2) + 100, 2, "b");
	ps3rsx_blame_add(&b, 0x00000000, 3, (unsigned long long)(3) + 100, 3, "c");
	e = ps3rsx_blame_find(&b, 0xffffffff);
	CHECK(e && e->chan == 3, "wrap: got %d", e ? e->chan : -9);
	/* ring overflow: 100 commits, ref in the middle of the kept 64 */
	memset(&b, 0, sizeof(b));
	for (s = 1; s <= 100; s++)
		ps3rsx_blame_add(&b, s, (int)s, (unsigned long long)((int)s) + 100, (int)s, "x");
	e = ps3rsx_blame_find(&b, 70);
	CHECK(e && e->seq == 71, "overflow: got %u", e ? e->seq : 0);
	/* the oldest kept one unpassed: the trap may be older, unknown */
	CHECK(!ps3rsx_blame_find(&b, 10), "dropped out: must be unknown");
	/* the oldest kept one is the first unpassed but nothing was dropped: it is the one */
	memset(&b, 0, sizeof(b));
	ps3rsx_blame_add(&b, 5, 7, (unsigned long long)(7) + 100, 7, "first");
	e = ps3rsx_blame_find(&b, 4);
	CHECK(e && e->chan == 7, "no overflow, first entry");
	/* R1: the faulting client's channel id 5 is reused by another client: the serial tells */
	memset(&b, 0, sizeof(b));
	ps3rsx_blame_add(&b, 20, 5, 1001, 50, "rsxdrm-smoke");
	ps3rsx_blame_add(&b, 21, 5, 1002, 60, "glxgears");
	e = ps3rsx_blame_find(&b, 19);
	CHECK(e && e->serial == 1001, "serial: got %llu", e ? e->serial : 0ULL);
	/* long comm cut to 15 characters */
	memset(&b, 0, sizeof(b));
	ps3rsx_blame_add(&b, 1, 1, (unsigned long long)(1) + 100, 1, "a-very-long-command-name");
	CHECK(strlen(b.e[0].comm) == 15, "comm length %zu", strlen(b.e[0].comm));
	printf(fails ? "blame-test: %d failure(s)\n" : "blame-test: all passed\n", fails);
	return fails != 0;
}
