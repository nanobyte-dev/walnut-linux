/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Zeroing new GART buffers (gart.c), on its own so test/zero-test.c can run it in userspace.
 *
 * memset() is a loop of 8-byte stores: on the PPE each 128-byte line it touches is first read
 * from memory (the L2 allocates on a store miss), so clearing a buffer moves twice its size over
 * the bus. clear_page() zeroes a line with dcbz, without reading it: clear_dcbz=1 uses it for the
 * whole pages of a range (GART buffers start and end on pages: offsets are 4 KiB-aligned, sizes
 * PAGE_ALIGN()ed); anything else, and clear_dcbz=0, is memset() as before.
 */
#ifndef PS3RSX_ZERO_H
#define PS3RSX_ZERO_H

static inline void ps3rsx_zero(void *p, size_t n, bool dcbz)
{
	if (dcbz && PAGE_ALIGNED(p) && PAGE_ALIGNED(n)) {
		for (; n; n -= PAGE_SIZE, p += PAGE_SIZE)
			clear_page(p);
		return;
	}
	memset(p, 0, n);
}

#endif
