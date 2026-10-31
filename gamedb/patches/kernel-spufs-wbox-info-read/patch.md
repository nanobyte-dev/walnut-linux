+++
component = "kernel"
patches = ["patches/local/0029-spufs-wbox-info-out-of-bounds-read.patch"]
aliases = ["local/0029", "kernel 0029"]
category = "security"
switch = "none"
default = "always on"
authors = ["Junrui Luo"]
forwarded = "not-needed (posted to linuxppc-dev by its author; not merged as of 2026-10-18)"
tests = []
batches = []
+++

# spufs: out-of-bounds read in spufs_wbox_info_read()

`spufs_wbox_info_cnt()` already returns a byte count (0 to 16), and `spufs_wbox_info_read()` multiplied
it by `sizeof(u32)` a second time, so with two or more mailbox entries queued a read could copy up to
48 bytes of kernel stack past the 16-byte buffer to userspace. The mailbox's occupancy is under the
context owner's control (writes to `wbox`) and `wbox_info` is readable. The fix passes the count
unscaled, as `spufs_wbox_info_dump()` does; the double scaling dates from 88413a6bfbbe. Posted by
Junrui Luo on 2026-08-12 (reported by Yuhao Jiang), not merged; taken in the survey of 2026-10-18 after
checking that the function returns bytes in our tree
([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md#what-was-ported)).
