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

Validated on 2026-11-01 (PS3 session M, the merged test kernel 6.12.111+ps3.1210~dev20261031171117, ps3rsx 0.1209, ps3-gpu-tools 0.1161, the Mesa ladder of key c12abbd5c30c45d4): `gl-mipread` at rung 1 (the fix off) and rung 2 (on), the last
runs of the session. Off: the first unaligned read-back, the 2x2 level of a 32x32 RGBA8 texture at
+0x1540, became a push that ps3rsx's validator rejected. The channel was refused, so all 70 cases
failed, but there was no graph exception and the GPU still answered (`rsxdrm-smoke nop`). On: all 70
cases passed with no kernel lines.

```
off: ps3rsx: rejected push: texture offset not 128-byte aligned at word 66 (0020fa00)
off: ps3rsx: channel 1 refused from now on (pid 5548, gl-mipread)
on:  get I8 16x256: ok (9 levels, 0 wrong) ... PASS
```
