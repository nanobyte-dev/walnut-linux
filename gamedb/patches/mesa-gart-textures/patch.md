+++
component = "mesa"
patches = ["patches/0055-nv30-on-the-PS3-a-texture-that-finds-no-VRAM-goes-to.patch"]
aliases = ["Mesa 0055"]
category = "correctness"
switch = "NV30_GART_TEXTURES"
default = "on"
gating = "is_ps3"
forwarded = "no"
tests = []
batches = []
+++

# On the PS3, a texture that finds no VRAM goes to GART

ps3rsx's CPU-visible VRAM is 215 MB less X's buffers, and by the time a game fills it the VRAM swap has
already given its chunks back. Trigger Rally keeps every level's menu thumbnails, so the textures it
loaded last (its car, sky and buildings) failed to allocate, drew untextured, and the state tracker
retried the allocation at every draw: about 400 failed `GEM_NEW` a second in dmesg.

On the PS3, a miptree that isn't a depth buffer, scanout, shared or display target is now placed in
GART when its VRAM allocation fails, with one `nv30: no VRAM for a N KiB texture, placed in GART` line
on stderr; it costs RAM while it lives. The RSX samples it through TEX_FORMAT's DMA1 and renders into
it, as it already did for the blit's GART destinations ([mesa-texture-uploads-gart](../mesa-texture-uploads-gart/patch.md),
0007). The design point is that the domain is read from the buffer wherever a texture is referenced
(fragment textures, transfer rects, render targets, clears), and the surface context DMAs (COLOR0-3,
ZETA) point at GART only while a render target is there, so the stream is otherwise unchanged.
`NV30_GART_TEXTURES=0` turns it off.

Checked on the PC with the mesa-shim, its heaps limited like ps3rsx's, replaying Trigger Rally's 427
texture loads: no texture without storage, the GART ones sampled through DMA1, mipmap generation
rendering into GART, every push accepted by the validator. Not yet run on the PS3. The game also got a
fix of its own that loads the thumbnails only when shown
([technical notes](../../games/trigger-rally/technical.md)).
