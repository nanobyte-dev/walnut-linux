+++
component = "kernel"
patches = ["patches/local/0026-spufs-cntl-mmap-fault-type-confusion.patch"]
aliases = ["local/0026", "kernel 0026"]
category = "security"
switch = "none"
default = "always on"
authors = ["Zhenhao Wan"]
forwarded = "not-needed (posted to linuxppc-dev by its author; not merged as of 2026-10-18)"
tests = []
batches = []
+++

# spufs: type confusion in the cntl mmap fault handler

`spufs_ps_fault()` took the SPU context from the faulting file's `private_data`. That is right for
most spufs files, but `spufs_cntl_open()` calls `simple_attr_open()`, which overwrites
`private_data` with a `struct simple_attr`, and `cntl` is the one such file with an mmap fault
handler. A fault on that mapping read the attribute as a context and fed a bogus value into
`vmf_insert_pfn()`, reachable by anyone who can open the 0666 `cntl` file. The fix takes the context
from the inode, as `coredump_next_context()` already does; that is the same for every other caller.

Posted by Zhenhao Wan on 2026-08-14 (reported by Yuhao Jiang) and not merged; taken in the survey of
2026-10-18 after checking that `simple_attr_open()` does overwrite `private_data` in our tree
([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md#what-was-ported)).
