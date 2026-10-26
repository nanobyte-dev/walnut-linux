# Part B: 2D games and Java (Taisei, OpenClonk, Hypersomnia, DDNet, Mindustry)

All measurements: Debian sid amd64 in a podman box, Xvfb + llvmpipe (Mesa 26.2.5, GL 4.6 core), no audio
device. Taisei at 960x540, OpenClonk and Hypersomnia at 1280x720, Mindustry at 1280x720 (llvmpipe at
1080p ran at 7 fps for Taisei and starved the game logic, so the smaller windows were used; texture MB
do not depend on window size, framebuffer MB do). llvmpipe baseline ~100 MB is subtracted for PS3
estimates. Reports (small) are under /mnt/data_linux/Temp/ps3/gl3census/traces/{taisei,clonk,hyper,mindustry}/c*/report.md;
big traces and downloads were deleted.

Short ranking by effort to a working PS3 port: DDNet (no GL 3 at all, but not buildable in Debian ppc64 today),
Mindustry (GL 2.1 already works, killed by JVM memory), Hypersomnia (S shader port, killed by size and
BE), Taisei (instancing + HDR targets, M-L, memory poor), OpenClonk (XL-ish by memory, not by GL).

## Taisei (own C engine on SDL3, master 2026-10-15, Debian 1.4.4+dfsg-3)

Verdict: GL needs are moderate (instancing is the hard one) and the engine has no GLES2/GL2 backend any
more, so a port means a new backend or Mesa-side instancing; memory is the real problem (550 MB RSS in stage 1 at 540p). Builds on ppc64 and is
already endian-aware. Effort L; PS3 playability poor; Debian's package has no music or sfx.
FEATURES: glsl_min=330 core_only=y fallback=none int_ops=opt (1 shader: floatBitsToUint) bitwise=opt
  texelFetch=no textureSize=no ubo=no instancing=req tex_arrays=no float_tex=req (RGBA16F HDR background,
  RG16F lasers) float_rt=req rg_tex=opt (R8/RG/RGTC from basisu; falls back by caps) mrt=4 (used 2 in the
  stage trace) vao=req sync=no geom=no tf=no compute=no derivatives=opt (6 of 49 shaders)
  textureLod_fs=opt (3 of 49, shader macro maps it to texture() on legacy) vertexid=no flat=no
  arb_fbo=req (glBlitFramebuffer x859, FBOs scaled to window size) srgb_fb=no vtf=no
  dyn_loops=opt (3 shaders) big_shaders=? (largest generated fragment source 16.8 kB after includes; instruction count
  not measured, the stage and boss shaders are likely over 32 temps)
SCORES: popularity=3 novelty=5 playability_256MB=2 effort=L
  ppc64=builds (taisei 1.4.4+dfsg-3 in debian-ports) data=free (CC/MIT code; art free) size_MB=175 (00-taisei.zip; Debian build ships no bgm/sfx)

The detail (GL needs, the port, memory, CPU, big-endian, data, novelty and its sources) is in the game's bundle: [gamedb/games/taisei/investigations/2026-10-19-gl3-census.md](../games/taisei/investigations/2026-10-19-gl3-census.md).

---

## OpenClonk (own C++ engine, SDL2 + epoxy, master 2026-04-28 (SDL only), Debian 8.1-5)

Verdict: GL 3.2 core only (no fallback), but the renderer is a plain 2D/mesh batcher whose shaders are almost GLSL 1.20 as written, so the GL port is M. The blocker is memory (742 MB RSS in a 64x40 sandbox) and CPU for the landscape shader. Builds on ppc64. Playability on 256 MB: 1-2.
FEATURES: glsl_min=150 core_only=y fallback=none (a 3.1 "emergency context" with software transform, C4DrawGLCtx.cpp:469) int_ops=opt (f2i() of float, array index by int) bitwise=no texelFetch=no
  textureSize=no ubo=no instancing=no tex_arrays=req (sampler2DArray for landscape materials; 2 shaders) float_tex=no float_rt=no rg_tex=no (one GL_RED) mrt=1 vao=req sync=no geom=no tf=no compute=no
  derivatives=no textureLod_fs=no vertexid=no flat=no arb_fbo=no (no FBOs at all in the sandbox trace; light maps use FBO textures when dynamic light is on, UNVERIFIED) srgb_fb=no vtf=no
  dyn_loops=no big_shaders=yes (landscape fragment source ~12.7 kB; 110 shader variants; bones[] array indexed by int in the mesh VS, MeshVertexShader.glsl:93-127; 8 bone weights = 8 indirect uniform reads per vertex, nv30 VS limit)
SCORES: popularity=3 novelty=5 playability_256MB=1 effort=L
  ppc64=builds (openclonk 8.1-5+b1 in debian-ports) data=free (CC-BY) size_MB=~110 (openclonk-data: Music 29, Objects 32, Sound 20)

### 1. GL needs
- Context request: 3.2 core via SDL (C4WindowSDL.cpp:98-100, C4DrawGLCtx.cpp:29-30), falls back to 3.1 with ForceSoftwareTransform. All shaders get `#version 150` (C4Shader.h:36 `C4Shader_Version = 150`, C4Shader.cpp:562) and MAX_*_UNIFORM_COMPONENTS defines. planet/Graphics.ocg/*.glsl = 677 lines; scenario/object shaders are "slices" assembled at runtime.
- Master dropped GLX/Qt and is SDL-only; Debian's 8.1 also runs through SDL2 (here sdl2-compat over SDL3), with the Qt editor window when started as `openclonk file.ocs` without `--fullscreen`.
- Trace (Sandbox.ocs, 1 player, 2127 frames, 1280x720): 110 shaders (#version 150), 32 draws/frame median (max 219), 606 GL calls/frame, 138k glBindVertexArray, glDrawElementsBaseVertex x10240
  (ARB_draw_elements_base_vertex: present in nv30), textures: 830 GL_TEXTURE_2D RGBA, 1 GL_TEXTURE_2D_ARRAY (landscape materials, "27 slots at 512x512 (56 MB)"), 2049 GL_TEXTURE_1D RGBA8, 1 GL_RED. No FBO, no float, no instancing.
- Features seen in shaders (glcensus): non_square_matrix (mat3x2/mat4x3 uniforms, GLSL 1.20 has them), dynamic indexing (4 shaders), discard, sampler2DArray (2 shaders).

### 2. Port
- Shaders to GLSL 1.20: `in/out` -> attribute/varying, `texture()` -> texture2D, `fragColor` -> gl_FragColor: a macro prelude in C4Shader::Build (one place, C4Shader.cpp:552-600). Easy. Context: ask for a compat 2.1 context, keep VAO use (ARB_VAO exists on nv30).
- Landscape: sampler2DArray (no arrays on nv30). Emulate: pack the 27 layers (512x512) into one 2D atlas and compute the UV in the shader (needs per-pixel clamp; mipmap bleed at tile borders), or drop to the first N materials. Landscape shader (LandscapeShader.glsl) is 162 lines, uses int math (`f2i`, `iRow = iScaler / 8`) that float emulation handles. Fragment complexity unmeasured: it samples landscape, scaler, matmap, material and normal arrays several times (6-8 fetches), likely OK for 4096 instr but fillrate heavy at 1080p (estimate).
- Mesh (animated Clonks) VS: 8 bone matrices indexed by int; nv30 VS has one address register and Mesa wires no indirect constants (per BRIEF): becomes if/else trees (512-instr limit risk) -> skin on the SPEs via libspeq or CPU; or drop mesh skinning.
- 1D textures: map to 2D height-1 (driver can do it).
- Driver-level: draw_elements_base_vertex (have), VAO (have), tex arrays (could be a Mesa 3D-texture-as-array emulation: NIR lowering of array sampler to 3D with z = (layer+0.5)/N, plausible since the RSX samples 3D; UNVERIFIED). Game-side: GLSL version prelude.
- Effort L: GL itself is S-M, memory work (below) is the L.

### 3. Memory
- Measured (1280x720, Sandbox.ocs, no sound, 2 runs): peak RSS 701-704 MB, VmHWM 742 MB; 561 MB at 5 s (loading), 704 MB steady in play. Sound disabled by config, music not loaded.
  Texture upload estimate 269 MB peak (253 MB live at end); buffers 7 MB.
- PS3 estimate: 704 - 100 - 253 = ~350 MB game heap (script engine, object defs, landscape surfaces in system RAM, all object graphics as surfaces) + 253 MB textures (stay uncompressed RGBA, no S3TC use; DXT via NV30_FORCE_DXT patch 0038 would cut to ~70 MB). Does not fit 160 MB; even a 2 GB-class ceiling is not the issue, the 350 MB heap is. Hopeless without a big engine-side diet (keep sprites as compressed textures, load-on-demand).
- Cuts: smaller scenario (Tutorial/Sandbox maps are 64x40 chunks only, bigger maps cost more), no dynamic light, drop normal maps (half the 56 MB material array).

### 4. CPU
- Measured game thread 4.8-8.4 s over 60 s on llvmpipe (not a real number). Engine is single threaded: C4Script interpreter (~89k lines of script linked), pixel-exact landscape physics and material simulation (liquids, flints) in C++: this is the heavy part and branchy. 606 GL calls/frame with 32 draws is high-ish; immediate-style surface blits. Expect PPE-bound under 30 fps in busy scenes (estimate).

### 5. ppc64 big-endian
- In debian-ports ppc64 (8.1-5+b1). c4group is a gzip'd archive with LE-written headers; the engine has a big-endian path for compile (it built on powerpc historically; C4Group uses StdCompiler with fixed-width LE integers? UNVERIFIED, not read). Network protocol uses its own StdCompiler binary packers (UNVERIFIED for BE; but BE hosts could not play mixed games if not). The Debian package builds, runtime on BE untested.

### 6. Data and licence
- Code ISC, data CC-BY 3.0 (Credits.txt, licenses/). Free. openclonk-data package about 110 MB (Music 29, Objects 32, Sound 20, Missions 7.6).

### 7. Novelty
- No GameOS port found. Nothing like a Worms/Clonk game on PS3 homebrew. Novelty 5; popularity 3 (cult classic, small active community, ~Steam-less).

### Sources
- git tag v8.1 and master of github.com/openclonk/openclonk; /mnt/data_linux/Temp/ps3/gl3census/traces/clonk/cc3/report.md, c3.mem.txt.

---

## Hypersomnia (own C++ engine "augs", GLFW + GL 3.2 core, v3.0.0, git 2026-10-18)

Verdict: renderer is tiny and nearly GL 2 already (VAO + FBO + stencil, 26 shaders of 35 kB, GLSL 130): the GL port is S. What kills it is that it has no BE support, only x86/arm builds with vendored static libs, and ~440-490 MB RSS. Not in Debian. Effort L overall, playability 2.
FEATURES: glsl_min=130 core_only=y (3.2 core requested, window_sdl2.cpp:88-90) fallback=none (web build is WebGL2/GLES3, `#version 300 es`) int_ops=opt (`uniform int physical_lights`) bitwise=no texelFetch=no textureSize=opt (3 shaders)
  ubo=no instancing=no tex_arrays=no float_tex=no float_rt=no rg_tex=no mrt=1 vao=req sync=no geom=no tf=no compute=no derivatives=no textureLod_fs=no vertexid=no flat=no arb_fbo=opt (renderbuffer STENCIL_INDEX8 + 4 FBO colour attachments, no blit)
  srgb_fb=no vtf=no dyn_loops=no big_shaders=no (largest fragment 1.9 kB) pbo=opt (glBufferStorage persistent PBO only if PERSISTENT, pbo.cpp:62; GL 4.4)
SCORES: popularity=2 novelty=5 playability_256MB=2 effort=L
  ppc64=not-in-debian data=free (CC for content; source AGPL-3) size_MB=61 (AppImage extract; 30 MB AppImage + content)

### 1. GL needs
- Context: core 3.2 on desktop, ES 3.0 on web (window_sdl2.cpp:84-90). Shader prelude: `#version 130` (Linux/Windows), 150 (macOS), `300 es` (web) (graphics/shader.cpp:75-80). Shaders (hypersomnia/content/shaders/*.vsh/.fsh, 32 files) use `in/out`, `smooth`, `out vec4 outputColor`, `texture()`, precision qualifiers.
  Nothing else above GLSL 1.20 in them (the census lists only discard x9, textureSize x6 in 3 shaders). `textureSize` in 3 shaders (GLSL 1.30) is the only function needing care.
- API (graphics/*.cpp): glGenVertexArrays x1 (a single VAO), glBufferData per draw (95k calls in 1602 frames = 60 per frame), glDrawArrays, stencil ops, glBlendFuncSeparate, glReadPixels, FBO with texture colour + stencil renderbuffer.
- Trace (tutorial arena, running, 1280x720): 1602 frames, 26 unique shaders (#version 130 plus precision), 28 draws/frame, 338 GL calls/frame, textures 13 RGBA (peak 77.6 MB, uncompressed), buffers 0.8 MB.

### 2. Port
- Shaders: prelude 130 -> 120 with defines (`#define in varying` in the fragment, `attribute` in vertex, gl_FragColor, texture2D, drop `smooth`): trivial. `textureSize` x3 -> uniform with the size. `uniform int` is fine on nv30 (ints are floats).
- Context: request compat 2.1 or let the driver override; one VAO is fine (ARB_VAO). FBO stencil renderbuffer: EXT_packed_depth_stencil present, STENCIL_INDEX8 alone may not be renderable on nv30 (UNVERIFIED): use DEPTH24_STENCIL8. Light system relies on min/max blend equations (glBlendEquation MIN/MAX: nv30 has EXT_blend_minmax; UNVERIFIED in Mesa).
- Loops: none. Driver-level coverage: none needed except context-version override. Effort for the GL side: S (days). The rest: build for ppc64 with GCC (they require clang + libc++; the tree vendors freetype/streflop/imgui, enet-like netcode, yojimbo/netcode, libdatachannel) and BE (below): L.

### 3. Memory
- Measured (1280x720, tutorial arena, ~25 s): RSS 440 MB steady, VmHWM 488 MB, with llvmpipe ~100 MB and textures ~78 MB (all 13 textures resident: texture atlas + fonts + blank FBO targets) -> ~260-300 MB game heap at the time of measurement. Includes the entire 35 MB content folder baked into an atlas at start (bake_fresh_atlas), the FP-determinism startup test, a WebRTC/NAT stack with SCTP threads.
- PS3 estimate: 260-300 MB heap on a 160 MB budget: swaps. Atlas size could be cut, but the engine keeps full intercosm (ECS) per arena. Cuts: smaller arena, lower atlas resolution, no fog-of-war FBOs.

### 4. CPU
- Simulation: custom ECS and Box2D-derived physics, 60 Hz deterministic logic (FP consistency test at startup: 5000 passes, 6.5 ms, uses `streflop` soft-float so results match across CPUs). Soft-float math is slower than hardware; on the PPE soft-float ~ expensive. The AppImage run used 30 s of game-thread CPU in 50 s of llvmpipe-throttled run (not a usable number; the thread was partly busy). 338 GL calls per frame (glBufferData per draw) is high for the nv30 driver (batching would be needed, as with X-Moto).

### 5. ppc64 big-endian
- Not in Debian. Source builds only for Windows/Linux x64/macOS/web(asm.js) per BUILDING.md/CMakeLists (aarch64 handled at CMakeLists.txt:1872, nothing for ppc). augs serialization (src/augs/readwrite/byte_readwrite.h) writes raw object bytes; grep found no byte-swapping (grep for endian in readwrite/ misc/ returned nothing): files, replays and network packets (yojimbo) are LE by memory layout. Cross-play with x86 servers would break; solo/offline could work but saved settings/arena caches would be written BE. Needs a full serializer audit (L-XL). Also requires clang+libc++ (the release binary links libc++.so.1/libc++abi.so.1/libunwind).

### 6. Data and licence
- Source AGPL-3.0; content (gfx, sfx, arenas) mostly CC; 35 MB `hypersomnia/content`, 61 MB extracted AppImage; arenas downloaded on demand from hypersomnia.io. Free.

### 7. Novelty
- No PS3 port found; novelty 5. Popularity 2 (small but active community, on Steam since 2024).

### Sources
- github.com/TeamHypersomnia/Hypersomnia master; AppImage https://hypersomnia.io/builds/latest/Hypersomnia.AppImage (30 MB, extracted in a box, deleted); traces/hyper/ch3/report.md, h3.mem.txt.

---

## DDNet (own C++ engine on SDL2, master 2026-10-18; Debian sid 20.1.1+ds-1, ppc64 16.0.2-1+b2 old)

Verdict: NOT GL3-blocked: legacy OpenGL 1.x/2.x backend is the default on most platforms; the problem is that current Debian ppc64 builds do not exist. Effort S for the GL side (nothing), M for getting a current build running on BE.
FEATURES: glsl_min=none core_only=n fallback=gl1 int_ops=no bitwise=no texelFetch=no textureSize=no ubo=no instancing=no tex_arrays=opt (3.0+ backend uses 2D arrays, off by default) float_tex=no float_rt=no rg_tex=no
  mrt=1 vao=no (legacy backend) sync=no geom=no tf=no compute=no derivatives=no textureLod_fs=no vertexid=no flat=no arb_fbo=no srgb_fb=no vtf=no dyn_loops=no big_shaders=no
SCORES: popularity=4 novelty=5 playability_256MB=4 effort=M
  ppc64=fails (sid 20.1.1+ds-1 Build-Attempted on ppc64, "out-of-date", buildd debian-project-be-1; a 16.0.2-1+b2 binary exists in the ports Packages but needs libavcodec58/libswscale5/libglew2.2/libssl3 which sid no longer has: not installable) data=free (CC-BY-SA maps, GPL art) size_MB=~100 (ddnet-data)

### 1. GL needs (source)
- config_variables.h:766-775: `gfx_gl_major` defaults to 1 (minor 1) on x86-64 Linux and everything except macOS/IA32 builds (3 only on macOS ARM); backend_sdl.cpp:617-652 switches among GL 1.x (fixed function, glBegin-free vertex arrays), GL 2.x (`gl_major==2`: texture arrays absent, no shaders in the 1.x path) and 3.x/4.x (needs 3.3 and ARB_buffer_storage style features, backend_sdl.cpp:742-744). Backends: OpenGL, Vulkan, plus GLES/EGL (backend_egl.cpp), selectable with `gfx_backend`. So a GL 1.1/2.1 path is a first-class citizen, tuned for Intel GMA era hardware.
- Fits nv30 as is: textures NPOT/2D only. Not measured (no run, per brief).

### 2. Port
- Nothing to port for GL. Work is packaging: DDNet 20.x needs Rust (cargo) for its config/engine parts (BD-Uninstallable on m68k "missing cargo": build dep), ffmpeg, curl, SDL2, sqlite3; the ppc64 build attempt on sid is reported as Build-Attempted for 5+ days with the test log at the end. Old failure noted by the user: test `Net.Ipv4AndIpv6Work` hang under the buildd (no IPv6 localhost on the BE builder). We can build it locally with that test disabled (S).
- 16.0.2-1+b2 ppc64 .deb: only runs on an older (bookworm-era) library set; use only as a hint that the code runs on BE.

### 3. Memory
- Estimate only (no run): the engine is light (<150 MB RSS typically on PC with legacy backend, textures few: map tiles and skins, 20-60 MB). Playability 4.

### 4. CPU
- Server-style tick (50 Hz), simple physics; client renders tile maps with a few hundred draws; legacy GL1 path uses client-side vertex arrays, good for nv30. Fine for 60 fps on the PPE (estimate; Teeworlds, its parent, already runs per docs/games).

### 5. ppc64 big-endian
- The 0.7-era Teeworlds code is already patched in our tree for BE (docs/games). DDNet has swap helpers (src/base/bytes.h: swap_endian, uint_to_bytes_be) and a CONF_ARCH_STRING "ppc" in detect.h:155; packets and map files are LE with explicit swapping (same lineage as Teeworlds; our patch notes cover map/demo BE fixes). The 16.0.2 build in the ports archive proves it compiled and linked on ppc64.

### 6. Data and licence
- Code zlib-like, assets CC-BY-SA/CC0 mixed; ddnet-data ~100 MB (maps downloaded on demand from servers). Free.

### 7. Novelty
- No GameOS port found. We already have Teeworlds, so novelty stays 5 since nothing of this family exists on GameOS. Popularity 4 (tens of thousands of daily players).

### Sources
- github.com/ddnet/ddnet master (src/engine/shared/config_variables.h, src/engine/client/backend_sdl.cpp); buildd.debian.org/status/package.php?p=ddnet&suite=sid; dl/ports/unstable-main-ppc64 (ddnet 16.0.2-1+b2 Depends).

---

## Mindustry (Anuken, Java, Arc framework with SDL2 JNI backend, release v160.7)

Verdict: not GL3-blocked: runs on a GL 2.1 context (measured, menu) with GLSL 1.10-style shaders and uses GL 3 only when available. The blockers are the JVM on ppc64 BE, Arc's prebuilt natives (x86/arm only) and memory (663-704 MB RSS at menu with -Xmx256m). Effort XL, playability 1.
FEATURES: glsl_min=100 core_only=n fallback=gl2 int_ops=no bitwise=no texelFetch=no textureSize=no ubo=no instancing=no tex_arrays=no float_tex=no float_rt=no rg_tex=no mrt=1 vao=opt (GL3 path only) sync=no geom=no tf=no compute=no
  derivatives=opt (atmosphere.frag, shockwave.frag use dFdx/loops: planet and shockwave effects) textureLod_fs=no vertexid=no flat=no arb_fbo=req (EXT_framebuffer_object accepted, SdlGraphics.supportsFBO) srgb_fb=no vtf=no dyn_loops=opt big_shaders=no
SCORES: popularity=5 novelty=5 playability_256MB=1 effort=XL
  ppc64=n/a (needs openjdk: openjdk-21/25 ppc64 packages exist, hotspot JIT UNVERIFIED) data=free (GPL-3 code and assets in jar) size_MB=89 (Mindustry.jar)

### 1. GL needs
- Arc SdlGraphics.java:40-60: requires `OpenGL 2.0 or higher with the FBO extension`; uses GL30 only `if(glVersion.atLeast(3,0) && allowGl30)`. Shader preprocess (Shader.java:128-190): sources are GLSL 1.00-style (`varying`, `attribute`, `texture2D`, `gl_FragColor`, precision qualifiers stripped on desktop); for GL3 contexts Arc rewrites them to `#version 150`. Context attempt list: 4.6 down to 3.1 then 2.x with a compatibility profile (SdlApplication.java:150-170). Mindustry logs "[GL] Using OpenGL 2 API" with a performance warning (ClientLauncher.java:61-69).
- Measured: on Mesa llvmpipe with GL 4.6 core -> "Using OpenGL 3 API"; with MESA_GL_VERSION_OVERRIDE=2.1 + GLSL 1.20 -> "Using OpenGL 2 API", menu renders correctly (screenshot checked). 33 shaders in core/assets/shaders; only atmosphere and shockwave contain dFdx/loops. Store requirement: OpenGL 2.0 + FBO.
- Not traced (Java under apitrace not attempted).

### 2. Port
- GL: none needed beyond what nv30 has (GL 2.1, GLSL 1.20, FBO). Potential issues: NPOT/large (16384) textures, RGBA8 FBOs only, the planet shader dFdx (Mesa nv30 derivative cap off per BRIEF -> disable planet view), loops in shockwave (constant loops unroll).
- Platform: the real work. (a) JVM: Debian ports ppc64 has openjdk-8/11/17/21/25/26/27/28 JRE-headless plus separate openjdk-21-jre-zero/openjdk-25-jre-zero packages, which suggests the main packages carry the JIT (hotspot C1/C2 on ppc64 BE is upstream-supported; UNVERIFIED for these builds, check the build log/`java -XX:+PrintFlagsFinal` on the PS3). A non-JIT Zero VM would make a 60 fps game impossible on the PPE.
  (b) Arc natives: arc-core loads prebuilt `.so` from the jar (SDL2 JNI, freetype, openal, GL loader; natives/ directory of the Arc repo: natives-desktop, natives-freetype-desktop, natives-filedialogs) built for x86_64/arm64 only: must be rebuilt for ppc64 (JNI C, small, but each library's build system) or repointed to Debian's libSDL2/freetype/openal. (c) Mindustry.jar's bundled SDL is 2.0.10 (measured log line) and drives GLX itself.
  (d) The Gradle build itself and ~130 dependency jars (ArcLib) are pure Java: fine.
- Effort XL: JVM validation, native libs, memory diet.

### 3. Memory
- Measured (java 21 Debian, -Xmx256m, 1280x720, main menu with the animated background, 45 s): peak RSS 704 MB (GL 3 API) and 663 MB (GL 2.1 override), VmHWM same. Breakdown unknown: ~100 MB llvmpipe, 256 MB heap cap, ~150-250 MB JVM native (metaspace, code cache, GC structures, mapped jar 89 MB, 17 GC threads). A custom game would raise heap use; not run.
- PS3 estimate: JVM + 256 MB heap alone exceeds the 217 MB available. A 96 MB heap with SerialGC and tiny code cache is the only conceivable setting (UNVERIFIED that the game loads with it); far beyond 160 MB. Hopeless on 256 MB.

### 4. CPU
- Simulation of thousands of buildings and units, Java JIT warm-up (C2 compile 3.6 s CPU in the first 45 s even at the menu), GC threads. Mindustry is demanding on PCs with 2 cores; an in-order PPE under HotSpot ppc64 will be well below 30 fps in a real base (estimate).

### 5. ppc64 big-endian
- Java is endian-neutral except Arc's natives, ByteBuffers that assume native/LE order (Mindustry saves use DataOutput big-endian: fine; Arc's `ByteBuffer` use for vertex data uses native order and GL needs native: OK on BE) and image decoders (stb in natives; UNVERIFIED). Mod JARs with native code do not apply.

### 6. Data and licence
- GPL-3.0; assets in assets.jar / Mindustry.jar 89 MB (sprites, maps, music). Free.

### 7. Novelty
- No PS3 port known (UNVERIFIED search). Popularity 5 (millions of Steam owners). Novelty 5.

### Sources
- github.com/Anuken/Arc (SdlGraphics.java, Shader.java, SdlApplication.java), github.com/Anuken/Mindustry (ClientLauncher.java, core/assets/shaders), release v160.7 Mindustry.jar (downloaded to its own dir, deleted); traces/mindustry/m1.mem.txt, m2.mem.txt; dl/ports/unstable-main-ppc64 (openjdk packages).
