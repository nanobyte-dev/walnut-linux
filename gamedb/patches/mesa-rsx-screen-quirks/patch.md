+++
component = "mesa"
patches = ["patches/0004-nv30-screen-rsx.patch"]
aliases = ["Mesa 0004"]
category = "port"
switch = "NV30_PS3_VBO_FIFO"
default = "off (the RSX fetches vertex and index buffers from VRAM)"
gating = "is_ps3"
forwarded = "no"
tests = ["ps3:egl-gears"]
batches = []
+++

# nv30: RSX screen quirks (PS3 handles, no Z16, semaphore byte swap, buffer placement)

The screen-level differences between the RSX and a PC NV4x:

- **Object handles.** The 3D, M2MF, surface 2D, swizzled surface and SIFM objects and the fence, query
  and notifier DMA objects are allocated with the handles the hypervisor pre-creates (0x31337000,
  0x31337303, 0x66606660 and so on, [RSX.md](../../../docs/RSX.md#objects-bound-by-ps3fbs-reset-program)).
  Mesa writes the handles it allocated into the stream, so the pushes carry the PS3's with no further
  change, and ps3rsx's validator accepts only those.
- **No Z16.** The RSX mis-renders a 16-bit depth buffer next to a 32-bit colour buffer (random depth
  failures), so `is_format_supported` refuses 2-byte depth formats and the state tracker takes Z24S8.
- **The fence semaphore.** The 3D back end stores a semaphore value with bytes 0 and 2 swapped (probe E4,
  [RSX.md](../../../docs/RSX.md#synchronisation)), so the value is pre-swapped, and the release goes to
  the fence's offset inside the notifier, which covers the whole reports area.
- **Buffer placement.** Vertex and index buffers go to VRAM, since the RSX fetches them itself
  ([mesa-vbo-gpu-fetch](../mesa-vbo-gpu-fetch/patch.md), 0005). `NV30_PS3_VBO_FIFO=1` keeps them in
  GART and pushes vertices inline, the old big-endian path.

The no-Z16 rule was its own patch on 2026-10-14 (commit 7bd5891, "0005-nouveau-nv30-ps3-no-z16-depth" in
the numbering of the time). The softpipe reference of the GL tests uses 24-bit depth as well, because with
Z16 four silhouette pixels of egl-gears resolved their depth tie differently
([DRIVER.md](../../../docs/DRIVER.md#phase-results-2026-10-14)).
