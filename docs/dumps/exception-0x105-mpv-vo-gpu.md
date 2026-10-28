# Graph exception 0x105 from mpv --vo=gpu: the analysis (2026-10-29)

The incident: `mpv --fs --vo=gpu --hwdec=no` on a 360p H.264 clip wedged the RSX on 2026-10-28 at
22:42 (kernel .874, Mesa ps3.924, ps3rsx 0.924, mpv 0.41.0). The raw dump, dmesg and versions are
in [exception-0x105-mpv-vo-gpu.txt](exception-0x105-mpv-vo-gpu.txt). The work was done on the PC
only; the PS3 check is in [TESTS.md](../TESTS.md) Next.

**Result.** The faulting command is the draw of mpv's first frame. Its one feature nothing had run
on the RSX before is a texture in format 0x14 (nouveau's A16, libgcm's X16): mpv's 64x64 dither
texture, which nv30 makes `L16_UNORM` for `GL_LUMINANCE16`. ps3rsx's validator checked texture
offsets but no texture format, so the push went through. ps3rsx 0.1036 refuses format 0x14 and
refuses a channel after any rejected push; Mesa 0052 (ps3.1037) no longer offers the format on
the PS3, and mpv's dither texture becomes an 8x8 matrix in L8. The second suspect, `gl_FragCoord` in the same draw,
is ruled out or in by step 2b of the morning test.

## Reading the dump

GET is entry 0x138 (312) and PUT 0x139, so one entry was pending and DMA get equals put: the
FIFO had fetched everything, and entry 311 is the last one PGRAPH took. ps3fb prints entries
4i..4i+3 for i = 0, 4, ..., 28 (task #29), which are entries 0-3, 16-19, ..., 112-115. In time
order that is 196 to 312 entries before GET:

| entries | words (subchannel `(a>>13)&7`, method `a & 0x1ffc`) | what it is |
|---|---|---|
| 0-3 | 3D 0x1a08-0x1a14: wrap REPEAT, enable, swizzle 0xaae4, filter NEAREST | texture unit 0 of an earlier submit (another channel's, likely glamor) |
| 16-19 | subchannel 6: bind 0x31337303 (M2MF), 0x180-0x188 notifier, in VRAM, out GART | ps3rsx's own VRAM-to-GART copy for the VRAM swap (`kcopy.c`, `SC_K` 6) |
| 32-35 | M2MF 0x310-0x31c: offset out 0, pitches 0x140, line length 0x140 | the restore of mpv's M2MF state after that copy (the 320-byte chroma plane upload's values) |
| 48-51 | 3D 0x1d88 = 0xc8, 0x1fd8 = 1, 0x1710; SSWZ 0x184 = VRAM | the end of the restore (mpv's last COORD_CONVENTIONS, a 200-pixel-high framebuffer, then the cache invalidates), then the start of mpv's submit |
| 64-67 | SIFM 0x400-0x40c: 64x64, pitch 128 point-sampled, source 0x64d000 (GART), POINT | the dither texture's upload into a swizzled 64x64 surface |
| 80-115 | 3D 0x388, 0x328, 0x32c, 0x348, 0xa68, 0x3b8, 0x3bc, 0x1db4, 0x1484-0x1490 | the first draw's state: depth bounds, stencil, polygon offset, line, polygon stipple |

The submit, captured on the PC (below), lines up with these entries one for one from the SIFM
POINT (entry 67) on: the next 244 methods end with the draw and the fence, and the last of them,
the back-end release 0x1d70, is entry 311. Entry 312, the pending one, is the kernel's
SET_REFERENCE. So PGRAPH took the whole submit, BEGIN_END included, and the exception came from
the draw while it ran (the UB256 case of 0x106 stopped at the BEGIN instead).

## mpv's first frame on nv30

nv30 has no ARB_framebuffer_object and no float textures, so mpv 0.41 runs in "dumb mode": one
draw per frame from the planes to the window. Its submit:

- the three planes are linear L8 textures (NPOT): 640x360 at pitch 640 and two 320x180 at pitch
  320, offsets page-aligned, uploaded by M2MF from GART staging (one copy per plane, 360 or 180
  lines);
- the dither texture: 64x64 `GL_LUMINANCE16`, `L16_UNORM`, TEX_FORMAT 0x00019429 (format byte
  0x94: 0x14, swizzled, normalized), swizzle 0xa9aa, NEAREST, REPEAT on S, T and R, uploaded by
  SIFM as R5G6B5 (2 bytes a texel);
- the fragment program (20 instructions and 11 inline constants, uploaded by M2MF into VRAM):
  four TEX, the colour matrix, and the dither lookup at `gl_FragCoord.xy / 64` (WPOS, input 0;
  COORD_CONVENTIONS 0x1438: origin bottom, height 1080);
- the vertex program: 6 instructions, four float2 attributes at stride 32 from GART, 6 vertices
  (two triangles);
- timer queries around the passes (QUERY_GET type 1 without ZPASS), then the fence.

None of the guesses about alignment hold: no width is odd (180 is a height), every pitch is a
multiple of 64, every offset is page-aligned, and the transfer paths are the ones games use (M2MF
for linear textures, SIFM for swizzled ones).

Against what had run on the RSX before (glamor for days, about 30 games, the mesa-tests), two
things in this draw are new: the X16 format, which gl-texfmt never covered (L8, A8, I8, 4444,
1555, L8A8) and which the state tracker picks only for sized 16-bit internal formats
(`GL_LUMINANCE16`, `GL_ALPHA16`, `GL_INTENSITY16`; `GL_R16` needs ARB_texture_rg, absent), and
`gl_FragCoord.xy` (of the game sources we build, ClassiCube's GL2 shaders read only `.w` and
Luanti's only `.z`, with shadows on). The other 0x105s
were memory accesses (an M2MF copy without a notifier DMA, a texture fetched at an offset only
32-byte aligned), and sampling a texture is one, so the format is the suspect. Rejecting it in the
validator also keeps the whole submit, `gl_FragCoord` included, off the GPU.

## Why the validator let it through

`validate.c` checked the texture *offset* of every unit (128-byte alignment, since 0.249) but not
the format word next to it. It also does fewer data checks than DRIVER.md listed: no bounds
checks at the trigger methods, no fence-slot or query-slice checks; DRIVER.md now says what the
code does.

A rejection alone was not safe either. Mesa sends state once: after mpv's first frame was
refused, its second frame's submit carries only the scissor, the vertex buffer offsets and the
draw (36 words in the capture), so it would have drawn with whatever another program left in the
texture units, programs and vertex formats. ps3rsx 0.1036 refuses a channel once one of its
pushes is rejected (-EIO from then on, as for a channel whose open primitive was closed).

## Reproducing it on the PC

`packages/walnut/drivers/ps3rsx/test/mesa-shim/` runs our ppc64 Mesa under qemu with Mesa's
nouveau drm-shim (`mesa-drm-shim`, which we build anyway) and `wrap.c` in front of it: it answers
like ps3rsx (chipset 0x47, vendor 0x104d, PS3_ABI 2, its BO placement and notifier offsets) and
writes every push to a file. `player.c` runs mpv's renderer through libmpv on a 1920x1080 pbuffer,
which is the GL work of `--vo=gpu` without a window. `decode.py` prints a capture with nv30's
method names; `to-fixture.py` turned the first frame into the validator's test fixture
(`test/fixtures/mpv-vo-gpu-0x105.h`). The offsets in a capture are the shim's, not the PS3's.

    tools=packages/walnut/drivers/ps3rsx/test/mesa-shim
    $tools/run.sh build/tmp/mesa-shim setup build/debs/mesa    # the chroot, mpv, our Mesa
    $tools/run.sh build/tmp/mesa-shim play clip.mp4 3 push.bin
    $tools/decode.py build/tmp/mesa-shim/push.bin | less

With Mesa ps3.1037 mpv finds no 16-bit format and makes its dither texture an 8x8 matrix in L8
(SIFM 8x8, TEX_FORMAT 0x00018129); the frame still reads `gl_FragCoord`, has no X16 anywhere, and
passes the validator (the second fixture, `mpv_frame1_0052`).
