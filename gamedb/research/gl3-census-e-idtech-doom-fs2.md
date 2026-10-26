# Part E: id Tech 4, the Doom source ports, FreeSpace 2 Open

The agent for this part stopped at the usage limit after its measurements; the coordinator wrote the
part from its saved runs (`$B/traces/dhewm3`, `$B/traces/gzdoom`) and from the clones it left
(`$B/src/{dhewm3,gzdoom,tdm,fs2open.github.com,doomsday}`). Clone dates: dhewm3 455b88e
2026-06-08, GZDoom c26ce2e 2026-08-10, fs2open 2026-10-18.

## dhewm3 (Doom 3 GPL engine, 1.5.5 / git 455b88e)

Verdict: not GL3-blocked. It renders with GL 1.x fixed function plus ARB_vertex_program /
ARB_fragment_program, which nv30 exposes; big-endian is a first-class build option. The blocker is
memory: the free Doom 3 demo's first map peaked at 1161 MB on the PC. Effort M (our ppc64 build,
texture downsizing and compression, memory cuts), playable only if those cuts land.
FEATURES: glsl_min=none (ARB assembly) core_only=n fallback=gl1 int_ops=no bitwise=no texelFetch=no
  textureSize=no ubo=no instancing=no tex_arrays=no float_tex=no float_rt=no rg_tex=no mrt=1 vao=no
  sync=no geom=no tf=no compute=no derivatives=no textureLod_fs=no vertexid=no flat=no arb_fbo=no
  srgb_fb=no vtf=no dyn_loops=no big_shaders=no (ARB programs written for NV30/R300-class cards)
SCORES: popularity=4 novelty=3 playability_256MB=2 effort=M ppc64=not-in-debian (contrib, arch any:
  our build) data=non-free (free demo, 462 MB) size_MB=462 (demo) / ~1500 (retail)

### 1. GL needs
- Measured (trace of `+map game/demo_mars_city1`, 526 frames): 84 entry points, no GLSL at all.
  ARB programs carry the lighting: glProgramEnvParameter4fvARB 497k calls, glBindProgramARB 17.8k,
  19 glProgramStringARB. Fixed-function arrays and matrices for the rest, glStencilOpSeparate and
  glDepthBoundsEXT for stencil shadows, two-sided stencil, cube maps (414 cube uploads), texture
  formats RGBA8/RGB8/INTENSITY8 and DEPTH_COMPONENT24, no FBOs, no MRT. Median 120 draws and 2638 GL
  calls a frame (max 2452 draws).
- Its log on llvmpipe uses GL_ARB_multitexture, texture_env_combine/dot3, texture_cube_map, NPOT,
  S3TC, EXT_texture3D, EXT_stencil_wrap, EXT_stencil_two_side, ARB_vertex_buffer_object, ARB vertex
  and fragment programs. All are in nv30's list except EXT_depth_bounds_test (UNVERIFIED on nv30;
  dhewm3 checks it as optional, `r_useDepthBoundsTest`).
- 1.5.x additions are ARB too: gamma in shaders (`r_gammaInShaders`, Changelog.md:160), soft
  particles via a depth capture (`r_enableDepthCapture`, Changelog.md:101), both optional.

### 2. Port
Nothing to port in the renderer. Work items: our ppc64 package (Debian's 1.5.5+dfsg-2 is contrib,
so ports never built it), `com_machineSpec 0`, `image_downSize 1`, `image_downSizeLimit 256`,
compressed textures (Doom 3's own `image_useCompression`, or Mesa 0038), `r_gammaInShaders 0`
(gamma through the X server instead of a full-screen pass) and a check of the ARB fragment
programs against nv30's limits (they were written for NV30 hardware, so they should fit).

### 3. Memory
- Measured, 1920x1080, llvmpipe, `com_machineSpec 0`, no sound: demo_mars_city1 VmHWM 1161 MB
  (twice, 1157 and 1161), plateau 20-30 s after the load. A run that stayed in the menu (a map name
  the demo lacks) held 234 MB.
- The trace counts 473.5 MB of textures, all uncompressed RGBA8/RGB8 at full size, and 9 MB of
  buffers. By the brief's formula: 1161 - 100 - 474 - 9 = ~580 MB of engine RAM (estimate), plus the
  textures for VRAM.
- Cuts: `image_downSize` with a 256 limit and DXT should bring textures to 30-60 MB (estimate). The
  580 MB of engine memory is the problem: frame allocators, collision models, AAS, the renderer's
  CPU copies of model geometry and shadow volumes, and 64-bit pointer bloat. Doom 3 needed 384 MB on
  Windows XP; the Xbox port shipped in 64 MB with cut maps. Estimate after cuts: 300-450 MB, so
  heavy swapping; playable only on small maps or with engine work. UNVERIFIED which allocations
  dominate (a `memoryStats`/massif run would say).

### 4. CPU
Measured on the PC: the game thread spent 18.9-30.4 s over 75-130 s including the load; llvmpipe
dwarfed it. Doom 3's CPU costs are skinning and stencil shadow volume construction on the CPU,
and the game logic is native C++ (no VM). dhewm3 keeps id's AltiVec SIMD code
(`neo/idlib/math/Simd_AltiVec.cpp`, 3939 lines, selected at `Simd.cpp:89-90` when the CPU reports
AltiVec), so the PPE gets vectorised skinning and shadow math. Doom 3's minimum was a 1.5 GHz P4;
estimate 15-30 fps on the PPE once the driver's share is counted, worse in shadow-heavy rooms.
Shadow volumes are pushed every frame: our VBO path matters (2638 GL calls a frame is high for the
PPE).

### 5. ppc64 big-endian
Explicit: CMake runs `TEST_BIG_ENDIAN` and sets `D3_IS_BIG_ENDIAN` (neo/CMakeLists.txt:355-361),
`idlib/Lib.cpp:49` refuses to build without it, and the CMake keeps PPC Mac flags
(neo/CMakeLists.txt:122, 487-491). id shipped Doom 3 on PPC Macs, so the data loaders swap. Debian
contrib builds it on arch any; debian-ports never builds contrib. Expect an S build job.

### 6. Data
Doom 3 is non-free. The demo (demo00.pk4, 462 MB, three maps) is a free download dhewm3 runs
(measured: "Loaded pk4 .../demo00.pk4", fell back to the base game library). Retail via GOG/Steam.
Free total conversions need the base game.

### 7. Novelty
Doom 3 BFG Edition was an official PS3 GameOS release, so the game is not new on the console; the
original Doom 3 (with its mods) was never on it. Novelty 3.

## RBDOOM-3-BFG

Out: since 1.4 it renders through NVRHI (Vulkan or D3D12; the GL backend was removed). Doom 3 BFG
was an official PS3 release anyway. No entry needed beyond this line.
SCORES: popularity=3 novelty=1 playability_256MB=1 effort=XL ppc64=n/a data=non-free

## The Dark Mod (2.13-era source)

Verdict: out. `GLimp_CheckRequiredFeatures` demands GL_VERSION_3_3 and S3TC and errors otherwise
(renderer/backend/qgl/qgl.cpp:71-80); the renderer is GLSL 330+ with uniform blocks and multi-draw
paths (from its docs; UNVERIFIED line by line), the data is several GB and its system requirements
start at 4 GB RAM (UNVERIFIED). A GL 2 port would be a rewrite of the 2.08+ renderer; the last GL 2-class
release (pre-2.06) would still need 1+ GB.
FEATURES: glsl_min=330 core_only=y fallback=none ubo=req (est) others ?
SCORES: popularity=3 novelty=5 playability_256MB=1 effort=XL ppc64=not-in-debian data=free (CC-BY-NC-SA) size_MB=~5000 (est)

## GZDoom 4.14 (and Raze, same backend)

Verdict: not GL3-blocked. GZDoom ships a GLES 2 backend that has an explicit desktop-GL-below-3.3
mode with GLSL 100 shaders; it needs one texture-format fix for nv30 and our ppc64 build. The cost
is the CPU: ZScript runs in an interpreter off x86_64. Effort M. Popular because of its mods, but
Doom itself is already on the PS3 (dsda-doom here, PrBoom on GameOS).
FEATURES: glsl_min=100 core_only=n fallback=gles2 (desktop GL 2.0 compat context) int_ops=no
  bitwise=no texelFetch=no textureSize=no (commented out, present.fp:23) ubo=no (uniform arrays)
  instancing=no tex_arrays=no float_tex=no float_rt=no rg_tex=req (GL_RED, patchable) mrt=1 vao=?
  sync=opt geom=no tf=no compute=no derivatives=no textureLod_fs=no vertexid=no flat=no arb_fbo=no
  srgb_fb=no vtf=no dyn_loops=? (up to 32 lights per surface, `gles_max_lights_per_surface`)
  big_shaders=? (one uber-shader with variants)
SCORES: popularity=5 novelty=2 playability_256MB=4 (vanilla maps) effort=M ppc64=not-in-debian
  data=free (Freedoom) size_MB=~30 engine + IWAD

### 1. GL needs (source)
- `vid_preferbackend 2` picks the GLES renderer (src/common/rendering/gles/). On desktop GL it reads
  the version: 3.3+ is GLES_MODE_OGL3 with `#version 330`, anything lower is GLES_MODE_OGL2 with
  shader version "100", NPOT, packed depth-stencil and mapped buffers assumed
  (gles_system.cpp:186-230). The SDL window asks for an ES 2.0 context (`gl_es`) or a 2.0
  compatibility context when the version list runs down (sdlglvideo.cpp:213-229).
- Shaders: wadsrc/static/shaders_gles, 27 files. Bone skinning (`uint boneIndex`) sits behind
  `#if __VERSION__ >= 300` (main.vp, around line 156), so GLSL 100 drops GPU skinning of models.
  Integer vertex attributes are only bound in OGL3 mode (gles_buffers.cpp:313-317).
- Scene target: an RGBA texture plus D24S8 (or D16) renderbuffer (gles_renderbuffers.cpp:146-163);
  post-processing is a 226-line file (present, dither). No HDR, SSAO or bloom in this backend.
- The one gap on nv30: one-byte textures are uploaded as GL_RED with a texture swizzle in the desktop
  modes (gles_hwtexture.cpp:149-172). nv30 has EXT_texture_swizzle (glxinfo) but no ARB_texture_rg,
  so GL_RED is not a valid internal format there: change it to GL_ALPHA (or LUMINANCE) as we did in
  dsda-doom. S.

### 2. Port
Build with `HAVE_GLES2=ON` (the default on Linux, CMakeLists.txt:236-238) and Vulkan off; the
GL_RED patch; force `vid_preferbackend 2`. Lost against the GL 3.3 renderer: dynamic light
shadowmaps, SSAO, bloom/tonemap, GPU model skinning, user shaders (`gl_customshader = false`,
gles_system.cpp:179). It looks like a 2010 GZDoom, which for Doom is fine.

### 3. Memory
The saved run (`+vid_preferbackend 2`, apitrace) sat at 49 MB with 0.6 s of CPU in 60 s: it never
left an early window (no trace, no frames), so nothing was measured. Estimate from GZDoom's
reputation and its 3.x numbers: 120-250 MB for Freedoom/vanilla maps at 1080p, more with big mods
(Brutal Doom, high-res packs). UNVERIFIED.

### 4. CPU
The ZScript VM has a JIT only on x86_64 (CMakeLists.txt:230-232, `HAVE_VM_JIT`), so on ppc64 every
actor's logic runs in the interpreter (estimate 3-5x slower than the JIT). Vanilla maps are light;
slaughter maps and gameplay mods are not. The GLES backend draws through client-side uniform
uploads per surface; draw counts are higher than dsda-doom's (UNVERIFIED).

### 5. ppc64 big-endian
`src/common/utility/m_swap.h:69, 209, 243` keep `__BIG_ENDIAN__` paths and 41 BE conditionals
remain in src/. Nobody builds GZDoom for BE today (no Debian package at all, ZDoom's PPC Mac builds
ended long ago), so expect bit-rot in the newer code (ZScript serialisation, model formats,
softpoly). UNVERIFIED; an S-M fix list once it runs. Not in Debian main or contrib (Sources checked).

### 6. Data
Freedoom (BSD), packaged in Debian. Mods are the draw; most are free to download, licences vary.

### 7. Novelty
PrBoom and Doom ports exist on GameOS homebrew, and we run dsda-doom. GZDoom-only mods would be
new. Novelty 2.

## FreeSpace 2 Open (fs2open master 2026-10-18)

Verdict: GL3-blocked hard. The current renderer requires GL 3.2 core and GLSL 150, samples every
texture as `sampler2DArray`, and keeps its uniforms in uniform blocks. A port of master is XL. The
realistic route is an old release with the GL 2 / fixed-function renderer (3.7.4, 2016) and retail
or freeware data: M, with modern mods lost.
FEATURES (master): glsl_min=150 core_only=y fallback=none int_ops=? bitwise=? texelFetch=req (5
  shaders) textureSize=req ubo=req (46 shader files with uniform blocks) instancing=req
  (gropengldraw.cpp:1262, gropengltnl.cpp:668) tex_arrays=req (36 uses, every material shader)
  float_tex=opt float_rt=opt (deferred, HDR) rg_tex=? mrt=req for deferred (opt) vao=req sync=opt
  geom=opt (effect-g, main-g, shadow_map-g; behind `-no_geo_effects`) tf=no compute=no
  derivatives=? textureLod_fs=? vertexid=? flat=? arb_fbo=req srgb_fb=? vtf=no dyn_loops=req
  (lighting loops) big_shaders=req
SCORES: popularity=4 novelty=5 playability_256MB=3 (retail data, old engine) effort=XL (master) /
  M (3.7.4) ppc64=not-in-debian data=non-free retail; free TCs (The Babylon Project, Wing Commander
  Saga, Diaspora) size_MB=~600-2000

### 1. GL needs (source)
- `MIN_REQUIRED_GL_VERSION = 32` and `MIN_REQUIRED_GLSL_VERSION = 150`, a fatal error below
  (code/graphics/opengl/gropengl.cpp:54-57, 962-963, 1347-1348).
- Texture arrays everywhere: bmpman's textures are bound as GL_TEXTURE_2D_ARRAY
  (gropengltnl.cpp:470-474, gropengldeferred.cpp:255, gropenglpostprocessing.cpp:636), and 36
  `sampler2DArray` uses across main-f, default-material-f, batched-f, decal-f, nanovg-f, rocketui-f,
  video-f, shield-impact-f and shadows.sdr.
- Uniform blocks in 46 effect files; texelFetch/textureSize in msaa-f, volumetric-f, shadow_map-v,
  main-v, decal-f. Instanced draws for batched effects and models.
- Geometry shaders only for optional effects; gpu_shader5 and viewport-layer-array gate shadow
  cascades (gropengl.cpp:1441-1442, 1605, 1625).

### 2. Port
The array habit is what kills a driver-level shortcut: a 2D array with mipmaps can't be faked on
NV40 (see G-driver.md), and FS2 uses arrays for animation frames of every texture. A renderer port
of master means bmpman without arrays, uniform blocks to uniforms, no instancing, forward lighting
only: XL. Old route: fs2open 3.7.4 (or earlier 3.6.x), whose renderer was GL 1.2 fixed function plus
optional GLSL 1.10/1.20 (from memory of its 2016 requirements, UNVERIFIED), runs retail FS2 and the older freeware TCs. Its BE state is the open
question (below). Effort M.

### 3. Memory
Not measured (no Linux build with free data was fetched). Estimate: retail FS2 data on 3.7.4 at low
settings 200-350 MB (the 1999 game needed 32-64 MB; the engine has grown, and the MediaVPs high-res
pack multiplies it); with MediaVPs 1+ GB. UNVERIFIED.

### 4. CPU
Native C++ with Lua scripting (since 3.6) for mods; FS2's AI and physics ran on a 233 MHz
Pentium II. The PPE is not the limit with retail data; particle-heavy mods are.

### 5. ppc64 big-endian
`code/globalincs/pstypes.h:395-413` maps INTEL_INT/INTEL_SHORT to SDL_Swap*LE on big-endian, which
the loaders call; that dates from the PPC Mac builds (3.6.x era). Master is not built for BE by
anyone (UNVERIFIED whether newer loaders use the macros). Not in Debian.

### 6. Data
FS2 retail is non-free (GOG). The Babylon Project, Wing Commander Saga and Diaspora are freeware
total conversions on fs2open (licences forbid sale; free download; sizes 1-5 GB, UNVERIFIED), most
built for fs2open 3.7+/4.x, so an old engine may not load the newer ones.

### 7. Novelty
No FreeSpace on PS3 GameOS (found none). Novelty 5.

## Doomsday 2.3.1 (debian-ports ppc64 has 2.3.1+ds1-2)

GL 3.3 core by default, with a build switch `DE_OPENGL_API` = 3.3 / GLES3 / GLES2
(doomsday/libs/gui/CMakeLists.txt:10-58; core profile requested at glwindow.cpp:73-75). A GLES2
build is the cheap route if its GLES2 code still compiles (UNVERIFIED, it may only be maintained for
GLES3). Low priority: dsda-doom already plays Doom, Heretic and Hexen on the PS3, and Doomsday is
Qt-heavy (RAM). SCORES: popularity=2 novelty=2 playability_256MB=2 effort=M ppc64=builds data=free
(Freedoom).

## Sources
- `$B/traces/dhewm3/{tr,map1,run2}.mem.txt`, `census/report.md`, `map1.out.log` (dhewm3 1.5.5 from
  Debian amd64 contrib, demo00.pk4); `$B/traces/gzdoom/gles.mem.txt` (gzdoom_4.14.2_amd64.deb).
- dhewm3 455b88e: neo/CMakeLists.txt, neo/idlib/Lib.cpp, neo/idlib/math/Simd.cpp, Simd_AltiVec.cpp,
  Changelog.md. Debian sid-contrib Sources (dhewm3 1.5.5+dfsg-2, arch any).
- GZDoom c26ce2e: src/common/rendering/gles/{gles_system,gles_hwtexture,gles_buffers,
  gles_renderbuffers,gles_postprocess}.cpp, wadsrc/static/shaders_gles, sdlglvideo.cpp,
  CMakeLists.txt, src/common/utility/m_swap.h.
- fs2open master: code/graphics/opengl/{gropengl,gropengltnl,gropengldraw,gropengldeferred,
  gropenglpostprocessing}.cpp, code/def_files/data/effects/*.sdr, code/globalincs/pstypes.h.
- The Dark Mod: renderer/backend/qgl/qgl.cpp. Doomsday: libs/gui/CMakeLists.txt, glwindow.cpp.
