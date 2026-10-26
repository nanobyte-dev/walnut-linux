+++
id = "byte-normals"
games = ["bananabread", "sauerbraten"]
driver_patches = ["mesa/0033", "mesa/0037", "mesa/0043"]
game_patches = ["cube2/0011"]
sessions = []
batches = ["2026-10-18-mesa-407-lut", "2026-10-18-batch17", "2026-10-19-ssdroot2-gameplay-profiles"]
+++

# The world's normals as signed bytes: a vertex conversion at every draw (2026-10-18)

Moved on 2026-10-26 from docs/HOTPATHS.md (Target 1, "BananaBread's 62.8%") and docs/MEMORY.md
("BananaBread's world normals as shorts"). Found in BananaBread's benchmark demo (map cave); the
vertex layout is the Cube 2 engine's (`engine/world.h`), so Sauerbraten's world has it too. The driver
side, Mesa 0033's translate table and 0043's vertex-program normalisation, is in
[docs/HOTPATHS.md](../../../../docs/HOTPATHS.md#target-1-the-snorm8-vertex-conversion-mesa-0033);
the system-memory copies it causes are Mesa 0037's subject in [docs/MEMORY.md](../../../../docs/MEMORY.md).

## Symptom

The first profiling batch (2026-10-18 early morning, Mesa ps3.299) had BananaBread at 22 fps with
72% of its CPU in Mesa, 44.9% of it in vertex format translation: `util_format_r8g8b8a8_snorm_unpack_rgba_float`
18.2%, `util_format_r8g8b8_snorm_unpack_rgba_float` 16.4%, the translate loop `generic_run` around them.
On the SSD root (batch ssdroot2 P1b, 2026-10-19, Mesa ps3.476 with 0033's table on) it was 40 fps
with Mesa at 62.8% of the game's CPU: vertex format translation 18.3, the query wait 17.4
([water-query-wait](2026-10-18-water-query-wait.md)), per-draw state 13.8, submission 6.9, nv30
validation 3.8, buffer allocation 3.4, the rest 13.

## Evidence

All of `generic_run` (18.1%) is under `renderbatches > glDrawRangeElements`. The world's vertex arrays
carry GL_BYTE normalized normals (R8G8B8_SNORM) and tangents (R8G8B8A8_SNORM). nv30's vertex formats
(nv30_format.c, `nv30_vtxfmt_table`) have no signed 8-bit type (U8_UNORM, U8_USCALED, V16_SNORM,
V16_SSCALED, V16_FLOAT, V32_FLOAT), so u_vbuf turns the draw's vertex range into floats, every draw and
every pass, from static buffers. Mesa 0033's table made that cheaper (44.9% of the CPU at 22 fps on
10-18, 18.1% at 38 fps on 10-19), not free. The CPU reads of those VRAM buffers also give every VRAM
buffer that is read a malloc'ed system memory copy (renderva.cpp's world VBOs need their copies; batch
17 showed nouveau's screen-wide copy hint is never set in BananaBread).

## Cause

R16G16B16_SNORM and R16G16B16A16_SNORM are in nv30's table (V16_SNORM), and the same VBO already feeds
the lightmap coordinates as GL_SHORT (V16_SSCALED), so 16-bit fetch works on the RSX's big-endian
buffers. Only the engine's choice of signed bytes sends the world through u_vbuf.

## Fix

Two routes, both built: the driver's (Mesa 0033 for every game, on by default since ps3.408; Mesa 0043,
`NV30_VTX_SNORM8=1`, which removes the conversion for every game and whose first build wedged the RSX
on this demo: [docs/RSX.md](../../../../docs/RSX.md#errors-and-what-wedges-the-gpu)), and the game's:

**cube2 0011 `vanormalshort`** (off by default). With `vanormalshort 1` the world VBOs are built from
`svertex` (40 bytes: position, normal as 3 shorts + padding, texture and lightmap coordinates, tangent
as 4 shorts; s = b*32767/127 rounded, -128 taken as -127 as GL does). Everything that reads the layout
follows the cvar: `genverts` and `addvbo` (octarender.cpp), `changevbuf` (normal and tangent as
GL_SHORT for the lightmap pass, the stride for every pass), the position-only passes (depth, occlusion,
alpha outlines, sky: `vavertsize()`), and `readva` (`writeobj` converts back, so the .obj is the same).
The glass and waterfall surfaces (material.cpp, through gle with signed byte normals of +-1) send shorts
too, exactly. A change rebuilds the world (`allchanged`). Models are not affected (float normals,
GL_SHORT tangents already; GPU skinning's bone weights and indices are U8, fetchable); grass keeps
GL_BYTE in its GART stream buffer.

On the PC (Debian trixie container, amd64 build of the work tree, llvmpipe forced to GL 2.1 / GLSL
1.20 with the extensions nv30 lacks removed, the benchmark demo at 960x540, SDL's clock advanced 33
ms per frame, 10 screenshots 3 s apart):

| | vanormalshort 0 | 1 |
|---|---|---|
| cave's world vertex buffers (`printvbo 1`) | 4.40 MB (137,532 vertices, 9 VBOs) | 5.50 MB (+1.1 MB of VRAM) |
| pixels against the unpatched build | the same (frames 2-10; frame 1 differs by 48 pixels between two runs of the unpatched build too) | ~170 of 518,400 differ, by at most 2 of 255 |

s/32767 can't equal b/127 except for 0 and +-1 (127 doesn't divide 32767), so the shading moves by up
to 1.5e-5 of a unit normal: invisible, but not the same pixels. That is why the default is 0.

On the PS3:

- Mesa 0033 (`NV30_TRANSLATE_LUT`, gamedb:batch/2026-10-18-mesa-407-lut): 25.9 -> 39.6 steady fps
  (+53%), `low1` 18.2 -> 26.9, x2, on the spectator demo of that night; CPU per run unchanged (231.6 /
  231.1 s). On by default since Mesa ps3.408; not re-measured on the first-person demo used since.
- cube2 0011, batch 17 (gamedb:batch/2026-10-18-batch17, cube2 .442~dev, `NV30_SYSMEM_STATS=300`, x3
  interleaved, one shortnormals run left out for a key press):

| | base (`vanormalshort 0`) | shortnormals |
|---|---|---|
| steady fps | 38.8 | **45.1** (+17%) |
| 1% low | 25.0 | 30.4 |
| gameplay stalls (s) | 1.2 | 1.6 (the same within the spread) |
| peak VRAM (MB) | 117.0 | 121.0 |
| nouveau copies now / made at a read / readbacks | 3 (783 kB) / 5 (1.5 MB) / 5 | 1 (1 kB) / 1 / 1 |
| hint | never set | never set |

The gain is CPU: no u_vbuf conversion or upload per world draw. The memory saved is small (the copies
were 1.5 MB, not the 4.4 MB feared, since the hint never fires), and the VRAM cost is 4 MB on the demo's
map. Package: cube2 since .442~dev (.466 at the time); recipe variants `shortnormals` (`vanormalshort 1`)
and `bytenormals` (`vanormalshort 0`, the default spelled out). The default is still 0; turning it on
is a cube2 change waiting on the webcam check of the world's lighting. Mesa 0043's expected gain on the
same demo (+15 to 20%) is this measurement's; its controlled re-test after the wedge fix is queued in
docs/TESTS.md.
