# OpenArena

OpenArena 0.8.8 (Debian's data, in the image) runs on our ioquake3 build with both of its renderers.
The fixed-function `opengl1` renderer is the suite's baseline and plays at 70.6 steady fps with no
gameplay stalls (row 59). The GLSL `opengl2` renderer, in its lean `gl2min` settings, reached 56-60
fps with 0-2.4 s of gameplay stalls once ioquake3 462 sent its vertex colours in a format nv30 can
fetch (rows 69, 70). What is left is in [backlog/games.md](../backlog/games.md).

## Packages and patches

`ioquake3`, `ioquake3-server`: our build of Debian's `ioquake3`
(`1.36+u20260719.58839361+dfsg-1+ps3.N`, package directory `gamedb/engines/ioquake3/packages/ioquake3/`), replacing Debian's. The PS3
runs ps3.462 since batch 17; the release candidate is ps3.396 ([RELEASE-CANDIDATES.md](../RELEASE-CANDIDATES.md)).
Every change is behind a cvar, so a recipe variant can turn it back.

**renderergl2 memory (0001-0010)**, the patches that made opengl2 fit:

| Patch | What | Cvar (the upstream behaviour) |
|---|---|---|
| 0001 | GLSL shader objects deleted once their program links (no source and IR kept in Mesa) | `r_glslKeepShaders 1` |
| 0002 | no GPU bone-animation programs (27 programs indexing a 105-matrix array, compare trees on nv30) | `r_glslMaxBones -1` |
| 0003 | none of the 15 shadow and post-processing programs that need framebuffer objects | `r_glslAllPrograms 1` |
| 0004 | compiled programs kept across renderer restarts (no second compile burst at the map load) | `r_glslKeepPrograms 0` |
| 0005 | opt-in: each program built at its first use (`r_glslLazy 1`); adds stalls when a variant is first drawn | |
| 0006 | the VAO cache allocated only with `r_vaoCache` (21 MB never drawn from) | |
| 0007 | no permanent CPU copy of MD3 vertex frames (about 87 MB of hunk for the demo's models) | `r_keepModelVerts 1` |
| 0008 | no tangents in MD3 VBOs without normal mapping | |
| 0009 | `r_md3Lods`, and no LODs that `r_lodbias` never draws | |
| 0010 | `r_gpuVertexAnimation 0` animates MD3 on the CPU as opengl1 does (variant `gl2min_cpuanim`) | |

**Engine and opengl1 CPU (0011-0018)**, on in every build since ps3.362: no rescan of the shader text
after a hash miss (`r_shaderTextScan 1`), no crc32 of pk3 entries (`fs_zipCrc 1`), the AltiVec MD3
interpolation and lighting built and on (`com_altivec 0`), no memsets per decal clip plane
(`r_markClear 1`), a player's sounds loaded at their first play (`s_alLazy 0`), pk3 entries through
libdeflate (`fs_zipFast 0`), the message Huffman trees from tables (`msg_huffSlow 1`, a command-line
cvar). There is no 0017. Variant `up` turns all of them back.

**opengl1 batching (0019-0024)**: the lightmap atlas `r_mergeLightmaps` (0019, 0023: values 1 and 2,
`r_mergeLightmapsSize`), a GL state cache `r_glStateCache`, no `glLockArraysEXT` around batches
(`r_lockArrays 0`), the sky box from vertex arrays `r_skyArrays` (islanddm's sky 1453 calls to 18).
0024 makes `r_mergeLightmaps 0` the default. On llvmpipe at GL 2.1: 3022 GL calls and 198 draws a
frame down to 377 and 79 on the bench demo, pixels within 1-4 levels, identical with every switch off.

**Loads during play (0025-0026)**: `r_keepImages 1` keeps the textures of image files across a
reload of the same map; `cl_preloadLate 1` registers again, at the end of the next load of the same
map, what the cgame loaded during the last one's play (joining bots). Both on by default.
`cl_deferPlayers 1` (opt-in) ignores `loaddefered`, so bots keep a substitute model: it changes the
picture.

**renderergl2 vertex formats (0027-0029)**:

| Patch | Cvar (default) | What |
|---|---|---|
| 0027 | `r_vaoSnormColor 1` | vertex colours as GL_SHORT normalized, which nv30 fetches, instead of 16-bit unsigned that u_vbuf converted at every draw (and that made nouveau keep malloc copies of every buffer it read: about 62 MB for the demo's models) |
| 0028 | `r_mipMapPowf 0` | the gamma-correct mipmap bytes from a threshold table instead of a `powf` per texel channel (7.8% of a profile's samples); the same bytes |
| 0029 | `r_md3PackedFrames 0` (opt-in, needs `r_normalMapping 0`) | animated MD3 frames in 12 bytes a vertex instead of 20 (16-bit positions, 8-bit normals): the demo's model VBOs 62.2 to 37.5 MB of VRAM |

The launcher (ps3-desktop) carries OpenArena's PS3 settings. Texture compression is the game's own
`r_ext_compressed_textures 1` (shipped on); Mesa makes DXT1 of the opaque textures, on the SPEs
through libspeq (`SPEQ_S3TC`; RGB sources on the SPEs too since libspeq 0.394, `SPEQ_S3TC_RGB=ppe`
sends them back to the PPE).

## The recipe

`gamedb/games/openarena/benchmark/recipe.sh`: the timedemo `bench.dm_71` (`gamedb/games/openarena/benchmark/assets/openarena-bench.dm_71`),
looped in one session (`PASSES=1`; `bench_loop` and `bench_again` aliases in autoexec.cfg, since
`nextdemo` is cleared before each pass). Passes starting 54 s or more after the first map load count
until about 120 s. Every pass reloads the map, which the gameplay columns leave out. `fps=`, `maxms=`
and `load=` are the first pass's, as the single timedemo had them. Parsing: `([0-9.]+) fps`, the
longest frame from the `min/avg/max/` field, the load from `CL_InitCGame: N seconds`; parse() adds
the GLSL program counts and the hunk's numbers.

OpenArena keeps only 32 `+` commands on its command line (Debian's wrapper passes about 10), so the
recipe writes its cvars into autoexec.cfg.

Variants: `tc0`, `tc1` (opengl1, compression off and on; base is tc1), `gl2min` (opengl2 with
normal, specular and deluxe mapping, sun, dlights, depth prepass, HDR, post-processing, tone mapping,
auto exposure, SSAO, cube maps and FBOs off), `gl2` (Debian's opengl2 defaults), one variant per
patch above (`gl2min_shaders`, `_bones`, `_fboprog`, `_recompile`, `_lazy`, `_modelverts`, `_lods1`,
`_lodbias1`, `_cpuanim`, `_lean`, `_picmip2`, `_tc0`, `_color16`, `_mippowf`, `_md3pack`, `_up`;
`scan`, `crc`, `noaltivec`, `markclear`, `sndeager`, `zlib`, `huffslow`, `up`; `mergelm1`, `mergelm2`,
`lmsize512`, `lmsize256`, `glcache0`, `lockarrays`, `skystrips`, `allbatchoff`; `keepimg0`, `preload0`,
`loads0`, `defer`), `tc1nowm` (no window manager, as step 0 ran), the OpenAL Soft variants of
`openal.inc` (`allog`, `b16`, `f44`, `b16f44`, `p1024`, `alnull`), `nosound`, `kick100`, `rgbspu`,
`rgbppe`.

## Current results

| Renderer | Steady fps | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Row |
|---|---|---|---|---|
| opengl1 (base, tc1) | 70.6 | 0.0 s / 34.6 / 56 ms | 26 / 39 MB | 59 (ioquake3 396, kicker) |
| opengl2 `gl2min` | 59.5 | 2.4 s / 21.9 / 868 ms | 15 / 117 MB | 69 (ioquake3 462) |
| opengl2 `gl2min`, USB / SSD data / SSD data and swap | 55.9 / 56.1 / 56.1 | 0.3 / 0.0 / 0.1 s; longest 111 / 96 / 105 ms | 13 / 12 / 14 MB; 117 MB | 70 (n = 2 / 1 / 3) |
| opengl2 `gl2min`, SSD root (n=3) | 60.4 (60.3-63.3) | 0.2 s / 25.6 / 199 ms | 15 / 119 MB | 71 |
| opengl2 `gl2min`, SSD root, later in the same boot, `NV30_FORCE_DXT_STATS=50` in both arms (n=2 + 2) | 56.3 off / 55.8 `opaque` | 0.0 / 0.4 s; longest 89 / 191 ms | 15 / 20 MB; 120 MB | 74 |
| opengl2 `gl2min_md3pack` | 59.0 | 1.3 s / 23.5 / 943 ms | 17 / 98 MB | 69 |

Picture and sound right in both renderers. opengl2 holds Rss 79-104 MB plus 58-68 MB swapped
(opengl1: 71 + 20). The idle fix confirmed on kernel .433 without the kicker (row 68, clean): opengl1
gameplay stalls 0.4 and 0.4 s with `ps3_idle_prep` against 9.0 and 7.9 s without, longest frame
0.22-0.38 s against 2.2-2.8 s.

The load (batch 17 profiles): opengl2's load takes 28.1 s from exec to the end of the loading screens
(10.2 s by the game's own `CL_InitCGame`), 10.9 s of it disk wait; the demo's second pass reads the
map and players again (131 MB, as much as the load) because memory pressure evicted them. With the
game data on the SSD (batch ssd1) the load fell 28% (10.7 to 7.7 s). With the whole root on the SSD
it takes 6.9 s (6.7-7.1) by the game's number and 14.2 s to the end of the loading screens, the
load's disk wait 2.5 s against 6.3 s, the window's disk stall seconds 1.1 against 4.4, its reloads
40.7 s against 55.7 s and the first minute's stalls 20 s against 28.6 s. Gameplay was clean on USB
already (0.3 s), so there was nothing more to win there (0.2 s). Steady fps was 60.4 in the baseline
and 56 for the rest of the boot (the force-DXT runs, a profiled run); the USB root gave 55.9, so an
SSD effect on OpenArena's fps is not established. The gameplay profile is Mesa 23.1%, the game's
renderer 24.1%, OpenAL 12.4%, kernel 14.2%, libdeflate 3.9%, ps3rsx 5.7%
([HOTPATHS.md](../HOTPATHS.md#gameplay-profiles-on-the-ssd-root-batch-ssdroot2-p1b)).
[PROFILING.md, "Start-up profiles, batch 17"](../PROFILING.md#start-up-profiles-batch-17) and
[the SSD root](../PROFILING.md#start-up-profiles-on-the-ssd-root-batch-ssdroot2);
[GAMEOS-HDD.md](../GAMEOS-HDD.md).

## What stalls and why

**opengl1** ([STALLS.md, "OpenArena"](../STALLS.md#openarena)). Before 0025-0026, of the
17.6 s of window stalls about 10 s were the map reload at each pass (three load-screen frames of
340, 700 and 380 ms: textures read from pak4-textures.pk3, decoded and compressed on the PPE) and
about 7 s five bots joining: the demo adds a bot every 10 s of game time (`bot_minplayers`) and
qagame's `loaddefered` makes the cgame load each one at once. The same happens in real play at every
match with bots. With keep and preload on, gameplay stalls went 9.9 and 9.0 s to 0, the longest frame
1057 and 807 ms to 64 and 47 ms, steady 64.6 to 70.4 (row 59, against `loads0`). Many of the remaining
"random" stalls were the kernel's lost decrementer: 16.1 and 13.3 s with nothing, 6.5 and 6.2 s with
the 10 ms kicker (row 56), 0.1-0.5 s with the kernel fix (rows 66, 68).

**opengl2** ([MEMORY.md, "OpenArena opengl2"](../MEMORY.md#openarena-opengl2-gl2min-the-16-s-freezes-and-the-memory)). Its "16 s gameplay freezes" were the
reload's last frame: the cgame load, `cl_preloadLate` (7-12 s in opengl2 while the PPE swaps) and
`RB_ShowImages` run inside one frame that outlasted report.py's cut. report.py now gives a frame that
starts in a reload segment to the reload: the profiled run went 54.7 to 16.0 s. The stalls left
were memory: MemAvailable 13-60 MB, swap-ins and VRAM swap write-backs every 0.5 s. Not the GLSL
compiles (55 programs built once at the first start in 3.0-3.4 s and kept; none during play) and not
FBOs (none in gl2min). Batch 17 measured the fix: gl2min on ioquake3 462 steady 57-60 with stalls of
0.7-2.6 s, against 51-53 and 9-11 s on 396 in batch 16; `gl2min_color16` (0027 off) 52-53 with 12.9 s
of stalls and 20 s of swap; `md3pack` 20 MB less VRAM, the same stalls; `mippowf` (0028 off) the
lowest stalls, 0.7-0.9 s. On the old colour path, Mesa's `NV30_SYSMEM_COPY=2` with
`NV30_INVALIDATE_BUFFER=1` (Mesa 0037) cut the stalls from 9.9 and 12.3 to 1.5 and 1.4 s and swap from
20-26 to 0.6-1.1 s, steady 53-54 to 59.

**Mesa 0038 `NV30_FORCE_DXT=opaque` does nothing on gl2min** (batch ssdroot2 P2, x2 against off): it
forced 0 textures, every one of 2238 uploads counted as `skip_format` (a format the switch does not
take; which one is not recorded, sRGB or already compressed uploads being the likely ones), and fps,
stalls and VRAM were equal
([MEMORY.md](../MEMORY.md#measured-on-the-ps3-batch-ssdroot2-p2)). `gl2min_tc0`, which leaves the
compression to Mesa, is not run yet. Mesa 0039 `NV30_SHADER_CACHE=on` cold
and warm ([PROFILING.md, "nv30's shader cache"](../PROFILING.md#nv30s-shader-cache)); its upper bound is 0.24 s of the
load. The engine CPU patches 0011-0018 were never A/B'd one by one on the PS3 (`up` against base).

Next: whether `r_md3PackedFrames 1` becomes the default, why the switch skips every format here, and the proposals
(`com_hunkTrim`, `r_keepImages` for renderergl2, ADPCM sound effects) in
[backlog/games.md](../backlog/games.md).

## History

- **2026-10-15**: the GLSL renderer was tried once and dropped ("ran out of RAM compiling its
  shaders", 82b6b5f), before the VRAM swap and game mode.
- **2026-10-16, stage 0**: opengl1 tc1 42.2 fps (3 runs, load 10.8 s), 47 HUD median, peak RSS 83 MB,
  VRAM 37-40 MB (row 11). The same runs with nothing on screen gave 45-54: showing the frames costs.
- **2026-10-17**: Mesa ps3.262 took opengl1 from 44.2 to 54.4 (+23%), ps3.299 52.4. opengl2 runs on
  nv30 (e708512: all 97 programs link, compiled twice at about 25 s each); Debian's build 10.7 fps,
  `gl2` 15.9, then our 0001-0010 (ps3.285): gl2min 14.8 to 26.1-31.8, the longest stall 24.7 to 6 s,
  CPU 103 to 55 s; 0007 is most of it (17.6 without it); `cpuanim` 30.5 at 41 MB VRAM (row 46).
- **2026-10-17/18, opengl1 batching**: steady 51.8 with all four on against 56.4 all off on the RSX
  (row 51); the atlas costs about 5%, not in frame cost (smooth fps 63.7-65.7 either way) but in about
  10 s of extra hitches a run with any atlas page (stall_s 34.1 base, 23.9 atlas off, 35.6 `mergelm2`,
  36.2 `lmsize256`, row 53). Hence 0024. The rest of the batching is neutral and stays on.
- **2026-10-18**: keep and preload (row 59), the recorder and kicker rows (55, 56), the idle fix
  (66, 68), the opengl2 memory work (61, 69), the SSD arms (70).

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md).
