+++
component = "mesa"
patches = ["patches/0014-nv30-blit-source-alignment.patch"]
aliases = ["Mesa 0014"]
category = "stability"
switch = "none"
default = "always on"
gating = "is_ps3"
forwarded = "no"
tests = ["ps3:gl-mipread"]
batches = ["2026-10-16-mesa-223-neverball-wedge", "2026-10-16-mesa-225"]
+++

# nv30: no 3D blit from a texture offset the RSX can't sample

Reading a level of a swizzled texture back (a transfer map with `PIPE_MAP_READ`) goes through
`nv30_transfer_rect_blit`, which samples the level as a texture based at the level's offset. The blit
checked only the destination's alignment. On the PS3 it now refuses a source offset that isn't 128-byte
aligned (libgcm's texture alignment), and such small levels are read with the CPU, as their writes
already were. The check is one line; ps3rsx's validator refuses the same offsets since version 0.249
([ps3rsx-validator-tex-offset](../ps3rsx-validator-tex-offset/patch.md)), so a stream that slipped
through would now be cancelled instead of wedging the GPU.

## Investigation

- **What crashed:** [Neverball](../../games/neverball/)'s first run on Mesa ps3.223 wedged the RSX:
  `nouveau: kernel rejected pushbuf`, then `ps3rsx: graph exception` with cause 0x105
  (gamedb:batch/2026-10-16-mesa-223-neverball-wedge). Only a console reboot recovered it.
- **Why:** Mesa generates the mipmaps of formats nv30 can't render to (L8A8, L8, A8, I8, 4444, 5551) on
  the CPU, mapping level n-1 for reading. In a 32x32 L8A8 texture the 2x2 level sits at +0xaa0, so the
  read-back blit set `TEX_OFFSET` 0x0142baa0, and the RSX raises 0x105 for a texture offset that is not
  128-byte aligned ([RSX.md](../../../docs/RSX.md#errors-and-what-wedges-the-gpu)). Counting the PFIFO
  cache entries in the exception dump put the fault in that blit; the push before it, level 3 at +0xa80,
  ran.
- **How it was found:** Neverball's `GL_GENERATE_MIPMAP` on L8A8 textures
  ([gamedb:neverball/driver-bugs](../../games/neverball/investigations/2026-10-16-driver-bugs.md)).
- **Minimal repro:** `gl-mipread` in `packages/walnut/tools/ps3-gpu-tools/mesa-tests` reads back the
  small levels of swizzled textures (32x32 L8A8 first). Without this patch it wedges the RSX, so it runs
  only on a Mesa that has it; on ps3.225 it passed all 70 cases (gamedb:batch/2026-10-16-mesa-225).

## Validation

Not yet validated on the PS3.
