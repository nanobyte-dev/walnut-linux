+++
component = "kernel"
patches = ["patches/local/0032-spufs-setattr-prepare.patch"]
aliases = ["local/0032", "kernel 0032"]
category = "security"
switch = "none"
default = "always on"
authors = ["Junrui Luo"]
forwarded = "not-needed (posted to linuxppc-dev by its author; not merged as of 2026-10-18)"
tests = []
batches = []
+++

# spufs: check permissions in spufs_setattr()

`spufs_setattr()` applied the caller's attributes with `setattr_copy()` without calling
`setattr_prepare()`, where the owner and `CAP_CHOWN` checks live, so the mode and owner of another
user's context files could be changed. The fix calls `setattr_prepare()` first; the `ATTR_SIZE` test
stays ahead of it, so resizing a spufs file still gives -EINVAL. Posted by Junrui Luo (v2, 2026-08-04,
reported by Yuhao Jiang), not merged; taken in the survey of 2026-10-18
([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md#what-was-ported)).
