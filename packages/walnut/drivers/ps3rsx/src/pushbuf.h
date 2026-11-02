/* SPDX-License-Identifier: GPL-2.0 */
/*
 * GEM_PUSHBUF's argument checks (submit.c). A push's offset and length come from userspace as
 * 64-bit values and a relocation's offset as a 32-bit one; every sum of them is taken with
 * check_add_overflow or in 64 bits, so none wraps past the bound it is compared with: the
 * push's BO, the submit's MAX_SUBMIT, the u32 length the ring takes, the copied push a
 * relocation patches.
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

/* A relocation's offset in the push BO names a whole word, or the submit is refused */
static inline int ps3rsx_reloc_args_check(u32 reloc_bo_offset)
{
	return reloc_bo_offset & 3 ? -EINVAL : 0;
}

/*
 * Does the relocation at reloc_bo_offset of the push BO fall in the push copied from push_off
 * (len bytes)? If so, *at is the word of the copy it patches. Compared in 64 bits: an offset
 * near 4 GiB doesn't wrap into the push. Both offsets are whole words (a push's is checked by
 * ps3rsx_pushbuf_args_check, a relocation's by ps3rsx_reloc_args_check), or nothing is patched.
 */
static inline bool ps3rsx_reloc_word(u32 reloc_bo_offset, u64 push_off, u64 len, u64 *at)
{
	u64 off = reloc_bo_offset, end;

	if (((off | push_off) & 3) || off < push_off || check_add_overflow(push_off, len, &end) ||
	    off + 4 > end)
		return false;
	*at = (off - push_off) / 4;
	return true;
}

#endif
