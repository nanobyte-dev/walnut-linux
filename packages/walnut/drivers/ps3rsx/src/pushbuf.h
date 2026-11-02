/* SPDX-License-Identifier: GPL-2.0 */
/*
 * GEM_PUSHBUF's argument checks (submit.c). A push's offset and length come from userspace as
 * 64-bit values; every sum of them is taken with check_add_overflow, so none wraps past the
 * bound it is compared with: the push's BO, the submit's MAX_SUBMIT, the u32 length the ring
 * takes.
 *
 * Pure code without kernel dependencies (submit.c includes linux/overflow.h first, the host
 * test inc/shim.h), so test/pushbuf-test.c builds it on the host.
 */
#ifndef PS3RSX_PUSHBUF_H
#define PS3RSX_PUSHBUF_H

#define MAX_SUBMIT	(512 * 1024 + 4096)	/* bytes of pushes in one submit */

/*
 * One push of a submit: len bytes (NO_PREFETCH masked off) at offset in its BO of bo_size bytes.
 * Both are whole words and the push lies inside the BO; len is added to *total, the submit's
 * bytes so far, which stays at most MAX_SUBMIT. Returns 0, -EINVAL or -E2BIG (*total unchanged).
 */
static inline int ps3rsx_pushbuf_args_check(u64 offset, u64 len, u64 bo_size, u64 *total)
{
	u64 end, sum;

	if ((offset | len) & 3)
		return -EINVAL;
	if (check_add_overflow(offset, len, &end) || end > bo_size)
		return -EINVAL;
	if (check_add_overflow(*total, len, &sum) || sum > MAX_SUBMIT)
		return -E2BIG;
	*total = sum;
	return 0;
}

/* The bytes a submit writes into the ring: the state restore, then its pushes (total) */
static inline int ps3rsx_pushbuf_ring_len(u32 restore, u64 total, u32 *len)
{
	return check_add_overflow(restore, total, len) ? -E2BIG : 0;
}

#endif
