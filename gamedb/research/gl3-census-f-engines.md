# Part F: engines and runtimes (OpenMW, Barotrauma, Godot 3 games, SDL3 GPU / raylib, skips, scan)

Measured on the PC in podman boxes (Xvfb 1920x1080, llvmpipe, apitrace). Everything not marked
"measured" or "read in source (file:line)" is UNVERIFIED or an estimate.

## OpenMW (OpenSceneGraph, 0.51.0, git 2026-10-18 master)

Verdict: Already GL 2.1 / GLSL 1.20 in its default configuration, so it is not GL3-blocked. Real work is
big-endian (ESM and BSA readers do no byte swapping, so Morrowind.esm cannot be read on ppc64 today; M),
then CPU and memory tuning for a 3.2 GHz in-order PPE with ~160 MB. Debian-ports ppc64 builds
0.51.0-1+b4. Data is non-free (Morrowind) unless one plays the CC0 Template and Example Suite. Novel on
the PS3: yes.

FEATURES: glsl_min=120 core_only=n fallback=gl2 int_ops=opt
  bitwise=opt (clustered lighting only) texelFetch=no textureSize=no ubo=opt (postprocessing, GLSL>=330 only)
  instancing=no tex_arrays=opt (multiview/VR only) float_tex=opt (postprocessing: RGB16F normals) float_rt=opt
  rg_tex=no mrt=opt (postprocessing normals via glDisablei) vao=no sync=no geom=no tf=no compute=opt (clustered lighting)
  derivatives=opt (2 shaders, water/normal effects) textureLod_fs=no vertexid=no flat=no
  arb_fbo=opt (only with postprocessing/refraction/shadows; OSG uses EXT FBO) srgb_fb=no
  vtf=no dyn_loops=no (light loops are over a define-sized count; water has a 3x3 unrolled loop)
  big_shaders=maybe (per-pixel lighting object shader: 171 live lines, 8 point lights; UNVERIFIED against 32 temps)
SCORES: popularity=5 novelty=5 playability_256MB=2 effort=L
  ppc64=builds (Debian-ports 0.51.0-1+b4 installed; also s390x) data=non-free (Morrowind ~1 GB; CC0 Template+Example Suite 160 MB) size_MB=1000

The detail (GL needs, the port, memory, CPU, big-endian, data, novelty and its sources) is in the game's bundle: [gamedb/games/morrowind/investigations/2026-10-19-gl3-census.md](../games/morrowind/investigations/2026-10-19-gl3-census.md).

---

## Barotrauma (MonoGame fork, .NET 8, repo pushed 2026-09)

Verdict: Skip. Not a GL problem (MonoGame DesktopGL needs OpenGL 2.0 plus ARB_framebuffer_object or
GL 3.0, per docs.monogame.net; UNVERIFIED for Barotrauma's fork) but a runtime and licence problem:
`LinuxClient.csproj` targets net8.0 and ships x64-only native webm and Steamworks libraries; there is no
.NET 8 for ppc64 BE (Debian-ports has Mono 6.14.1 on ppc64, which cannot run net8.0 code). The source is
available under an EULA (not open source; README says read EULA.txt first), and the Content folder is not
in the repo, so the paid Steam game is needed.
FEATURES: glsl_min=? (MojoShader-translated GLSL, 1.10-class UNVERIFIED) core_only=n fallback=gl2 int_ops=? mrt=? arb_fbo=req
SCORES: popularity=4 novelty=5 playability_256MB=1 effort=XL
  ppc64=n/a (needs .NET 8) data=non-free size_MB=3000
Novelty: not on PS3. Not worth it.

---

## Godot 3 (3.6.2 source; Debian sid source 3.6.2+ds-1, ppc64 binary is old 3.2.3-stable-1+b2)

Verdict: Best find of the part. Godot 3's GLES2 renderer runs on desktop GL 2.1 with an explicit EXT FBO
fallback, auto-falls back from GLES3 to GLES2, and software-skins when there are no vertex textures, so the
engine is already shaped for nv30. The risks are the huge uber-shaders, 150 MB of default 3D buffers, big-
endian binary arrays, and that Debian ppc64 only has the stale 3.2.3 (sid's 3.6.2 excludes ppc64:
Architecture amd64 arm64 ppc64el riscv64), so we must build 3.6.2 ourselves. Many Godot 3 games exist.

FEATURES: glsl_min=120 core_only=n fallback=gles2 int_ops=no
  bitwise=no texelFetch=no textureSize=no ubo=no instancing=no tex_arrays=no float_tex=opt float_rt=opt
  rg_tex=no mrt=no vao=no sync=no geom=no tf=no compute=no derivatives=opt textureLod_fs=opt (macro falls back to texture2D with bias)
  vertexid=no flat=no arb_fbo=opt (EXT FBO fallback in code) srgb_fb=no vtf=opt (software skinning if 0 units)
  dyn_loops=opt (7 of 15 shaders have loops, dynamic_index in 7) big_shaders=req (57 KB fragment sources)
SCORES: popularity=5 novelty=5 playability_256MB=3 effort=M
  ppc64=fails (3.6.2 not built on ppc64; 3.2.3 builds) data=free size_MB=34 (binary) + per game

### 1. GL needs (source + trace)
- `drivers/gles2/rasterizer_gles2.cpp:162-214`: needs GL >= 2.1, and ARB_framebuffer_object **or**
  EXT_framebuffer_object (function pointers rebound to the EXT versions). `platform/x11/os_x11.cpp:351-404`
  tries GLES3-compatible first and falls back to GLES2; `--video-driver GLES2` forces it.
- `rasterizer_storage_gles2.cpp:6334-6530`: float textures optional (`float_texture_supported`), S3TC used,
  depth textures optional (else RGBA-packed shadows), `support_write_depth` optional, 32-bit indices
  assumed on desktop, half-float vertices; `use_skeleton_software = no float textures || max vertex
  texture units == 0` (:6530): nv30 reports 0, so skeletons skin on the CPU automatically.
- `shaders/scene.glsl:758-775`: textureLod only if GL_EXT/ARB_shader_texture_lod exists, else `texture2D(img,
  coord, lod)` (bias). nv30 lacks it: reflection roughness lookups become biased, not blurred as designed.
  No derivatives needed in scene.glsl (grep found no dFdx).
- Measured (godot3-runner 3.6.2 amd64, project: cube + sphere + plane, 1 directional + 1 omni light with
  shadows, GLES2, 1920x1080, 15 s trace): context GLX compat, 15 shaders all `#version 120`, extensions
  used only GL_ARB/EXT_shader_texture_lod, 33 draws per frame, ~990 GL calls per frame, 62 entry points, no
  GL3 buckets at all. Biggest fragment shader 57,075 chars (omni and directional shadow + lights ubershader),
  vertex 30,891 chars. Textures: 161 2D, 30 cube faces, 35 depth; internal formats RGBA, LUMINANCE_ALPHA,
  DEPTH_COMPONENT, RGB8. Whether the 57 KB fragment programs fit 4096 instructions / 32 temps is UNVERIFIED
  (needs the PS3); Godot has `force_vertex_shading` and fewer-light variants as outs.

### 2. Port to nv30
Nothing to port for the engine; set `rendering/quality/driver/driver_name="GLES2"`. Drop shadows or use
simple ones if the shader fails to link (nv30 keeps the previous program: "silently wrong"). Lost features
vs Godot 3 GLES3: no GI probes, SSAO/SSR, glow, DoF, bicubic lightmaps, many tonemap/HDR effects (GLES2 has
no HDR); 2D games are unaffected. 3D games with Environment effects look flatter.

### 3. Memory
Measured: empty 2D scene (one Label) 321 MB peak RSS at 1080p; 3D shadow scene 378 MB. Trace of the empty
2D project: 151 MB of textures uploaded at start (shadow atlas, cube/radiance map pools, depth, all created
even for a 2D game), 2.3 MB buffers. So PS3 estimate for an empty 2D project: 321 - 107 (llvmpipe) - 151
(GL textures, which live in VRAM) = ~65 MB + 10 Mesa nv30; the 151 MB of VRAM is the problem (VRAM is also
swap). Cuts: `rendering/quality/shadow_atlas/size` (default 4096), `directional_shadow/size` (4096),
`reflections/atlas_size`, `reflections/roughness_layers`; a 2D-only game can drop most to 1 or 64 and
reclaim ~140 MB. A 2D game on Godot 3 GLES2 is then a ~100-150 MB game: fits.

### 4. CPU
GDScript interpreter (slow, PPE-bound), 2D batching in GLES2 (canvas batching exists in 3.2+), physics on the
main thread. Per-frame GL calls: 152 for an empty 2D scene, ~990 for the 3D test, driver cost applies.
Software skinning in 3D skinned characters costs PPE (could use libspeq). 2D games at 60 fps plausible;
3D games hard.

### 5. ppc64 big-endian
- File format core is byte-wise little-endian: `core/os/file_access.cpp:164-210` `get_16/get_32/get_64`
  assemble from bytes (endian-independent); `.pck` header reads use `get_32`.
- Binary resources carry arrays as raw memory with `#ifdef BIG_ENDIAN_ENABLED` swaps
  (`core/io/resource_format_binary.cpp:416, 435, 468, 493, 518, 832, 1069`; `scene/2d/tile_map.cpp:1277`).
  **BIG_ENDIAN_ENABLED is defined nowhere in the SCons tree** (grep of SConstruct, platform/x11/detect.py,
  core/SCsub), and Debian's godot3 patches (Backport SCons env option, Namespace metadata, Fix recast flags)
  do not add it. So on BE, PoolIntArray, PoolRealArray, Vector arrays and the like read from binary
  `.res/.scn` would be wrong. Fix: add `-DBIG_ENDIAN_ENABLED` for ppc64 BE in detect.py and test (S).
  Text `.tscn/.tres` and imported textures (.stex, raw bytes by format) are mostly fine; .stex of
  uncompressed formats and audio samples need checking. UNVERIFIED beyond the code read.
- Debian-ports ppc64 has 3.2.3 only, and it will not open projects exported by 3.4-3.6. Build 3.6.2 with
  ppc64 added to Architecture. Upstream: ppc64le is a known arch, ppc32 was dropped (PR 106390 note); BE
  ppc64 untested upstream.
- Mono/C# builds are out (Thrive etc.); GDScript games only. Debian's Mono 6.14 on ppc64 exists but the
  Godot 3 Mono glue wants a full Mono SDK and CoreCLR-era assemblies are not needed for 3.x; still skip.

### 6. Data and licence
Engine MIT. Games listed below carry their own licences. Exports are `.pck`; open-source games build from source
with the same Godot version.

### 7. Novelty
No Godot port to PS3 GameOS (Godot has no PS3 target). Godot games are novel there.

### Godot 3 open-source games (checked project.godot config_version=4 means Godot 3, 2026-10-19)
| Game | Engine | Licence | Notes |
|---|---|---|---|
| Super Tux Party (gitlab.com/SuperTuxParty/SuperTuxParty) | Godot 3 (config_version 4), v1.0-rc 2022, GDScript | GPL-3 | 3D party game, Mario Party style; default driver GLES3, can force GLES2; 3D with shadows: heaviest |
| Haldric (wesnoth/haldric) | Godot 3.1+, dormant since 2022-10 | no spdx listed (Wesnoth org; UNVERIFIED) | official Wesnoth reimplementation WIP; 2D hex tiles; unfinished |
| TuxBuilder (Alzter/TuxBuilder) | Godot 3.2+, 2023-06 | GPL-3 | SuperTux level-editor-ish clone, 2D |
| Flappy Race (Jibby-Games) | Godot 3, pushed 2026-04 | MIT | 2D online multiplayer |
| Everplast (WraithWinterly) | Godot 3, pushed 2026-05 | other/unspec | 2D platformer, 457 MB repo |
| Pocket Broomball (dulvui) | Godot 3.2+ | AGPL-3 | 2D mobile game |
| official godot-demo-projects `3.x` branch | Godot 3 | MIT | 2D/3D demos, good test cases |
Not Godot 3 any more: Tanks of Freedom II (now config 5, Godot 4.4), Librerama (Godot 4.6). Thrive is Mono:
out. Best bets for the PS3: the 2D ones (Flappy Race, TuxBuilder, Everplast) and Super Tux Party if a
few shadows are dropped. None is a household name, so popularity of the whole item rests on the engine.
Cheap PC test done: the Debian godot3-runner 3.6.2 ran a hand-made GLES2 project (above); no full game
was run.

### Sources
src/godot3 (tag 3.6.2-stable, read-only); awesome-godot-3 list; project.godot files fetched from GitHub,
GitLab, Codeberg; buildd.debian.org godot3 status; sources.debian.org patch list.

---

## SDL3 GPU API and raylib

SDL3 GPU API: out. `src/gpu/` in libsdl-org/SDL has only `d3d12`, `metal`, `vulkan` (and `xr`), no GL
backend (GitHub contents listing, 2026-10-19). A game written against `SDL_GPU*` cannot render on the RSX;
it would need a port to the SDL renderer or GL. Few notable open-source games use it yet: found jsoulier/blocks
(tiny Minecraft clone), vinisnthenorhim/SDL3-Project, FNA's SDL_GPU work (UNVERIFIED); acoto87/war1 and
quetoo are tagged sdl3 but GPU use is UNVERIFIED. Verdict: nothing notable lost; skip.

raylib 6.0 (git 2026-10-18): `src/rlgl.h:25-30, 152-158` supports `GRAPHICS_API_OPENGL_11 | 21 | 33 | 43 |
ES2 | ES3`, default 33; CMake `-DGRAPHICS=GRAPHICS_API_OPENGL_21` (or ES2) selects it, and 6.0 adds a
`GRAPHICS_API_OPENGL_SOFTWARE` backend. So raylib games are a rebuild against raylib built with
GRAPHICS_API_OPENGL_21 (Debian has libraylib-dev 6.0+ds-2 on ppc64, built for 3.3 presumably), not a port,
except games that ship their own `#version 330` shaders (rewrite to 120/100). Notable raylib games are
small: raysan5/raylib-games jam titles, Waste of Space (C++, ENet rollback coop), ccleste-raylib, Wolf3D
clone, Asteroids Reimagined. None is a headline game. Effort S per game, value low.
Raylib's big-endian status: UNVERIFIED (rlgl/stb use host-order texture data; stb_image handles files).
SCORES: popularity=2 novelty=4 playability_256MB=4 effort=S (per game)

---

## Confirm-and-skip

- **Veloren**: out. `voxygen/Cargo.toml:69` `wgpu = { version = "27", features = ["spirv", "glsl"] }`
  (Vulkan/Metal/DX12 plus an experimental GL3.3+ path); Rust, several GB RAM. Skip.
- **SuperTuxKart modern renderer**: skip, we use the legacy one. `src/graphics/central_settings.cpp:104`:
  `m_glsl` (the SP shader pipeline) needs GL >= 3.1 (non-GLES) or 3.0+ ES and `m_supports_sp`; below that
  the legacy fixed-function device is used (`:107`). What the legacy renderer loses (read in
  `central_settings.cpp` and settings, partly UNVERIFIED): the deferred pipeline `isDeferredEnabled` (dynamic
  lights, point lights from items), PBR materials and normal maps, cascaded shadows, SSAO/GI, bloom/glow
  post effects, DoF and motion blur, hardware skinning (kart skinning is on the CPU, as our docs say). It keeps
  the kart/track draw, fog, texture compression and simple blob shadows. Nothing important for gameplay.
- **Yamagi Quake II gl3/gles3**: skip. Debian ships `ref_gl1` (GL 1.4 fixed function, runs) and `ref_gl3`
  (GL 3.2 core); gles3 same class. docs/games/quake.md:46-49 already say gl1 runs. gl3 adds coloured lighting
  and MSAA only.

---

## Scan: other games that need GL 3+ (one line each)

| Game | GL need | Debian ppc64 | RAM | Verdict |
|---|---|---|---|---|
| Colobot (gold edition) | OpenGL 1.4 default engine; `-graphics gl21`/`gl33` devices exist (shadows need them) (colobot.info forum) | colobot 0.2.2-4+b2 | ~300 MB | GL2 already: runs on the 1.4 or gl21 device; worth a try |
| Oolite | GL 1.x/2.x, shader effects optional (UNVERIFIED) | not in sid | ~300 MB | GL2 already; needs a build, GNUstep |
| FreeOrion | GG engine; GL 2.1-class (UNVERIFIED version) | freeorion 0.5.1.2-1+b1 | 500 MB+ | probably GL2 already; heavy, Python AI |
| MegaGlest | GL 2.x, shaders optional | megaglest 3.13.0-10.1+b1 | 400 MB+ | GL2 already; heavy CPU RTS |
| Trigger Rally | PLIB / GL 1.x-2.x | trigger-rally 0.6.6.1-4+b1 | ~200 MB | GL2 already; cheap win |
| Luanti / ClassiCube / SuperTuxKart | GL 1.x / 2.1 | already run | | covered |
| Pioneer Space Sim | OpenGL core profile 3.1+ (Slackware READMEs) | not in sid | ~600 MB | GL3-blocked, no instancing/UBO fallback: not worth it |
| Rigs of Rods | Ogre 1.x GL3+ (GL 3.3, UNVERIFIED) | not in sid | 2 GB+ | out |
| Stunt Rally 3 | Ogre-Next, GL 3.3/Vulkan | not in sid | 2 GB+ | out |
| OpenSpades | GL renderer 2.0/3.0 class (wiki), also `softgl` software renderer, runs on llvmpipe with GL2 | not in sid | ~400 MB | likely GL2 already or softgl; novel; worth a look |
| Speed Dreams | forge PR #3 "Reduce minimum required OpenGL version from 3.3.0 to 2.1.0" (title only, UNVERIFIED merged) | not in sid | 1 GB | maybe GL2 after that change; check |
| sm64ex / Render96 | `RENDER_API=GL_LEGACY` (GL 1.3) or GL 2.1, big-endian save support in README; ROM required (non-free assets) | n/a | ~150 MB | GL2 already: strong candidate, N64 decomp is BE-friendly; needs the user's ROM |
| Ship of Harkinian / 2Ship2Harkinian | libultraship Fast3D, `gfx_opengl.cpp:306` `#version 130` on Linux (GLSL 1.20-class code), ES `#version 300 es` | n/a | 400 MB+ | near GL2: change to 120; C++20, ImGui, needs ROM, BE UNVERIFIED |
| Overgrowth | open-sourced engine, GL 3.2+ (UNVERIFIED), data not free | n/a | 2 GB+ | out |
| Cytopia | SDL2 renderer | n/a | | not a GL item, abandoned |
| Thrive, Space Station 14 | Godot Mono, .NET | no | | out |
| Barotrauma | see above | no | | out |

Best finds: sm64ex (GL_LEGACY already, BE-aware), Colobot and Trigger Rally (already GL 1.4/2 and in Debian
ppc64), OpenSpades (software or GL2), Ship of Harkinian (a GLSL version line away).

---

## Cleanup note
Traces deleted; census reports kept under `$B/work/godot/out/{census,c2}`, `$B/work/openmw/out/{census,census21}`;
containers `openmw` and `godot` removed.
