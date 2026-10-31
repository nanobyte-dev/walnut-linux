+++
component = "kernel"
patches = ["patches/local/0025-spufs-ps-fault-lock-inversion.patch"]
aliases = ["local/0025", "kernel 0025"]
category = "stability"
switch = "none"
default = "always on"
authors = ["Junrui Luo"]
forwarded = "not-needed (posted to linuxppc-dev by its author; not merged as of 2026-10-18)"
tests = []
batches = []
+++

# spufs: the mmap_lock / state_mutex inversion in the fault path

`spufs_ps_fault()` is entered with `mmap_lock` held for read and takes the context's `state_mutex`.
When the context is saved it drops `mmap_lock`, waits for the context to become runnable and takes
`mmap_lock` again, but `spufs_wait()` returns with `state_mutex` re-acquired: the opposite order of
every other spufs fault path. The fix releases the context before taking `mmap_lock` again and goes
to the existing refault path, which returns `VM_FAULT_NOPAGE` as before. Posted by Junrui Luo on
2026-08-12 (reported by Yuhao Jiang), not merged.

## Investigation

- **What crashed:** nothing seen on our PS3. The failure is a deadlock; since `spusched_tick()` takes
  the same `state_mutex`, one wedged context also stalls SPU scheduling for every other context.
- **Why:** three threads of one mm close the cycle: one holds `state_mutex` and waits in
  `mmap_read_lock()`, one holds `mmap_lock` and waits for the context, and a queued
  `mmap_write_lock()` keeps the first read lock from being granted.
- **How it was found:** code reading, by the patch's author; premises checked against our code in the
  survey of 2026-10-18 ([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md#what-was-ported)).
- **Minimal repro:** None yet: a race found by reading code; lockdep on a debug kernel running the
  libspeq tests would be the cheapest way to see the inverted order.

## Validation

Not yet validated on the PS3.
