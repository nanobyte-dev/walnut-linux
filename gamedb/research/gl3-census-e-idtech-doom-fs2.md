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

The detail (GL needs, the port, memory, CPU, big-endian, data, novelty and its sources) is in the game's bundle: [gamedb/games/doom-3/investigations/2026-10-19-gl3-census.md](../games/doom-3/investigations/2026-10-19-gl3-census.md).

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

The detail (GL needs, the port, memory, CPU, big-endian, data, novelty and its sources) is in the game's bundle: [gamedb/games/gzdoom/investigations/2026-10-19-gl3-census.md](../games/gzdoom/investigations/2026-10-19-gl3-census.md).

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

The detail (GL needs, the port, memory, CPU, big-endian, data, novelty and its sources) is in the game's bundle: [gamedb/games/freespace-2/investigations/2026-10-19-gl3-census.md](../games/freespace-2/investigations/2026-10-19-gl3-census.md).

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
