## Endless Sky (own C++ engine, SDL2 + GLEW, upstream master 2026-10-18; Debian sid/ports 0.11.2-1)

Verdict: the only game of the pair worth porting, and upstream already ships an "experimental OpenGL 2" path (3D textures instead of texture arrays, no VAO, EXT FBO) that needs GLSL 1.20 shader files supplied as a plugin, so effort is M. The blocker is memory: 1.3 GB RSS on the PC with the lowest texture settings, 477 MB of it GPU textures; it fits 256 MB only after DXT compression plus more cuts, and that is unproven. ppc64 binary exists in debian-ports, novelty is 5 (no PS3 port found).

FEATURES: glsl_min=130 (version string is taken from the driver, no #version in the files: Shader.cpp:107-130) core_only=n (asks 3.0 core, retries a non-core context: GameWindow.cpp:224-247) fallback=gl2 (experimental; GLES 3.0 build also exists, no GLES2) int_ops=opt (sprite.frag uses int uniforms as flags only) bitwise=no texelFetch=no textureSize=no ubo=no instancing=no tex_arrays=req on the GL3 path, no on the GL2 path (GL_TEXTURE_3D, Sprite.cpp:38) float_tex=no float_rt=no rg_tex=opt (fog map uses GL_R8/GL_RED, FogShader.cpp:207) mrt=1 vao=opt (OpenGL::HasVaoSupport, off on the GL2 path) sync=no geom=no tf=no compute=no derivatives=no textureLod_fs=no vertexid=no flat=no arb_fbo=opt (RenderBuffer.cpp:123: CORE or EXT, EXT works; one RGBA8 texture FBO, no blit, no MSAA) srgb_fb=no vtf=no dyn_loops=opt (sprite.frag blur: constant 11-tap loop, unrolls) big_shaders=no (22 shaders in the trace, largest fragment 2.7 KB source)

SCORES: popularity=4 novelty=5 playability_256MB=2 effort=M
  ppc64=builds (endless-sky 0.11.2-1 ppc64 in unstable-main-ppc64; endless-sky-data is arch:all, 396 MB installed) data=free size_MB=~400

The detail (GL needs, the port, memory, CPU, big-endian, data, novelty and its sources) is in the game's bundle: [gamedb/games/endless-sky/investigations/2026-10-19-gl3-census.md](../games/endless-sky/investigations/2026-10-19-gl3-census.md).

---

## Naev (own C engine + a new Rust renderer on glow/naga, SDL3; upstream master 0.14.0-alpha.4, git 2026-10-16; Debian sid 0.8.2)

Verdict: not portable to GL 2.1 in any sensible time. Upstream master requires a GL 3.3 core context (tries 4.6 down to 3.3) with UBOs, instancing, sampler objects, texelFetch, a glTF PBR ship renderer, a Rust toolchain and SDL3; effort XL, and it is a Rust-renderer rewrite rather than a port. The Debian 0.8.2 package is old (GL 3.1 core, GLSL 140, simple 2D) but still 1.7 GB RSS and 365 MB of data, and its gameplay was not measured. Novelty 5 but skip it.

FEATURES: glsl_min=150 (master: 150 or the context version when above 3.2: renderer/src/lib.rs:693; 0.8.2 measured: #version 140) core_only=y fallback=none int_ops=req (uint/ivec in 8 shader files, naga-generated code) bitwise=? texelFetch=req (4 files) textureSize=req (12 files) ubo=req (nebula_overlay.frag:5, rust_solid.glsl:1, gl.uniform_block_binding) instancing=req (background.c:229-238 dust field, glDrawArraysInstanced; gl.draw_arrays_instanced in the Rust side) tex_arrays=opt (1 file) float_tex=? float_rt=? rg_tex=opt (glow::RED x2) mrt=? vao=req sync=opt (glFenceSync in src, 3 uses) geom=? (changelog v0.11.0:1314 "Require OpenGL 3.2 for geometry shaders"; I found none in master's dat/shaders: UNVERIFIED) tf=no compute=no derivatives=opt (5 files dFdx/fwidth) textureLod_fs=no vertexid=opt (gl_VertexID in one 0.8.2 shader) flat=req (5 files) arb_fbo=req (glBlitFramebuffer in ship.c:498,1417,1465, nlua_gfx.c:1095; MSAA FBOs glTexImage2DMultisample; many FBOs) srgb_fb=opt (SRGB8_ALPHA8, framebuffer_srgb_compatible attribute: lib.rs:589) vtf=no dyn_loops=req (69 files with `for(`; blur, nebula, noise) big_shaders=req (ffx_a.h FidelityFX header, magic/nebula/jump shaders, glTF PBR with interface blocks: likely over 4096 FS instructions)

SCORES: popularity=3 novelty=5 playability_256MB=1 effort=XL
  ppc64=builds (naev 0.8.2-1+b3 and naev-data in unstable-main-ppc64; master untested, needs rustc >=1.89 and SDL3 on ppc64 BE) data=free size_MB=~365 (0.8.2 data package)

### 1. GL needs (source + trace)
Source (master, shallow clone):
- Context request in Rust: src/renderer/src/lib.rs:582-610: core profile, forward-compatible, double buffer, sRGB-capable framebuffer, optional MSAA, tries (4,6) then 4.5 ... 3.3, bails otherwise. The C side warns if not GL 3.3 (src/opengl.c:171-176: "We are interested in 3.3 because it drops all the deprecated stuff and gives us instancing").
- Renderer: 10k lines of Rust (src/renderer/src/*.rs: shader.rs, texture.rs, framebuffer.rs, buffer.rs, model.rs, luagfx.rs). Uses sampler objects, UBOs (uniform_block_binding, bind_buffer_base), vertex_attrib_divisor, blit_framebuffer, read_pixels, debug output, CLAMP_TO_BORDER, DEPTH_COMPONENT32 depth textures, glow::SRGB8/SRGB8_ALPHA8.
- Shaders: 124 files in dat/shaders (277 shader-ish files overall), some in WGSL (circle.wgsl, solid.wgsl, texture.wgsl, rect_hollow.wgsl; 532 lines) that naga translates to GLSL at run time (shader.rs:282-436). Lua scripts can add shaders (nlua_gfx.c: gfx.shader, GLSL version exposed to Lua at nlua_gfx.c:1201); the love-compat layer in dat/scripts/love uses sampler arrays/3D. Optional ARB_shader_subroutine (jump.frag).
- Geometry: ships are glTF models with PBR materials (gltf.c, material_pbr.glsl).
- Iced/wgpu in Cargo.toml.in:94 is only for the separate plugin-manager GUI, not the game renderer. Vulkan-only: no.
- GLES: no support found (no ES profile request in the code).
Trace (Debian 0.8.2 amd64, a different, older code base; apitrace on llvmpipe; only the loading screen was captured, 128 frames, I did not get into the game): context request 3.1 core; 20 unique shaders `#version 140`; one VAO; glGenerateMipmap x488 over 751 GL_TEXTURE_2D uploads (4 GL_RED, so R8 for fonts); texture bytes 1155.6 MB uncompressed estimate at the end of loading (it preloads textures with mipmaps); gl_VertexID in one vertex shader. The draw-per-frame numbers (8458 draws, 27575 GL calls per frame) come from the loading screen and are not gameplay figures. No UBO, no instancing, no FBO attachments in that capture.

### 2. Port to GL 2.1/GLES 2 on nv30: 1:1 / emulate / drop; how it looks; effort and work items
Master: no. A port needs the Rust renderer re-targeted to GLSL 1.20 (naga has a GLSL 1.20 backend? UNVERIFIED, it targets 1.30+/ES 3.0 normally), UBOs turned into uniform arrays, instancing for the dust field turned into a CPU loop, MSAA/blit FBOs rewritten (nv30 has EXT FBO only), texelFetch/textureSize replaced, all 124 shaders audited against 32 temps and 512 VS instructions, and the glTF PBR path dropped or simplified. Mesa-side emulation could cover UBO (as uniforms), ints (as floats) and texelFetch (nearest sampler plus a size uniform), but not sampler objects (cheap), blit between differently sized FBOs (mixed sizes unsupported on nv30), nor the shader sizes. Effort XL.
Debian 0.8.2: GLSL 140 shaders and a simple 2D renderer look like a plausible base for a GL2 backport (shader rewrite, GL_RED to LUMINANCE) but it was not read here (no source tree fetched for 0.8.x), so effort is a guess: L, and it would diverge from upstream and from current content.

### 3. Memory: measured (setup) and PS3 estimate, biggest consumers, cuts
Measured (Debian 0.8.2 amd64, llvmpipe, Xvfb 1920x1080, 70 s from start in the loading/menu phase, default settings, traced): peak RSS 1693 MB, VmHWM 1688 MB, reached at 10 s and flat after. Glcensus texture bytes 1155.6 MB (uncompressed, mipmapped). PS3 estimate: 1693 - 100 - 1156 = about 440 MB of non-texture memory (Lua, data, sound), plus 1.1 GB of textures: far over budget. DXT and dropping mipmaps would cut textures to maybe 250 MB, not enough. Master's footprint was not measured (no AppImage/tarball fetched, and it needs the Rust renderer). Playability 1.

### 4. CPU
All game logic (missions, AI, events, GUI) is Lua 5.1; LuaJIT is optional (meson.build:176-190) and used where available. On PPC big-endian LuaJIT runs in interpreter mode only for us (ppc64 has no JIT in LuaJIT 2.1), so expect a several-fold slowdown against a PC for AI and mission scripts. Rendering: 8458 draws per frame in the loading screen of 0.8.2 (not a gameplay number). Measured game-thread CPU in the 70 s load/menu run: 61 CPU s on a modern x86 core (no llvmpipe split reported, the rasteriser ran inside the same threads). Gameplay CPU not measured. ESTIMATE: unplayable on the PPE for master.

### 5. ppc64 big-endian
Debian 0.8.2 builds on ppc64 (naev 0.8.2-1+b3 in ports). Master needs Rust >=1.89 (meson.build:20-26), SDL3 >=3.2 (we have SDL3 patched for BE), physfs data archives (physfs handles BE), libxml2/cmark/enet. Rust crates for audio (symphonia) and the renderer's own byte handling (buffer.rs, model.rs gltf loading) were not checked for endianness: UNVERIFIED. Save format: XML in 0.8.x (from memory, UNVERIFIED); master unknown.

### 6. Data and licence
Code GPL-3 (some dual); data (naev-data) mostly CC-BY-SA/CC0, free, in Debian main. Debian naev-data 0.8.2: 365 MB installed (350 MB download). Master dat/ is 23 MB in git and the art lives in a separate assets submodule (not fetched).

### 7. Novelty (PS3 GameOS homebrew port?)
The same web search found no PS3 port of Naev. Novelty 5. Popularity 3 (well known among open-source space games, smaller than Endless Sky).

### Sources
build/tmp/gl3census/src/naev (shallow clone, 2026-10-16): src/renderer/src/lib.rs:582-610,689-699; src/renderer/src/shader.rs:282-436; src/opengl.c:100-176; src/opengl_shader.c:253; src/background.c:229-238; src/ship.c:498,1417,1465; src/nlua_gfx.c:1095,1201; meson.build:1-26,144,176-190; Cargo.toml.in:94; Changelog.md:91,262,406-407,1314; dat/shaders/{nebula_overlay.frag,rust_solid.glsl,jump.frag,material_pbr.glsl}. Debian ports Packages (dl/ports). Census reports kept in $B/work/naev-census-keep (report.md/json, shaders/, menu.mem.txt) and $B/work/es-census-keep.

Cleanup done: traces (19 GB ES, 744 MB Naev), downloads and work trees deleted; containers es and naev removed; clones in $B/src/es and $B/src/naev kept (small).
