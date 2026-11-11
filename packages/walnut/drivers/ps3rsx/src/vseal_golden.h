/* SPDX-License-Identifier: GPL-2.0 */
/* vseal_golden.h: the answer to VSEAL_OP_GOLDEN (spu/vseal.c golden()): the built-in pattern page (an LCG
 * from seed 7) sealed with key 80..9f, counter VSEAL_GOLDEN_CTR, AD (page 0x1234, slot 77, format 0).
 * Computed with the reference ChaCha20-Poly1305 on the PC; test/vseal-test.c recomputes and compares it.
 * spe.c checks every load and restart of the SPU program against it: a mismatch and the SPE isn't used. */
#ifndef PS3RSX_VSEAL_GOLDEN_H
#define PS3RSX_VSEAL_GOLDEN_H
static const unsigned char vseal_golden_tag[16] = {
	0x43, 0xde, 0x68, 0xe3, 0xd8, 0xda, 0x7c, 0xa1, 0xf3, 0xed, 0x1a, 0x75, 0x60, 0x8c, 0x73, 0xf5
};
#define VSEAL_GOLDEN_FNV 0x4dc6af50u
#endif
