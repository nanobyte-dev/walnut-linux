+++
id = "opengl2-freezes"
games = ["openarena"]
driver_patches = ["mesa/0037"]
game_patches = ["ioquake3/0027", "ioquake3/0028", "ioquake3/0029"]
sessions = []
batches = ["2026-10-18-batch8", "2026-10-18-batch16", "2026-10-18-batch17"]
+++

# OpenArena opengl2 (gl2min): the 16 s freezes and the memory (2026-10-18)

Moved on 2026-10-26 from docs/MEMORY.md ("OpenArena opengl2 (gl2min): the 16 s freezes and the
memory"). The patches are our ioquake3's (renderer_opengl2); the driver side, nouveau's system-memory
copies of VRAM buffers and Mesa 0037's switches that limit them, stays in
[docs/MEMORY.md](../../../../docs/MEMORY.md#nouveaus-system-memory-copies-of-vram-buffers-mesa-0037).
Both causes are settled: since batch 17 gl2min runs at 57-60 steady fps with 1-2 s of gameplay stalls.

## Symptom

The profiled gl2min run of 2026-10-18 (`PERF=1 MEM=1`, ioquake3 ps3.396, Mesa ps3.408~dev,
`build/bench/ps3-bench-20261018T080250Z-openarena-gl2min`, in gamedb:batch/2026-10-18-batch8) had 54.7 s of gameplay stalls, a 16148 ms
longest frame, 66.5 MB swapped and 117 MB of VRAM. Two causes, one in our report and one in memory.

## The 16 s freeze was the load's last frame, counted as play

Each demo pass reloads the map. report.py cut the reload at 1 s after `CL_InitCGame:`, but in opengl2
the load ends in one long frame with no SwapBuffers in it: the cgame load, `cl_preloadLate` (35 models,
skins and shaders, 7-12 s in opengl2 with the PPE swapping), then `RB_ShowImages` (0.1-1.5 s, every
texture drawn once) and the first demo frame. That frame starts inside the reload and ends 4-5 s after
the cut:

| Pass 3 of the profiled run (s from start) | |
|---|---|
| 131.5 | RE_Shutdown, R_Init: 55 GLSL programs kept, 0.27 s |
| 137.1 | world loaded |
| 143.06 - 159.20 | **the 16148 ms frame** |
| 154.0 | `cl_preloadLate: 35 models, skins and shaders ... in 10315 msec`, `CL_InitCGame: 19.13 seconds` |
| 155.06 | report.py's old cut |
| 156.5 | `1533 msec to draw all images` |

Of the 54.7 s, 38.8 s were such frames (16148, 12985 and 9640 ms). report.py now gives a frame that
starts inside a reload segment to the reload (passsplit.py already went by the frame's start; commit
3634dcc, docs/STALLS.md "Gameplay-only columns"). The profiled run became 16.0 s of gameplay stalls
with a 6263 ms longest frame; a second run (`20261018T114707Z`) 9.7 s and 2125 ms. OpenArena opengl1
runs move by 0-4 s (20261018T001126Z: 19.2 -> 15.1 s).

## The play stalls that were left: memory

The remaining stall frames (1.0-6.3 s) were the first seconds after each load, plus single 0.5-1 s
frames when a bot joins. The samples during them show a machine out of memory, not a compile:
MemAvailable 13-60 MB, swap-ins of 25-260 pages and VRAM swap write-backs of 256-1024 pages every
0.5 s, swap-out bursts of 1.2-1.7k pages, the game's VRAM going 70 -> 114 MB while GART staging peaks
at 48 MB during the loads. The game held Rss 79-104 MB plus 58-68 MB swapped (opengl1: 71 + 20).

| MEM=1 snapshots (start / mid / end) | opengl2 gl2min | opengl1 |
|---|---|---|
| malloc in use (all arenas) | 56 MB (in a load) / **95** / 94 | 21-34 |
| heap Rss + swap | 51.3 + 43.7 | 22.8 + 13.9 |
| VRAM (ps3rsx, the game) | 100 / 111 / 66 MB, 117 peak | 36 |
| VRAM swap | 88 MB of it in VRAM, the rest written back to the USB disk all through play | |

Not the causes: GLSL compiles (the 55 programs are built once at the first start in 3.0-3.4 s, kept
across every renderer restart by 0004, 0.02-1.3 s to take them back, none built during play:
`glsl*_deferred=0`), and FBOs (gl2min sets `r_ext_framebuffer_object 0`). Mesa's disk shader cache
caches the GLSL front end's NIR for nv30; it shortens only the first start (batch 17 measured no load
gain: docs/PROFILING.md "The shader caches").

## Cause: why opengl2's malloc held 60 MB more

renderergl2 sent vertex colours as `GL_UNSIGNED_SHORT` normalized (R16G16B16A16_UNORM), a format nv30
can't fetch (`nv30_vtxfmt_table`). Every draw from the tess VAO (the world, effects, the HUD; everything
but MD3 models, since `r_vaoCache` is 0) went through u_vbuf, which converts the colours to floats on
the CPU and maps the vertex and index buffers for reading (perf: 0.9% of the samples in the
conversion). A read map of a VRAM buffer makes nouveau keep a malloc'ed copy of the whole buffer, and
after four flushes in a row with a readback nouveau sets a hint that gives every new VRAM buffer a copy.
`RB_UpdateTessVao` orphans the tess buffers with `glBufferData(NULL)` before each tess flush; nv30 has
no buffer invalidation, so each orphan is a new pipe buffer, written dirty and read back for the
conversion: a readback and a drain at every tess flush, which set the hint in the first frames. Then
the MD3 models load (STATIC, VRAM): the animation VBOs got copies too, 62.2 MB for the demo's models
(223 VBOs, counted on the host). That is about the 60 MB.

Where the load CPU went (perf, a window with three reloads): `R_MipMapsRGB`'s `powf` per texel channel
7.8% of all samples (picmip and every mip level are made on the CPU at each load and for every player a
joining bot brings), `COM_ParseExt` 2.8% (shader scripts), `R_RegisterMD3` 2.4%, libdeflate 2.7%.
opengl2 has no `r_keepImages` (0025 is opengl1's), so every pass reloads every texture.

## Fix: ioquake3 ps3.462, patches 0027-0029

| Patch | Switch (default) | What | Host check (llvmpipe, GL 2.1, the bench demo, 30 frames) |
|---|---|---|---|
| 0027 | `r_vaoSnormColor 1` (latched) | colours as GL_SHORT normalized (v >> 1, 0 -> -1 so a zero alpha still fails an alpha test under either GL rule), which nv30 fetches (V16_SNORM, as the normals): no u_vbuf, no buffer reads, no copies, no hint | 17 pixels differ by 1 level |
| 0028 | `r_mipMapPowf 0` | the gamma-correct mipmap bytes from 255 thresholds found once by bisection with the same powf, plus a 4096-bin start table: the same bytes without powf | identical; every float in [0,1] equal on x86-64 glibc, every 16th + all within 200000 ulps of each threshold on ppc64 glibc (qemu) |
| 0029 | `r_md3PackedFrames 0` (opt-in, latched, needs `r_normalMapping 0`) | animated MD3 frames as MD3's own 16-bit positions + 4 unsigned-byte normals, 12 bytes a vertex instead of 20 (V16_SSCALED, U8_UNORM: both native on nv30), decoded in the 4 vertex-animation programs by `u_PackedFrames` (lightall draws single-frame models with that variant too, and they keep floats): the demo's model VBOs 62.2 -> 37.5 MB of VRAM | 1734 pixels differ, by at most 2 levels |

Recipe variants (`gamedb/games/openarena/benchmark/recipe.sh`): `gl2min_color16` (0027 off),
`gl2min_mippowf` (0028 off), `gl2min_md3pack` (0029 on); `gl2min_up` turns 0027-0028 off too.

Measured on the PS3, batch 17 (2026-10-18 17:38-20:40 UTC, kernel .433, ioquake3 .462, Mesa .442~dev,
`STALLS=1`, results commit 3939a48), each variant x2 or x3, interleaved:

<!-- gamedb:batch-table 2026-10-18-batch17 games=openarena variants=gl2min@rec10,gl2min_color16@rec10,gl2min_md3pack@rec10,gl2min_mippowf@rec10 metrics=fps_steady,stall_play_s,stall_swap_s,low1_play,vram,ram_free root=../../.. -->
| Game | Variant | `fps_steady` (fps) | `stall_play_s` (s) | `stall_swap_s` (s) | `low1_play` (fps) | `vram` (MiB) | `ram_free` (MiB) |
|---|---|---|---|---|---|---|---|
| [OpenArena](../../../games/openarena/) | gl2min@rec10 | 59.5 | 2.4 | 0.9 | 21.9 | 117.1 | 15.2 |
| [OpenArena](../../../games/openarena/) | gl2min_color16@rec10 | 52.4 | 12.9 | 20.1 | 10.8 | 116.6 | 10.1 |
| [OpenArena](../../../games/openarena/) | gl2min_md3pack@rec10 | 59 | 1.3 | 1 | 23.5 | 97.7 | 16.6 |
| [OpenArena](../../../games/openarena/) | gl2min_mippowf@rec10 | 60 | 0.8 | 0.8 | 23.6 | 117.2 | 11.7 |

Numbers: gamedb:batch/2026-10-18-batch17 (batch 17: ETR uncapped, start-up profiles x5, shader cache cold/warm, OpenArena gl2 memory variants, Mesa sysmem switches, Sauerbraten shortnormals, 2026-10-18; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

Peak Rss (VmHWM) was 86-89 MB in gl2min and md3pack, 110-112 MB with 0027 off, 87-88 MB with 0028 off;
the map load 10.1-10.3 s, 9.9-10.7 s, 10.2-10.8 s and 11.7-11.9 s. Batch 16, the same boot on ioquake3
.396: gl2min 51.8 steady, 11.0 s of stalls, 20.1 s of swap stalls.

- **0027 is the win**: 7 fps more, stalls 12.9 -> 2.4 s, swap stalls 20 -> 0.9 s, 21-26 MB lower
  peak Rss and 5 MB more MemAvailable. It is the default.
- **0029** frees 19.4 MB of VRAM (expected 24.7: the model VBOs) and leaves the stalls the same within
  the run spread. It stays opt-in (`r_md3PackedFrames 0`): the batch recorded no check of the players on
  the webcam, which a default change needs (a wrong vertex fetch loses the players, not the world).
- **0028** takes 1.5 s (13%) off each map load. The 0028-off arm had the lowest stalls (0.7-0.9 s),
  which reads as run spread at these levels; 0028 stays on.

The same batch measured Mesa 0037's switches on `gl2min_color16` (0027 off): with
`NV30_SYSMEM_COPY=2` and `NV30_INVALIDATE_BUFFER=1` the 61 MB of copies and the stalls go too
(docs/MEMORY.md "Measured on the PS3 (batch 17)"). With 0027 on, OpenArena doesn't need them.
