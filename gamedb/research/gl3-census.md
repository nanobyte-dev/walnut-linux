# GL 3 census: open-source games that need GL 3+, and what it takes to run them on the PS3 (2026-10-19)

The question: which open-source games need GL 3+ (or GLES 3), and what would it cost to get each one
working on our RSX (Mesa nv30: GL 2.1, GLSL 1.20, GLES 2.0) with everything else that comes with it:
memory, CPU, big-endian ppc64, data. Less bling is fine; not working is not. Games new on the PS3
come first. Vulkan-only engines are out.

The per-game detail is in `parts/` next to this file (A to F, one section per game in a fixed
template, with file:line citations), the driver-level analysis in `G-driver.md`, and the
brief the agents worked from in `BRIEF.md`. This file is the cross-game view. Labels as in the
parts: measured, read in source, estimate, UNVERIFIED.

## The short answer

1. **Most "GL 3 games" have a GL 2 route already.** Of the 30-odd titles looked at, about half
   either still have a GL 2.x / GLES 2 path in some release (Xonotic 0.8.6, Warzone 2100 4.3.5,
   GZDoom's GLES backend, Endless Sky's experimental GL2 path, Godot 3's GLES2 renderer) or never
   needed GL 3 at all (Unvanquished, OpenMW, Warfork, dhewm3, DDNet, Mindustry, 0 A.D.). The
   cheapest port is usually "build the right release or backend", not "rewrite the renderer".
2. **Memory, not GL, is the gate.** Every game measured on the PC except Neverball-class titles comes
   out above our ~160 MB comfortable budget even after the corrections below; the GL work is S-M
   for the best candidates, the memory work is the open-ended part.
3. **The driver-level "GL 3.3 subset" is not worth building now.** It is technically sound
   (G-driver.md: 7-9 weeks for an honest profile), but only Taisei, and Hypersomnia (blocked by
   other things), fall inside it. Its break-even is three novel games. Keep its cheap, upstreamable
   items on the backlog.
4. **Top 3 to start: Warzone 2100 4.3.5, Xonotic 0.8.6, Endless Sky's GL2 path.** First steps at the
   end.

## How the numbers were taken

- PC measurements ran in throwaway podman containers (Debian sid amd64, Xvfb, Mesa llvmpipe 26.2.5,
  apitrace) with their storage on `/mnt/data_linux/Temp/ps3/gl3census` (podman `--root` there;
  /home stayed untouched). `measure.sh` sampled RSS/VmHWM and per-thread CPU every second;
  `glcensus.py` turned each apitrace dump into a feature census (entry points by GL 3 bucket,
  texture formats, live texture/buffer MB, draws per frame, GLSL features per unique shader). The
  tools are in `$B/bin` (B = that directory); small reports stay in `$B/traces/*` and `$B/work/*-keep`.
- Installed for the runs, all in containers since deleted: Debian amd64 packages endless-sky, naev,
  warzone2100, taisei, openclonk, dhewm3 (contrib) with the Doom 3 demo pk4, openmw with the CC0
  Template and Example Suite, godot3-runner, neverball, cube2/cube2-data; upstream downloads
  gzdoom 4.14.2 .deb, Hypersomnia 3.0.0 AppImage, Mindustry v160.7 jar with Debian's OpenJDK,
  Xonotic 0.8.6 zip. Source was read from shallow clones (most since deleted).
- **Calibration (measured, this session).** llvmpipe holds every texture and buffer in RAM and adds
  its own LLVM/JIT memory, so the brief's conversion was: PS3 RAM ~= PC RSS - 100 MB (glxgears'
  llvmpipe baseline at 1080p) - texture MB - buffer MB + ~10 MB Mesa nv30. Run on two games we
  measured on the PS3, it over-predicts:

  | Game | PC VmHWM (1080p, llvmpipe) | textures / buffers in the trace | formula | PS3 measured (Rss + swap, docs/MEMORY.md) |
  |---|---:|---:|---:|---:|
  | Neverball (Debian 1.6, demo1 replay) | 152 | 5.3 / 0.3 | ~56 | 30 |
  | BananaBread (Debian cube2, map cave) | 529 | 249 / 13 | ~177 | 90 (our build, with its memory patches) |

  So every "PS3 estimate" below that comes from PC RSS is an upper bound, perhaps 1.5-2x too high
  for small heaps (two points only; a shader-heavy game pays more LLVM JIT memory on the PC). They
  still rank the games, and the large ones stay out of reach after halving.

## The ranked table

Value = popularity + novelty + playability on 256 MB (1-5 each, from the parts); effort weight S=1,
M=2, L=3, XL=5; score = value / weight. "Route" is the cheapest way to GL 2.1 found. PS3 RAM is the
estimate before cuts, with the calibration above in mind; "Gate" is what decides whether it plays.

| # | Game | Route to GL 2.1 | Effort | Pop / Nov / Play | Score | PC peak RSS (setting) | PS3 RAM est. / textures | ppc64 | Data | Gate |
|---|---|---|---|---|---:|---|---|---|---|---|
| 1 | **Warzone 2100** | build release 4.3.5 (last GL 2.1/GLES 2 line); no renderer change | S | 4 / 5 / 2 | 11.0 | 396 MB (4.7 lowest, skirmish) | 180-250 (4.3.5, est) / 85 MB | 4.5.5 builds on ports | free, ~150 MB | RAM; CPU on big maps |
| 2 | **Xonotic** | build the 0.8.6 release engine (still has GL20/GL13/GL11 paths; measured "Using GL2.0 rendering path") | M | 5 / 5 / 2 | 6.0 | 848 MB (the-big-keybench) | ~510 upper bound / 199 MB | ours (like darkplaces-gl2) | free, 1.2 GB | RAM; 450 draws, 1945 calls a frame |
| 3 | **Endless Sky** | upstream's experimental GL2 path (3D textures, no VAO, EXT FBO) + 22 GLSL 1.20 shaders to write | M | 4 / 5 / 2 | 5.5 | 1293 steady, 1560 HWM ("reduce large graphics: all") | ~700 MB non-texture unexplained / 477 MB | 0.11.2 builds on ports | free, ~400 MB | the 700 MB; 3D textures max 512 px on nv30 |
| 4 | GZDoom (+ Raze) | its GLES backend has a desktop GL<3.3 mode with GLSL 100 shaders; one GL_RED fix | M | 5 / 2 / 4 | 5.5 | not measured (run stalled at start) | 120-250 (est) | not in Debian; BE paths bit-rotted (est) | Freedoom free | ZScript interpreted off x86-64; BE fixes |
| 5 | Warfork (qfusion) | none needed: ref_gl builds GLSL 120/130/140 with optional extensions | M | 4 / 5 / 2 | 5.5 | not measured | ? | ours | mixed licences, 1.6 GB | RAM; unclear asset licence |
| 6 | FreeSpace 2 Open | an old release (3.7.4, GL 1.2 + optional GLSL, UNVERIFIED); master is XL (arrays everywhere, UBOs) | M (old) / XL | 4 / 5 / 2 | 5.5 | not measured | 200-350 retail (est) | ours; INTEL_INT swaps exist | retail non-free; free TCs may need a new engine | data; BE of the old release |
| 7 | DDNet | none: GL 1.x legacy backend is its default | M (BE build) | 4 / 3 / 4 | 5.5 | not measured | small (est) | sid build fails/never tried; old binary uninstallable | free | the ppc64 build (test hang) |
| 8 | Godot 3 games | GLES2 renderer runs on GL 2.1 (EXT FBO fallback, software skinning); build 3.6.2 with `-DBIG_ENDIAN_ENABLED` | M (engine) + S per game | 2 / 5 / 3 | 5.0 | 321 (2D scene), 378 (3D) | ~150 MB of default 3D atlases to cut | 3.2.3 only (3.6.2 excludes ppc64) | per game, free | 57 KB uber-shaders vs nv30 limits; BE dead code |
| 9 | dhewm3 (Doom 3) | none: ARB assembly programs; explicit BE build, AltiVec SIMD | M | 4 / 3 / 2 | 4.5 | 1161 (demo map) | ~580 non-texture / 474 MB uncompressed | ours (contrib) | non-free; free demo 462 MB | engine heap |
| 10 | OpenMW | none: default shaders `#version 120`; ESM/BSA readers need byte swapping | L | 5 / 5 / 2 | 4.0 | 456 (menu, no cell loaded) | 500-800 after cuts (est) | builds on ports (but can't read Morrowind.esm) | Morrowind non-free; CC0 Template | BE loaders; RAM |
| 11 | Taisei | no GL2 backend since 1.4: a new backend, or the driver subset (instancing, FP16, blit) | L | 3 / 5 / 2 | 3.3 | 562 at 540p (stage 1) | ~240 upper bound / 221 MB | builds on ports, endian-aware | free (Debian ships no music/sfx) | RAM; instancing |
| 12 | Unvanquished (Daemon) | none: officially GL 2.1 + EXT_fbo + ARB_vao, GLSL 120 path | L | 3 / 5 / 1 | 3.0 | not measured | heavy (est) | ours; ppc64 in its CMake; native game DLLs | free, ~800 MB | RAM, CPU, untested BE |
| 13 | OpenClonk | GL 3.2 core only; shaders nearly 1.20, sampler2DArray in 2 landscape shaders, int-indexed 8-bone skinning | L | 3 / 5 / 1 | 3.0 | 742 (64x40 sandbox) | ~350 non-texture / 253 MB | builds on ports | free, ~110 MB | RAM |
| 14 | Hypersomnia | GL port S (GLSL 130 -> 120, 3 textureSize) | L | 2 / 5 / 2 | 3.0 | 440-488 at 720p | ~300 upper bound | not in Debian; x86/arm only, no BE handling | free | BE serialisation, build |
| 15 | Mindustry | none: Arc runs "OpenGL 2 API" (measured) | XL | 5 / 5 / 1 | 2.2 | 663-704 at menu, -Xmx256m | JVM | JIT on ppc64 BE UNVERIFIED; natives need building | free | the JVM on 217 MB |
| 16 | OpenRA | none today (GL 2.1 profile removed after 20231010) | XL | 4 / 5 / 1 | 2.0 | not measured | 400-800 (est) | n/a: .NET 10 has no BE PowerPC port | free (downloaded) | runtime |
| 17 | Naev | Rust renderer, GL 3.3 core, UBO, instancing, texelFetch, MSAA, glTF PBR | XL | 3 / 5 / 1 | 1.8 | 1693 (0.8.2, loading) | far over | 0.8.2 builds; master needs rustc 1.89 + SDL3 | free | everything |
| - | Beyond All Reason / Recoil, Zero-K | GL 3.0+ up to 4.3 shaders, GL4 widgets | XL | 5 / 5 / 1 | - | - | 2-4 GB | no | free, ~3 GB | out |
| - | 0 A.D. | GL 2.1 already (ARB or GLSL) | XL | 5 / 5 / 1 | - | - | 2 GB+ | not built for ppc64 | free | out (RAM, arch) |
| - | The Dark Mod | GL 3.3 required (qgl.cpp:74) | XL | 3 / 5 / 1 | - | - | 4 GB class | no | free | out |
| - | Red Eclipse 2 | starts on GL 2.0 but deferred-only G-buffer, light passes over 1080p | XL | 3 / 5 / 1 | - | - | - | - | free | out (fill rate); 1.6 already runs |
| - | Barotrauma | .NET 8, x64 native libs, EULA source, paid data | XL | - | - | - | - | n/a | non-free | out |
| - | RBDOOM-3-BFG, Veloren, SDL3-GPU games | NVRHI / wgpu / Vulkan-Metal-D3D12 only | - | - | - | - | - | - | - | out (no GL) |

Not GL 3 problems, noted so they aren't looked at again: OpenMoHAA and ET: Legacy (ioq3-family GL1
renderers, non-free or mixed data), Colobot (GL 1.4 default device, in Debian ppc64), Trigger Rally,
MegaGlest, FreeOrion (GL 2-class, heavy), OpenSpades (GL2 or software), sm64ex (GL_LEGACY, needs
a ROM), Ship of Harkinian (`#version 130` one line from 120, needs a ROM), raylib games (a rebuild
with `GRAPHICS_API_OPENGL_21`), Doomsday 2.3 (a GLES2 build switch exists; low value next to
dsda-doom), SuperTuxKart's modern renderer and Yamagi gl3 (we run the legacy/gl1 ones; they lose
lighting and post effects, nothing that matters for play). Pioneer (core 3.1+), Stunt Rally 3, Rigs
of Rods and Overgrowth are GL3/Ogre-Next class and out on RAM.

## The feature census: which GL 3 features block games

Counted over the renderers that really need GL 3 in their current version (Endless Sky's GL3 path,
Naev, Taisei, OpenClonk, Hypersomnia, Warzone 2100 4.7, OpenRA, BAR/Recoil, FS2Open master, The Dark
Mod). "Required" means no fallback inside the game; "optional" uses are left out. From the FEATURES
lines of the parts and the traces.

| Feature | Required by | Count | Driver-level route (G-driver.md) |
|---|---|---:|---|
| Core profile context only | Naev, Taisei, OpenClonk, Hypersomnia, WZ 4.7, OpenRA, FS2Open, TDM | 8 | free: Mesa already creates core 3.3 on nv30 with the overrides; dangerous without the rest |
| ARB_fbo semantics (blit, MSAA FBOs, mixed sizes) | Taisei (859 blits a stage), Naev, WZ 4.7, OpenRA, FS2Open, BAR | 6 | M: linear attachments for mixed sizes; blit through nv30's blitter; MSAA faked single-sample |
| Uniform buffer objects | Naev, WZ 4.7, FS2Open, BAR, TDM | 5 | M-L: flatten into nv30's constant space at validate; exact while blocks fit 462 VS / ~224 FS vec4 |
| Instancing | Taisei (37.7k instanced draws in the stage trace), Naev, WZ 4.7, FS2Open, BAR | 5 | M: driver loop with gl_InstanceID as a constant, later the 0x1fc0 frequency divider |
| Texture arrays | Endless Sky (GL3 path), OpenClonk, WZ 4.7 HQ terrain, FS2Open (every texture), BAR | 5 | none good: split textures (few layers), 3D (no mips), atlas (seams) |
| Integer ops beyond small arithmetic | Naev, WZ 4.7, OpenRA, BAR | 4 | small ints exact as floats; `%`, shifts by constants, masks rewritable (S) |
| `flat` varyings | Naev, WZ 4.7, OpenRA, BAR | 4 | S: interpolate + round, right when constant per primitive |
| textureSize | Naev, OpenRA, FS2Open, BAR (Hypersomnia optional x3) | 4 | M with texelFetch: constant slots filled at validate, exact |
| Dynamic loops / big shaders | Naev, FS2Open, BAR, Red Eclipse 2; OpenClonk's skinning VS | 4-5 | none: NIR unrolls; over-limit fragment programs fail silently |
| texelFetch | Naev, FS2Open, BAR | 3 | M: TXL at texel centres, exact |
| RG textures (R8/RG8) | WZ 4.7, BAR (Taisei, Endless Sky fog, Naev, GZDoom optional/patchable) | 2 + 4 | sampling already works through L8/A8L8; rendering S (BGRA8 promotion) or M (B8/G8B8) |
| Float textures / render targets | Taisei (RGBA16F HDR background, RG16F lasers), BAR | 2 | S-M: the RSX has FP16 (filtered, blended) and FP32 (neither); Mesa's table entries are commented out |
| Bitwise ops, bit casts | OpenRA, BAR (Taisei: one floatBitsToUint) | 2 | never in general |
| Geometry shaders, transform feedback, compute | none required (FS2Open, BAR, Unvanquished, OpenMW use them optionally) | 0 | CPU draw module only: not for real time |

What the table says: no single feature unlocks many games. The four most common hard needs are
ARB_fbo blits, UBOs, instancing and texture arrays, at 5-6 games each, and the games that need them
are mostly the same heavy ones (WZ 4.7, FS2Open, Naev, BAR) that also need texture arrays, loops or
bitwise ops the RSX can't do, or RAM we don't have. Texture arrays are the worst: required by five,
emulable by none without losing mipmaps or exactness. VAOs, sync objects, primitive restart and
MRT 4 are present already and blocked nobody.

## The driver-level option: verdict

(Detail, sources and the 34-row feature table in `G-driver.md`.)

- **What exists.** Mesa creates a GL 3.3 core context on nv30 when `MESA_GL_VERSION_OVERRIDE=3.3` and
  `MESA_GLSL_VERSION_OVERRIDE=330` are set; GLSL 330 parses, ints are lowered to floats. But nothing
  behind the version is turned on: instanced draws draw one instance with no error, texelFetch,
  textureSize and bitwise ops make fragment programs fail translation and the previous program stays
  bound, `flat` silently becomes smooth, and the first geometry shader likely crashes (nv30 has no
  `create_gs_state`). **The plain overrides must never be set for a game.**
- **What the hardware has that Mesa leaves off.** FP16 RGBA textures and render targets (filtered,
  blended) and FP32 (unfiltered, unblended): table entries exist in `nv30_format.c:120-152` with the
  bindings commented out. The instancing frequency divider (0x1fc0, no instance ID). TXL/TXD and
  DDX/DDY in fragment programs (TXL is already translated, the cap is off). Per-MRT blend enable
  (code exists, cap off). sRGB output (0x1fec). 4 vertex texture units (FP32 2D nearest). Each of
  these sends new methods or formats, so each needs its ps3rsx validator check first: one bad push
  wedges the RSX until a reboot.
- **An honest "GL 3.3 subset"** (GLSL 330 syntax, VAOs, small ints, texelFetch/textureSize,
  instancing with gl_InstanceID, gl_VertexID, UBOs that fit, R8/RG8, FP16/FP32 targets, mixed-size
  FBOs, derivatives, textureLod) is 7-9 weeks of driver work (est), gated per executable by driconf
  plus an env switch, reporting true limits so oversize shaders fail at link. It never covers
  bitwise-heavy shaders, integer textures, texture arrays with mipmaps, GS/TF at game rates,
  compute, more than 8 varyings, dynamic loop counts.
- **Which census games it would unlock.** Taisei fits: core 3.3, 17 shaders all `#version 330 core`,
  instanced sprites, R8 textures, 4 draw buffers, derivatives, no UBO/GS/TF/arrays. Hypersomnia
  fits too, but its GL port is S anyway and it is blocked by big-endian serialisation and an
  x86/arm-only build. Endless Sky, Warzone 2100 and Xonotic have their own GL 2 routes. OpenClonk
  needs arrays; Naev, FS2Open, OpenRA, BAR need things outside the subset or a runtime we lack.
  That is one, maybe two games, below the break-even of three.
- **Verdict: don't build it now.** Port per game, by the cheapest route (an older release or an
  existing fallback path). Keep the cheap, exact, upstreamable items on the backlog for the Core 2 +
  7600 GT upstreaming work, since they help every NV4x user and some GL 2 games here: the
  derivatives cap and ARB_shader_texture_lod (S), FP16/FP32 formats (S-M), independent blend (S),
  and ARB_framebuffer_object via linear attachments (M; renderergl2's FBO features and Neverball's
  effects want it too). Revisit the whole subset if Taisei or a second instancing-heavy GL 3.3 game
  becomes a target: then do items 1, 2, 5, 6, 11 of G-driver.md's list first (gate, int maths,
  float targets, instancing loop, ARB_fbo), about 5 weeks.

## The top 3 and first steps

### 1. Warzone 2100 4.3.5

Why: a real RTS (we have only OpenTTD's kind), popular, free data, no PS3 port, and the GL work is
nil: 4.3.5 (2023-04-16) is the last release with GL 2.1 and GLES 2.0 contexts and GLSL 1.20
shaders, no instancing, UBOs or arrays, with a LUMINANCE fallback for R8 (C-rts.md). 4.4.2+ needs
GL 3.0. It has no GameNetworkingSockets (4.7's big-endian static assert doesn't apply), and its
binary reads go through physfs's little-endian helpers.

First steps:
1. Package 4.3.x as debsrc: Debian bookworm ships warzone2100 4.3.3-3, built on s390x (big-endian)
   as well as ppc64el (qa.debian.org madison), so its packaging is the starting point; rebuild it
   for sid ppc64 in the chroot, then bump to 4.3.5.
2. On the PC, measure 4.3.5 at the lowest settings (classic terrain, texture size 128, no shadows,
   1v1 on a small map) with `measure.sh`, to replace the 180-250 MB estimate; the 4.7 run gave 396 MB
   PC peak and 85 MB of textures. `--autogame` ends after 8-55 s; start from the lobby with xdotool
   as the C agent did.
3. PS3 smoke run (when the PS3 is free): grep stderr for nouveau "out of temps", watch the PPE
   split; sound through OpenAL. Then a bench recipe (a fixed skirmish, HUD fps).
4. Memory cuts if needed: texture size, Mesa 0038 DXT at upload, mono 22 kHz sound, small maps.

### 2. Xonotic 0.8.6

Why: the best-known open-source arena shooter, free data, novel on the PS3, and the 0.8.6 release
engine still has the GL20 renderpath (measured on a forced GL 2.1 / GLSL 120 context, 36 shaders all
`#version 120`). Only engine master and Debian's darkplaces went GL 3.2 core / GLES2, and master's
GLES2 path is no way out (doesn't build natively, no S3TC, no 3D lightgrid; D-fps.md).

First steps:
1. Run Nexuiz Classic on our `darkplaces-gl2` first (PLAN 1.12, waiting on the user's disk decision
   D12): same engine family, same risks (GL20 shaders on nv30, the skeletal code's CPU cost,
   16-bit sound byte order), and a third of the data.
2. Package the 0.8.6 engine as `xonotic-gl2` the way darkplaces-gl2 was done (`-std=gnu17`, no SSE
   rasterizer off x86, the Debian link patches). Check DP_QC_FS_SEARCH_PACKFILE, the one extension
   Xonotic's QuakeC checks that our 2018 snapshot lacks (so the 0.8.6 engine, not the old snapshot).
3. A lean config like nexuiz-lean.cfg (effects-low, `gl_picmip 2`, `r_texture_dds_save 1`, no
   realtime lights, no bloom) and a re-measure on the PC: the-big-keybench peaked at 848 MB with
   199 MB of textures, 450 draws and 1945 GL calls a frame. Pick small maps.
4. Data: 1.2 GB (pk3s); a ps3-dev data package only after the user decides on disk, like D12.

### 3. Endless Sky (GL2 path)

Why: popular, fully free, already built for ppc64 in debian-ports (0.11.2), novel on the PS3, 2D
with simple shaders (22 in the trace, the largest 2.7 KB), and upstream already has an experimental
GL2 path (GameWindow.cpp:242-247, opengl.cpp:43-85): GL_TEXTURE_3D instead of arrays, no VAO, EXT
FBO. What upstream doesn't ship is its GLSL 1.20 shaders. The risk is memory, so step 1 is a go/no-go.

First steps:
1. Go/no-go on the PC: heaptrack (or massif) 0.11.2 at "reduce large graphics: all" to explain the
   ~700 MB of RSS that isn't textures (1293 MB steady, 477 MB of textures in 2766 array textures).
   If it is decoded image buffers or llvmpipe per-texture overhead, it goes away on the PS3; if it
   is game state, stop here.
2. Check the 3D-texture route against nv30: `GL_MAX_3D_TEXTURE_SIZE` is 512 here, and 3D mip levels
   halve the depth, so large single-frame sprites (planets, landscapes) need plain 2D textures in the
   GL2 path, and animation frames lose their mips (UNVERIFIED whether ES mipmaps sprites).
3. Write the 22 shaders at GLSL 1.20 as the plugin upstream expects, replace the R8 fog map with
   LUMINANCE (FogShader.cpp:207); test on llvmpipe with `MESA_GL_VERSION_OVERRIDE=2.1` (a test tool
   here only, never for a game on the PS3); offer them upstream.
4. Then DXT at upload (Mesa 0038) for the sprites, and a PS3 smoke run.

Runners-up, in order: GZDoom's GLES backend (fits memory, popular for mods, novelty low), Warfork
(unmeasured, asset licence unclear), dhewm3 with the demo (non-free data, 580 MB engine heap on the
PC), OpenMW (needs the user's Morrowind and an ESM/BSA byte-swap pass), Godot 3 (engine work once,
then many small games).

## Open questions and gaps

- The calibration has two points. A third, OpenArena opengl2 (shader-heavy, measured on the PS3),
  would show how much LLVM JIT memory inflates shader-heavy PC numbers.
- Not measured: GZDoom (stalled at startup), Warfork, Unvanquished, FS2Open, OpenRA, BAR, DDNet,
  Warzone 4.3.5 itself (only 4.7), Endless Sky and Taisei in actual flight/gameplay beyond stage 1.
- Novelty rests on web searches that found no GameOS homebrew port of any of these; PS3 homebrew
  lists are scattered, so "5" means "none found".
- Big-endian claims are from source reading (swap macros, CMake checks, loader helpers); none of
  these games ran on ppc64 here. OpenMW's ESM/BSA readers and Godot 3's dead BE code are known
  problems; Hypersomnia, GZDoom and old fs2open are UNVERIFIED.
