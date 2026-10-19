# Part C: RTS games (Warzone 2100, OpenRA, Beyond All Reason / Recoil, 0 A.D.)

Measured 2026-10-19 on the PC (Debian sid box `wz`, Xvfb 1920x1080, llvmpipe, Debian package 4.7.0-3).
Census report kept at /mnt/data_linux/Temp/ps3/gl3census/traces/wz/c47/report.md, mem files in work/wz/.

## Warzone 2100 (own engine, SDL2 + GLAD, QuickJS; master of 2026-10-17 = 4.7.x, Debian sid 4.7.0-3)

Verdict: the current line (4.4 to 4.7) needs GL 3.0/3.1+ and is not worth porting, but **release 4.3.5 (2023-04-16) is the last one that runs on plain GL 2.1 or GLES 2.0** with GLSL 1.20 shaders, no instancing, no UBOs, no texture arrays. So the route is "build 4.3.x, change nothing in the renderer, maybe cut textures". Effort S to M, RAM is the open question (estimate 300-450 MB, so it needs cuts to fit; not 256 MB friendly), novelty 5. ppc64: Debian ports ships 4.5.5deb-1+b1 on ppc64 (so the game compiles and links big-endian); 4.5.5 itself needs GL 3.0 though.

FEATURES (4.7.x / master): glsl_min=330 core_only=y fallback=none int_ops=req bitwise=? texelFetch=? textureSize=? ubo=req instancing=req tex_arrays=req(HQ terrain, optional in classic) float_tex=opt rg_tex=req(R8/RG8) float_rt=opt mrt=1 vao=req sync=opt geom=no tf=no compute=no derivatives=? textureLod_fs=? vertexid=? flat=req arb_fbo=req(blit/MSAA resolve) srgb_fb=no vtf=no dyn_loops=opt big_shaders=opt(HQ terrain, SSAO, SSR, SMAA; 17 KB fragment source in the trace)
FEATURES (4.3.5, the port target): glsl_min=120 core_only=n fallback=gl2 (and gles2: OpenGLES20 context request in lib/sdl/gfx_api_gl_sdl.cpp, case OpenGLES20) int_ops=no instancing=no ubo=no tex_arrays=no rg_tex=opt (LUMINANCE fallback in the source) float_tex=no mrt=1 vao=opt (only if GL 3.0) sync=no geom=no tf=no derivatives=no dyn_loops=no big_shaders=no (18 shader files, 1026 lines total) srgb_fb=no vtf=no
SCORES: popularity=4 novelty=5 playability_256MB=2 effort=S (4.3.5) | L (4.7 renderer to GL 2.1; not recommended) ppc64=builds (4.5.5 binary on debian-ports; 4.7.0 UNVERIFIED, buildd log not read) data=free size_MB=~150 for 4.3.5 data (tarball data dir is 33 MB without the texpages and music; 4.7 Debian data is 292 MB installed; estimate for 4.3.5)

### 1. GL needs (source + trace)
- Minimum check, master: `lib/ivis_opengl/gfx_api_gl.cpp:3931` refuses anything below GL 3.1 / GLES 3.0 ("OpenGL 3.1+ / OpenGL ES 3.0+ not supported"). Debian 4.7.0 sources have the 3.0 check at line 3952. Context requests in `lib/sdl/gfx_api_gl_sdl.cpp:108-136`: core 3.x/4.x or ES 3.0.
- History of the minimum (I fetched gfx_api_gl.cpp per tag): 4.2.7 and 4.3.5 check "OpenGL 2.1+ / OpenGL ES 2.0+" (4.3.5 line 2325); 4.4.2 onward check 3.0 (line 3514), 4.5.5 and later warn below 3.1. 4.3.5 even has an `OpenGL21Compat` context request (gfx_api_gl_sdl.cpp in the 4.3.5 tarball).
- GL 3 features in master, file `lib/ivis_opengl/gfx_api_gl.cpp`: UBOs 1589-1874 (glBindBufferRange, glUniformBlockBinding at 1769, persistent mapped UBO ring with glFenceSync at 4362), instancing glVertexAttribDivisor 2557 and glDrawElementsInstanced/glDrawArraysInstanced 2834/2852, 2D texture arrays 814, 2414, 4946, R8/RG8 (157-177), glVertexAttribIPointer 2543, VAO 4046, MSAA renderbuffer + glBlitFramebuffer 2439/4559, glDrawBuffers 4333, glTexBuffer RGBA32F for point-light data 5288, time-elapsed queries 3068, optional tessellation (terrain_*tess* shaders, 2104).
- Trace of 4.7.0, lowest settings (classic terrain, no shadows, textureSize 128, weather off, 1080p window, skirmish Sk-HighGround, about 45 s incl. menu): context GL 3.3 core forward-compatible; 24 unique shaders all `#version 330 core`; features: dynamic indexing (2 shaders), sampler2DArray (2), loops (2), flat (2), uint (2), samplerShadow (2, the shadow map shader exists even with shadows off), int modulo (1), GL_EXT_gpu_shader4. 18739 glDrawElementsInstanced + 686 glDrawArraysInstanced, 1376 glVertexAttribIPointer, 21 array textures, 95 R8 uploads, max draw buffers 1, no geometry/compute/TF. No UBO calls appeared in this trace (the Debian build apparently took the non-UBO path or the light data went elsewhere: UNVERIFIED).
- Draw cost (measured, trace): 3089 frames, 167 draws/frame median (196 max), 3390 GL calls/frame median (heavy state churn: glTexParameteri 1.58 M, glBindTexture 1.07 M, glUniform2f 0.8 M in 45 s).
- Shader folder in master: data/base/shaders has classic, medium and high terrain/water variants, plus SMAA, SSAO, SSR, FSR1; 4.3.5 has only button, decals, tcmask, terrain, water, skybox, text, rect, gfx.

### 2. Port to GL 2.1/GLES 2 on nv30
- Route A (recommended): use the 4.3.x line. It already targets GL 2.1 and GLES 2 with `#version 120` shaders (4.3.5 gfx_api_gl.cpp lines 452-484 list VERSION_120 and up), and its shader sources have no instancing, no arrays, no texture lookup tricks, no loops. Expect 1:1 on nv30: the fragment programs are small. Work items: build with ENABLE_GNS off (not present in 4.3.5), check 4.3.5 on Mesa nv30 (R8 uses a LUMINANCE fallback, NPOT fine, S3TC fine), batch or cache the glTexParameteri/glBindTexture churn (the same pattern costs a lot of PPE time), lower texture size. Multiplayer from 4.3.5 does not interoperate with 4.4+ hosts (fine for skirmish).
- Route B (4.7 renderer to GL 2.1): drop instancing (replace with a driver loop or per-draw uniforms: 18.7 K instanced draws per 45 s means a 10x draw count on the PPE), drop UBOs (to plain uniforms), drop texture arrays (only classic terrain = no arrays), R8 to LUMINANCE, remove integer attribs. A driver-level override in Mesa could emulate UBO->uniforms and instancing->loop, not the core-profile context or GLSL 330 `in/out`. Effort L, and the result is worse than just running 4.3.5. Not recommended.
- Look: 4.3.5 "medium" terrain (the classic-era look: lightmap + decals, no normal maps). Drop: HQ terrain, SSAO/SSR/SMAA/FSR, tessellation, shadows (shadow mapping exists in 4.3.5 as an option, skip).

### 3. Memory
- Measured, 4.7.0 Debian amd64, 1080p window, llvmpipe, `--skirmish=hg2.json` (custom copy of tests/highground.json: bases 2, no scavengers) started from the lobby by xdotool clicks (the `--autogame` mode ended the game after 8-55 s because player 0 gets converted to a spectator, so it is useless for steady runs):
  - lowest settings (terrainMode=0 classic, textureSize=128, no shadows, no weather): VmHWM **396 MB** (menu 193 MB, in game 391-396 MB, flat over 95 s). apitrace counts texture upload 199.9 MB peak, 84.7 MB live at the end (uncompressed estimate), buffers 6.9 MB.
  - default settings (HQ terrain mode 2, textureSize 2048, autogame run): 577 MB.
- PS3 estimate (formula of the brief): 396 - 100 (baseline) - 85 (live textures) - 7 + 10 + 10 = about 225 MB for 4.7. That counts DXT-compressed textures at 85 MB live in VRAM. The game data itself is zipped in .wz archives read through physfs (no big unpacked copy, but the map, stats DB and JS state are heap). For 4.3.5 (smaller data, no HQ texture sets, no basis transcoder buffers) I expect 180-250 MB of non-texture RAM. Both are over the 160 MB no-swap line but under the 1 GB stretch bar. Estimate, not measured on 4.3.5 (I did not build it).
- Biggest consumers: the textures (page of 2048 tiles; 4.7 loads basis/KTX2 and transcodes at load), the stats and model data (PIE meshes), QuickJS heaps for the AIs, map terrain buffers. Cuts: textureSize 128 or 256 (config key), no HQ terrain overrides (97 MB of terrain_overrides in the Debian data), skip music (110 MB on disk, streamed).

### 4. CPU
- Measured, PC: the game thread took 20.9 CPU s over about 130 s (lobby plus about 95 s of play, one AI opponent); llvmpipe took 835 s (the rasterizer, irrelevant on the RSX). Worker threads wzPath (A* pathfinding) and wzDanger (AI danger map) exist but used 0.0 s in this short, quiet game.
- Estimate for the PPE: one thread at 20-25 % of a modern x86 core in a quiet 1v1 opening becomes about 1.2-2 PPE cores-worth after the in-order penalty. Later in a game with 100+ units and 3-7 AIs it will not hold 30 fps. Add the nv30 driver cost on 167 draws and about 3400 GL calls per frame (30-55 % of the PPE per the brief). Simulation runs at 10 ticks per second with a fixed timestep, and the logic can be capped while rendering slows. Verdict: playable skirmish 1v1 with one easy AI at 20-30 fps is plausible, big maps and 8-player games are not.
- JS: QuickJS (3rdparty/quickjs-wz), pure C, interpreter only, no JIT: runs on BE (UNVERIFIED on ppc64, plain C so expected fine). SemperFi is light; Nexus and Cobra/BoneCrusher AIs are heavier.

### 5. ppc64 big-endian
- Debian ports has `warzone2100 4.5.5deb-1+b1` in unstable-main-ppc64 (installed size 20 MB) and the sid source 4.7.0-3 is `Architecture: any all`, so it is not arch-excluded. I did not read the buildd logs for 4.7.0 on ppc64.
- 4.7.x: the bundled GameNetworkingSockets carries a big-endian static assert (per docs/research/GAME-CANDIDATES.md; submodule not cloned here so I could not read the line). It is gated by CMake option `ENABLE_GNS_NETWORK_BACKEND` (3rdparty/CMakeLists.txt:305, default ON): configure with it OFF and the assert is gone (skirmish does not need it; the TCP backend remains). 4.3.5 has no GNS at all.
- Data formats: binary game files are read with `PHYSFS_readULE16/32` (`lib/framework/physfs_ext.h:181`, 135 call sites in lib and src in 4.3.5), which is endian-explicit; PIE models are text; maps use the same readers and `lib/wzmaplib` has an SDL_endian shim. Ogg/Vorbis and sequence code have BYTE_ORDER checks. So BE should be clean; the open risk is texture/basis code (basis_universal in 3rdparty is endian-aware but I did not test it) and any raw struct dumps in savegames (savegames are not needed for a first run).

### 6. Data and licence
- Code GPL-2+, data mostly CC-BY-SA / GPL. Free. Debian `warzone2100-data` 4.7.0: 292 MB installed (base.wz 118 MB, mp.wz 10 MB, music 110 MB, fonts 33 MB, mods 20 MB, terrain_overrides 97 MB); no campaign videos (the game shows a "Campaign videos are missing" banner). For 4.3.5: estimate 150 MB total, 33 MB of it in the source tree, rest textures and music.

### 7. Novelty
- Web search found no PS3 homebrew port or GameOS release of Warzone 2100 (only the 1999 original PlayStation game, which is a different title). Novelty 5. Popularity 4 (long-lived, Debian/snap/Steam-less, active releases).

### Sources
- build/tmp/gl3census src: /mnt/data_linux/Temp/ps3/gl3census/src/wz (master 2026-10-17), tag tarballs 4.2.7/4.3.5/4.4.2/4.5.5/4.6.0/4.7.0 of lib/ivis_opengl/gfx_api_gl.cpp from raw.githubusercontent.com/Warzone2100/warzone2100.
- debian-ports `unstable-main-ppc64`, `sid-Sources`; measurements in work/wz/low.mem.txt, a1.mem.txt, traces/wz/c47/report.md.

---

## OpenRA (own C# engine on .NET, SDL2 + OpenAL, master of 2026-10-11, net10.0)

Verdict: out. The renderer needs GL 3.2 core or GLES 3.0 (the GL 2.1 "Legacy" profile was removed after release-20231010) and, worse, the runtime does not exist: current OpenRA needs .NET 10 and .NET has no big-endian PowerPC port. Mono 6.14 exists in debian-ports ppc64 but cannot run net10 assemblies. Only an old Mono-era build (2021-2022, GL 2.1 Legacy profile) would run in theory. Effort XL, novelty 5, RAM unlikely.

FEATURES: glsl_min=330 (GLSL 3.30 core or 3.00 es) core_only=y fallback=gles3-only (Legacy GL 2.1 profile exists up to release-20231010, gone in 20250330) int_ops=req bitwise=req texelFetch=no textureSize=req ubo=no instancing=no tex_arrays=no float_tex=opt (RGBA16F array texture, Texture.cs:135) float_rt=no rg_tex=no mrt=1 vao=req (Sdl2GraphicsContext.cs:46) sync=no geom=no tf=no compute=no derivatives=opt (dFdx/dFdy for pixel-art scaling, combined.frag:166) textureLod_fs=no vertexid=no flat=req arb_fbo=req (renderbuffer depth + FBO, simple) srgb_fb=no vtf=no dyn_loops=no big_shaders=no (12 shaders, 481 lines total)
SCORES: popularity=4 novelty=5 playability_256MB=1 effort=XL ppc64=n/a (needs .NET 10; Mono 6.14.1 is in ppc64 but cannot run it) data=free (C&C, RA, D2K content downloaded from EA/freeware sources by the game; sizes UNVERIFIED, expect 100-500 MB) size_MB=~300 (estimate)

### 1. GL needs
- `OpenRA.Platforms.Default/Sdl2PlatformWindow.cs:149-164` tries profiles ANGLE, Modern, Embedded; `535-558`: Modern = GL 3.2 core, Embedded/ANGLE = ES 3.0. In release-20230225 (and 20210321, 20231010) a fourth `GLProfile.Legacy` (GL 2.1) was added when the env/config allowed it (lines 152, 536 of that tag); the 20250330 tag and master have none.
- Shaders (glsl/*, 481 lines): `#version {VERSION}`, `in/out`, flat varyings of type uint (`flat in uint vChannelSampler` combined.frag:28), `glVertexAttribIPointer` for the packed `aVertexAttributes` uint (Shader.cs:142), shifts and masks in combined.vert:56-61, `switch` on uint, `textureSize` over 8 sampler units (combined.frag:78-95), dFdx/dFdy (166). In the Legacy profile era the same file had `#version 120` variants (UNVERIFIED, did not diff).
- Not measured on the PC (no .NET 10 in the box, and content download needed). The renderer is a 2D sprite batcher: expect a few hundred draws per frame, one big combined sprite shader, palette and 8 sheet textures. Estimate.

### 2. Port to GL 2.1/GLES 2
- Renderer alone: M. Replace uint attributes with floats unpacked in the shader (bitwise to float arithmetic: mod/floor), `switch` to if chains (nv30 unrolls), textureSize to a uniform, flat to plain varying (drop flat), VAO optional, remove RGBA16F (use RGBA8, only the shadow/depth parts). The Legacy profile in 20231010 is the proof it was once done upstream. A Mesa-level "GL 3 subset" would cover integer attribs (as floats), flat, textureSize (nearest lookup with size uniform), but not the 330 version string and `in/out` syntax without a GLSL-version emulation; not worth it.
- The renderer is not the blocker. The blocker is the runtime (section 5).

### 3. Memory
- Not measured. .NET desktop apps start at 80-150 MB RSS before game data; OpenRA with the RA mod loaded typically sits at 400-800 MB (UNVERIFIED, estimate). With Mono add 50-100 MB. Over the 1 GB stretch bar when textures and the sheet atlases (RGBA 2048 sheets x several) are counted; not a fit.

### 4. CPU
- Lockstep simulation, 25-40 ticks per second, a few thousand actors with C# traits and per-tick pathfinding (hierarchical A* in `OpenRA.Mods.Common/Pathfinder`). On a JIT-less or slow Mono path on the PPE it runs far below 25 ticks/s on any real match. UNVERIFIED, estimate. The sim must be deterministic across clients (matters only for multiplayer).

### 5. ppc64 big-endian
- OpenRA master targets `net10.0` (Directory.Build.props:21; INSTALL.md:11 lists the .NET 10 SDK). CoreCLR has no ppc64 BE port (ppc64le is a community port, s390x BE exists but is a different architecture). Debian sid has `mono 6.14.1+ds2-3` with ppc64 in its architecture list and `mono-runtime` in unstable-main-ppc64, but Mono 6 cannot run net10 code (new BCL APIs, Span/ref-struct features). The last releases that built for Mono were the 2021-2022 ones; release-20230225 and later are .NET 6+ (UNVERIFIED: I did not check which exact release dropped Mono).
- Binary formats: .NET `BinaryReader` and `BinaryPrimitives` are little-endian by definition, so game file readers are fine on a BE host (OpenRA.Game FileFormats use explicit BigEndian calls only where the format is BE, e.g. PNG chunk parsing). Mono on BE Mono fixes aside, this is not the problem.

### 6. Data and licence
- Code GPL-3. C&C (TD), Red Alert and Dune 2000 content is freeware released by EA; OpenRA downloads it at first start (needs network, mix files). The Dune 2000 content needs a purchased/original copy for some assets. Sizes UNVERIFIED.

### 7. Novelty
- Web search found no PS3 port of OpenRA. Novelty 5. Popularity 4.

### Sources
- /mnt/data_linux/Temp/ps3/gl3census/src/openra (master 2026-10-11); tags release-20210321, release-20230225, release-20231010, release-20250330 of Sdl2PlatformWindow.cs from raw.githubusercontent.com/OpenRA/OpenRA; debian-ports `unstable-main-ppc64` (mono-runtime 6.14.1+ds2-3), `sid-Sources` (mono arch list).

---

## Beyond All Reason / Recoil engine (Spring fork; Zero-K runs on the same engine family)

Verdict: out. Recoil asks for a GL 3.0 context minimum and loads GLSL up to `#version 430` plus GL4 widgets; the typical game uses 2-4 GB RAM. No measurement made.

FEATURES: glsl_min=130 core_only=n (compat/core both tried, glCtxs list 2.0 to 4.6 at GlobalRendering.cpp:466; config default GL 3.0, minimum 3.0, CONFIG at :50-51) fallback=none int_ops=req bitwise=req texelFetch=req ubo=req(GL4 widgets, `#version 420/430` shaders) instancing=req tex_arrays=req float_tex=req float_rt=req rg_tex=req mrt=4+ vao=req sync=? geom=opt tf=opt compute=opt (`#version 430` shaders in cont/) derivatives=req textureLod_fs=req vertexid=req flat=req arb_fbo=req srgb_fb=? vtf=req (heightmaps in the vertex shader) dyn_loops=req big_shaders=req
SCORES: popularity=5 novelty=5 playability_256MB=1 effort=XL ppc64=not-in-debian (not packaged for ppc64 as far as I know; UNVERIFIED) data=free size_MB=~3000 (maps and base content downloaded by the launcher; estimate)

### 1. GL needs
- `rts/Rendering/GlobalRendering.cpp:50-51`: GLContextMajorVersion default 3 (min 3, max 4), minor min 0. Context probing at 466-535 walks 2.0 to 4.6. `GlobalRendering.cpp:1815` uses GL 4.0 sample shading when available; `myGL.cpp:330` uses GL 4.5 paths; `FBO.cpp:447` and `LuaFBOs.cpp:53,366` need GL 3.2 for layered FBOs.
- Shaders in `cont/` by version (counted): 2x `#version 120`, 34x 130, 9x 150, 1x 400, 2x 420, 4x 430. Many gadgets and widgets are named `*_gl4.lua` (e.g. cont/examples/Widgets/gui_buildsquare_gl4.lua, `ShadowGenVertProgGL4.glsl`). BAR's own widgets ship GL4 shaders (instanced, SSBO-style data). A GL 2.1 fallback does not exist.

### 2-4. Port, memory, CPU
- No port: the GL4 widget layer (instance VBOs, UBO/SSBO, compute) is the engine's modern path and most of BAR's visuals sit on it. Lua-heavy gadget/widget stack (LuaJIT or Lua 5.1 in Recoil, which we already patch for BE on the PS3, but not at this scale), unit counts in the thousands, RAM 2-4 GB (UNVERIFIED), single sim thread dominating. Out on every axis.

### 5-7. ppc64, data, novelty
- Recoil is C++, GPL-2; big-endian status unknown; no Debian ppc64 package (Spring itself was in Debian; the Recoil fork is not). BAR content: game content is GPL/CC, maps free; initial download is gigabytes. Zero-K (same engine family) has the same engine requirements. No PS3 port found. Novelty 5, popularity 5.

### Sources
- /mnt/data_linux/Temp/ps3/gl3census/src/recoil (RecoilEngine master 2026-10-19).

---

## 0 A.D. (Pyrogenesis engine, A27 "Agni" / A28)

Verdict: out for RAM (2 GB+ typical) and no ppc64 build (debian-ports arch list amd64 arm64 i386 ppc64el for 0ad 0.28.0-3, confirmed in sid-Sources). GL is not the problem: the GL backend still runs on GL 2.1 with ARB or GLSL shaders.

FEATURES: glsl_min=110/120 core_only=n fallback=gl2 (ARB assembly shaders as fallback) int_ops=no bitwise=no ubo=no instancing=opt (GL_ARB_draw_instanced + ARB_instanced_arrays, used for models when present) tex_arrays=? float_tex=opt rg_tex=? mrt=opt (ARB_draw_buffers) vao=opt sync=no geom=no tf=no compute=opt (GL 4.3 or ARB_compute_shader only, for GPU skinning/post effects, optional) derivatives=? textureLod_fs=? vertexid=no arb_fbo=opt (EXT or ARB FBO) srgb_fb=? vtf=opt (GPU skinning via texture or compute, optional) dyn_loops=? big_shaders=opt (water, terrain with many layers; UNVERIFIED)
SCORES: popularity=5 novelty=5 playability_256MB=1 effort=XL (RAM, CPU, no ppc64) ppc64=not-in-debian data=free (CC-BY-SA/GPL) size_MB=~2500

### 1. GL needs
- Source (0ad master `source/renderer/backend/gl/Device.cpp`): line 309-313 requires ARB_vertex_program + ARB_fragment_program (the ARB path) or GLSL (`GL_ARB_shading_language_100`, vertex and fragment shader, 609-629), ARB_vertex_buffer_object, multitexture, and EXT or ARB framebuffer objects. Line 362 `capabilities.ARBShaders`, 365 compute shaders only on GL 4.3 or 4.2 + extensions, 448-449 instancing via ARB_draw_instanced + ARB_instanced_arrays, 373 S3TC, 381 multisample, 635 draw_buffers. So the GL 2.x path remains; Alpha 27 added a Vulkan backend as an alternative, not a replacement (Phoronix/play0ad release notes).
- Not measured (judged out for RAM before this census; docs say 2+ GB).

### 5-7. ppc64, data, novelty
- sid-Sources: `0ad 0.28.0-3`, Architecture `amd64 arm64 i386 ppc64el`. Big-endian code issues unknown (SpiderMonkey JS engine is a big dependency; Firefox's SM has a BE port for s390x/ppc64 but the packaged one is not built for BE ppc64).
- Data: free (CC-BY-SA / GPL), about 2-3 GB installed. No PS3 port found. Novelty 5.

### Sources
- 0ad master Device.cpp (raw.githubusercontent.com/0ad/0ad/master/source/renderer/backend/gl/Device.cpp); play0ad.com Alpha 27 release posts; `sid-Sources`.
