+++
component = "kernel"
patches = ["patches/local/0002-ps3fb-fix-gpu-mmap-offsets.patch"]
aliases = ["local/0002", "kernel 0002"]
category = "correctness"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++

# ps3fb: the /dev/ps3gpu_fifo and _ctrl mmap offsets

The OtherOS++ ps3fb patch ([kernel-ps3-ps3fb-gpu-devices](../kernel-ps3-ps3fb-gpu-devices/patch.md),
ps3/0030) exposes the GPU's FIFO area and control registers to userspace through `/dev/ps3gpu_*`. Two of
its mmap paths were wrong: the FIFO mapping passed a kernel virtual address where a physical address is
expected, so userspace got unrelated memory, and the control mapping subtracted the page offset instead
of adding it. Both are fixed here.

These devices are how the first RSX experiments ran from userspace (`rsx-probe`, the triangle and
textured quad in [RSX.md](../../../docs/RSX.md)) before ps3rsx existed.
