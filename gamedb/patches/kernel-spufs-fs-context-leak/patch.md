+++
component = "kernel"
patches = ["patches/local/0030-spufs-free-fs-context.patch"]
aliases = ["local/0030", "kernel 0030"]
category = "correctness"
switch = "none"
default = "always on"
authors = ["Junrui Luo"]
forwarded = "not-needed (posted to linuxppc-dev by its author; not merged as of 2026-10-18)"
tests = []
batches = []
+++

# spufs: free the spufs_fs_context in spufs_free_fc()

`spufs_init_fs_context()` allocates two objects, a `spufs_fs_context` in `fc->fs_private` and a
`spufs_sb_info` in `fc->s_fs_info`, and the `->free` callback released only the second. Every spufs
mount attempt leaked one allocation, and `fsopen()` reaches it without privileges. The fix frees
`fs_private` too. Posted by Junrui Luo on 2026-08-12 (reported by Yuhao Jiang), not merged; taken in the
survey of 2026-10-18 ([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md#what-was-ported)).
