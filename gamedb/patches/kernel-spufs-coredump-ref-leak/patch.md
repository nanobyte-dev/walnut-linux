+++
component = "kernel"
patches = ["patches/local/0031-spufs-coredump-context-leak.patch"]
aliases = ["local/0031", "kernel 0031"]
category = "correctness"
switch = "none"
default = "always on"
authors = ["Junrui Luo"]
forwarded = "not-needed (posted to linuxppc-dev by its author; not merged as of 2026-10-18)"
tests = []
batches = []
+++

# spufs: the spu_context reference leak in coredumps

`coredump_next_context()` returns a context with a reference the caller must drop.
`spufs_coredump_extra_notes_size()` drops it on every exit, but
`spufs_coredump_extra_notes_write()` never did, so every SPU context written into a core dump leaked a
reference, on success too. The fix drops it on each of the loop's three exits. Posted by Junrui Luo (v2,
2026-08-04, reported by Yuhao Jiang), not merged; taken in the survey of 2026-10-18
([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md#what-was-ported)).
