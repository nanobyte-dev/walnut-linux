+++
component = "mesa"
patches = ["patches/0075-nv30-on-the-PS3-a-SIFM-copy-s-source-has-the-room-ps3rsx-counts-for-it.patch"]
aliases = ["Mesa 0075"]
category = "correctness"
switch = "none; NV30_PS3_IDX_STATS=1 also counts the guard's copies"
default = "on on the PS3"
gating = "is_ps3"
forwarded = "no"
tests = ["mesa-sifm-extent:state", "mesa-sifm-extent:fix-off"]
batches = []
+++

# A SIFM copy's source has the room ps3rsx counts for it

SuperTuxKart's first run on a build whose kernel checks draws and copies (session 6, B' with Mesa 1498) lost one submission 15 s
into its load: "rejected push: scaled image source outside the file's buffers". Its draws were lost and Mesa sent its state again.

ps3rsx checks a scaled-image (SIFM) copy's source inside one of the file's buffers. It doesn't know whether the engine clamps its
reads to the declared IMAGE_IN_SIZE, so it counts the larger of that size and the texels the steps sample. nv30 declares
align(w, 2) x align(h, 2), so an odd-height source counts one row more than it has. A texture upload into a swizzled (power-of-two)
texture is a SIFM copy of the staging buffer, which held exactly the box's rows rounded up to its pool class (a power of two, at
least 4 KiB) or to a page. When that left less than a row the copy was refused: a 33x21 RGBA box is 4032 bytes in a 4 KiB class,
and the kernel counts 4168. About 1.3% of the odd-height boxes up to 256 KiB do that. The corpus has no refused one, but quakespasm's
256x11 uploads end exactly at their buffer's end and etr's 26x31 has 24 bytes to spare.

On the PS3 nv30 now:

- gives an odd-height upload's staging buffer one row more, after the layout (layer_stride and the offsets stay);
- gives a linear texture one row more at its buffer's end, for its last level copied by SIFM (`resource_copy_region` into a
  swizzled texture, a resolve);
- checks the kernel's count (`nv30_sifm_extent.h`, the kernel's formula on the words nv30 sends) before choosing SIFM, and leaves
  a copy that wouldn't fit to the 3D blit, or to the CPU for a one-byte swizzled destination (mappable on the PS3: textures are
  made in the CPU-visible VRAM or in GART). The first such copy prints a line, and with `NV30_PS3_IDX_STATS=1` every power of two
  does. The padding makes it unreachable for uploads and textures; it catches a source nothing padded.

The kernel's rule stays as it is: the client now has the bytes it declares. `mesa-sifm-extent:state` runs nv30_miptree.c and
nv30_transfer.c against a fake device and checks every SIFM copy's words against a model of the kernel's rule: the pool-class
edges, SuperTuxKart's 33x21, quakespasm's and etr's sizes and a sweep, with the staging pool and without; `nv30_sifm_src_bytes`
equal to the kernel's count on random rects; a short source taken by the 3D blit; a page-exact linear level copied into a swizzled
texture; and the copy session 6's SuperTuxKart push dump has the kernel refuse (chan1-s2.push, submit 28: a 35x21 glyph at
(219, 154) of a 256x256 swizzled page, SIZE 36x22, pitch 192, 4176 bytes counted in a 4 KiB staging buffer): its recorded words are
refused by the model, and the same upload here sends the same words from a buffer that holds them. `fix-off` takes out the staging
row (at 33x21 and at the dump's copy), the guard and the texture row, each of which must fail.

Stable users have it too: ps3rsx 0.1403's slice a/b sifm rule refuses the same copy. The SIFM and staging code is the same in
Mesa 1219 (walnut-stable); 0075 applies there except its nv30_screen.h hunk, and 1219 has no `idx_stats`, so a backport counts
the guard's copies without the power-of-two lines.
