+++
component = "kernel"
patches = ["patches/local/0061-spu-kernel-reserve-exclusive.patch"]
aliases = ["local/0061", "kernel 0061"]
category = "feature"
switch = "spu_base.enum_shared"
default = "no change with enum_shared=0 (the default boot entry)"
forwarded = "no"
tests = ["kernel-spu-reserve"]
batches = []
+++

# powerpc/cell: a kernel driver's SPE is an exclusive one with enum_shared=1

With `spu_base.enum_shared=1` (ps3/0100) the PS3 SPU setup also creates
lv1's shared SPE, the console's seventh, which lv1 may preempt and save, local store and all, where a
kernel driver can't clear it. Nothing recorded an SPE's resource type once it existed, so
local/0058's `spu_kernel_reserve()` took no SPE at all on such a boot and
the VRAM swap kept nothing in VRAM there.

`struct spu` gets `shared`. `ps3_enumerate_spus` marks the SPE it creates for a resource that isn't
exclusive (a flag set around that one create call), and the boot log names it (`spuN: lv1's shared SPE`).
`spu_kernel_reserve()` skips a shared SPE as it skips one with a context and takes an exclusive one on any
boot. With `enum_shared=1` the kernel holds one exclusive SPE for the VRAM swap and spufs has the other five
and the shared one: six for libspeq instead of five. With `enum_shared=0` no SPE is shared and reserve takes
the same SPE as before. No new hypervisor call.

## Validation

Host test `kernel-spu-reserve`: the block of 0058 with this patch's part applied, on the model of six SPEs
with one marked shared at the head, in the middle and at the tail of the node's list (never reserved and
never touched, five exclusive ones reserved, the sixth refused, the shared SPE left as spufs's), and with
`enum_shared=0` the same SPE taken as before; fix-off `take-shared`. The kernel objects compile with the
release config. On the PS3: the D6 boot ([backlog/spu.md](../../../docs/backlog/spu.md), "The 7th SPE"), not run yet.
