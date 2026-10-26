// SPDX-License-Identifier: GPL-2.0
/*
 * ps3rsx_zero() (src/zero.h, clear_dcbz) in userspace: ranges zeroed exactly, the bytes around
 * them untouched, aligned and unaligned, with and without dcbz. On ppc64 (under qemu-ppc64-static)
 * clear_page is the kernel's dcbz loop with the Cell's 128-byte cache block; elsewhere a memset.
 *   zero-test [iterations] [seed]
 */
#include "shim.h"

#define PAGE_SIZE	4096UL
#define PAGE_ALIGNED(x)	(((unsigned long)(x) & (PAGE_SIZE - 1)) == 0)

#if defined(__powerpc64__)
static inline void clear_page(void *addr)
{
	/* arch/powerpc/include/asm/page_64.h's loop, block size 128 (the Cell's L1/L2 line) */
	unsigned long iterations = PAGE_SIZE / 128 / 8;
	unsigned long onex = 128, twox = 256, fourx = 512, eightx = 1024;

	asm volatile(
	"mtctr	%1\n\
1:	dcbz	0,%0\n\
	dcbz	%3,%0\n\
	dcbz	%4,%0\n\
	dcbz	%5,%0\n\
	dcbz	%6,%0\n\
	dcbz	%7,%0\n\
	dcbz	%8,%0\n\
	dcbz	%9,%0\n\
	add	%0,%0,%10\n\
	bdnz+	1b"
	: "=&r" (addr)
	: "r" (iterations), "0" (addr), "b" (onex), "b" (twox),
		"b" (twox+onex), "b" (fourx), "b" (fourx+onex),
		"b" (twox+fourx), "b" (eightx-onex), "r" (eightx)
	: "ctr", "memory");
}
#else
static unsigned long pages_cleared;
static inline void clear_page(void *addr)
{
	pages_cleared++;
	memset(addr, 0, PAGE_SIZE);
}
#endif

#include "../src/zero.h"

static u64 rng_state;
static u32 rnd(void)
{
	u64 x = rng_state;

	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	rng_state = x;
	return (u32)((x * 0x2545f4914f6cdd1dULL) >> 32);
}

int main(int argc, char **argv)
{
	unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 0) : 20000, i, fails = 0;
	size_t size = 64 * PAGE_SIZE;
	u8 *buf;

	rng_state = argc > 2 ? strtoull(argv[2], NULL, 0) : 1;
	if (posix_memalign((void **)&buf, PAGE_SIZE, size))
		return 2;
	for (i = 0; i < iters; i++) {
		bool dcbz = rnd() & 1, aligned = rnd() % 4 != 0;
		size_t off, n, j;

		if (aligned) {
			off = (rnd() % 48) * PAGE_SIZE;
			n = (rnd() % 16) * PAGE_SIZE;
		} else {
			off = rnd() % (48 * PAGE_SIZE);
			n = rnd() % (16 * PAGE_SIZE);
		}
		memset(buf, 0xa5, size);
		ps3rsx_zero(buf + off, n, dcbz);
		for (j = 0; j < size; j++) {
			u8 want = j >= off && j < off + n ? 0 : 0xa5;

			if (buf[j] != want) {
				printf("FAIL: iteration %lu, off %zu n %zu dcbz %d: byte %zu is %#x, not %#x\n",
				       i, off, n, dcbz, j, buf[j], want);
				fails++;
				break;
			}
		}
		if (fails > 10)
			break;
	}
#if !defined(__powerpc64__)
	printf("%lu ranges, %lu pages by clear_page, %lu failures\n", i, pages_cleared, fails);
#else
	printf("%lu ranges (dcbz), %lu failures\n", i, fails);
#endif
	free(buf);
	return fails != 0;
}
