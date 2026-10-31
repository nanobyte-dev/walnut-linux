+++
component = "mesa"
patches = ["patches/0001-nouveau-ps3-detect-pushbufs.patch"]
aliases = ["Mesa 0001"]
category = "port"
switch = "none"
default = "always on"
gating = "is_ps3"
forwarded = "no"
tests = ["ps3:egl-clear", "ps3:egl-gears"]
batches = []
+++

# Detect the PS3's RSX, use its DMA handles, smaller push buffers

This is the patch every other PS3 change hangs on. `nouveau_screen_init` sets `nouveau_screen.is_ps3`
before the channel is allocated: chipset 0x47, the PCI vendor getparam answering Sony's 0x104d, and
ps3rsx's private `PS3_ABI` getparam (0x1000) succeeding. ps3rsx refuses a channel until that getparam
has been asked, so a stock Mesa fails cleanly at init instead of submitting unpatched streams
([DRIVER.md](../../../docs/DRIVER.md#ioctls-abi16c-nvifh)). On the PS3 the VRAM and GART DMA objects
are the hypervisor's (`nv04_data.vram`/`gart` = 0xfeed0000/0xfeed0001).

Push buffers live in the small GART and ps3rsx copies each submission into its own ring, so fewer and
smaller ones do: the screen gets two of 64 KiB (it only carries init and fences), each context two of
256 KiB, where stock nouveau uses four of 512 KiB.

The object handles themselves are substituted in [mesa-rsx-screen-quirks](../mesa-rsx-screen-quirks/patch.md)
(0004). The getparam's value is kept as `nouveau_screen.ps3_abi`, which
[mesa-point-size](../mesa-point-size/patch.md) (0051) reads, and
[mesa-cancel-recover](../mesa-cancel-recover/patch.md) (0053) adds the `PS3_RECOVER` getparam next to it.
