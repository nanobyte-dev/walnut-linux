+++
component = "kernel"
patches = ["patches/local/0058-spu-kernel-reserve.patch"]
aliases = ["local/0058", "kernel 0058"]
category = "feature"
switch = "none"
default = "always built in; unused until a kernel driver calls spu_kernel_reserve()"
forwarded = "no"
tests = ["kernel-spu-reserve"]
batches = []
+++

# powerpc/cell: let a kernel driver reserve an SPE

A kernel driver that runs its own SPU program needs an SPE that spufs never schedules, preempts or
touches. `spu_base.c` gets four calls, exported GPL like the rest of the SPU API:

- `spu_kernel_reserve(ops)` takes an SPE that is `SPU_FREE` with no context off its node's list (the only
  list spufs schedules, preempts and notifies from) and keeps it `SPU_USED` (all the affinity walk checks).
  The entry goes with `list_del_rcu()`, which keeps `->next`: spusched_thread drops the list's mutex with
  its cursor parked on an SPE and goes on from there. It masks every interrupt class and resets the SPE
  from nothing: stopped, its MFC queue suspended, purged and empty, SR1 written as 0x33, TCLASS as spufs
  sets it, the SLBs invalidated, LSLR, privileged control and the channels reset, the local store cleared,
  the outbound mailboxes drained and every class's status cleared. After a 50 ms grace period the driver's
  four callbacks go in; it gets the SPE with every class still masked.
- `spu_kernel_reset(spu)`, the same reset for the driver's restart: no allocation, no firmware.
- `spu_kernel_ls_decode(spu, on)`: SR1 0x32 or 0x33, so the local store isn't reachable through an
  effective address while the program runs.
- `spu_kernel_release(spu)` gives an SPE back only when it is seen stopped with its queue suspended,
  purged and empty, after the same grace period. One that isn't stays off spufs's list until reboot, with
  its SLBs invalidated, one more suspend and purge asked, and the driver's module (`ops->owner`) pinned
  for good: its DMA may still reach module memory.

After a release, or when an SPE is given up, the callbacks point at stubs inside `spu_base.c`, never at
NULL and never at the driver: lv1 can deliver an SPE interrupt after the SPU was seen stopped, and the
class 0 handler calls its callback even with nothing pending. A reboot notifier stops every reserved or
given-up SPE and clears its local store. `/sys/devices/system/spu/spuN/reserved` reads 1 for an SPE spufs doesn't
have, so libspeq counts only the ones it can use.

Only the priv1 operations spufs already uses (interrupt masks and status, SR1, TCLASS) and the
problem-state and priv2 registers: no new hypervisor call. `CONFIG_SPUISO_FS` and `CONFIG_SPULDR_FS` stay
off.

## Validation

Host test `kernel-spu-reserve` (`packages/patched/kernel/linux/tests/spu-kernel-reserve.sh`): the code as
the patch adds it, on a model of six SPEs, PS3's priv1 operations (SR1's allowed bits), spu_base's
interrupt handlers (late interrupts included), status and mailboxes that latch while masked, and spufs's
list walks (spusched_thread's parked cursor, the affinity path); each quiesce step stuck in turn; 34
fix-offs. Not yet run on the PS3.
