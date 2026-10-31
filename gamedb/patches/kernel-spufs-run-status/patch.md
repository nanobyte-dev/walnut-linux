+++
component = "kernel"
patches = ["patches/local/0028-spufs-spu-run-status-init.patch"]
aliases = ["local/0028", "kernel 0028"]
category = "security"
switch = "none"
default = "always on"
authors = ["Junrui Luo"]
forwarded = "not-needed (posted to linuxppc-dev by its author; not merged as of 2026-10-18)"
tests = []
batches = []
+++

# spufs: don't leak kernel stack through spu_run

`do_spu_run()` passes an uninitialized local `status` to `spufs_run_spu()` and copies it to userspace
unconditionally, but two exits of `spufs_run_spu()` (an interrupted wait for `run_mutex` and a
failed `spu_acquire()`) never write it, so four bytes of kernel stack reached userspace. The fix
initializes it to 0, the "no events pending" value userspace would have seen anyway. Posted by Junrui Luo
(v2, 2026-08-04, reported by Yuhao Jiang), not merged; taken in the survey of 2026-10-18
([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md#what-was-ported)).
