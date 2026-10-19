## Endless Sky (own C++ engine, SDL2 + GLEW, upstream master 2026-10-18; Debian sid/ports 0.11.2-1)

Verdict: the only game of the pair worth porting, and upstream already ships an "experimental OpenGL 2" path (3D textures instead of texture arrays, no VAO, EXT FBO) that needs GLSL 1.20 shader files supplied as a plugin, so effort is M. The blocker is memory: 1.3 GB RSS on the PC with the lowest texture settings, 477 MB of it GPU textures; it fits 256 MB only after DXT compression plus more cuts, and that is unproven. ppc64 binary exists in debian-ports, novelty is 5 (no PS3 port found).

FEATURES: glsl_min=130 (version string is taken from the driver, no #version in the files: Shader.cpp:107-130) core_only=n (asks 3.0 core, retries a non-core context: GameWindow.cpp:224-247) fallback=gl2 (experimental; GLES 3.0 build also exists, no GLES2) int_ops=opt (sprite.frag uses int uniforms as flags only) bitwise=no texelFetch=no textureSize=no ubo=no instancing=no tex_arrays=req on the GL3 path, no on the GL2 path (GL_TEXTURE_3D, Sprite.cpp:38) float_tex=no float_rt=no rg_tex=opt (fog map uses GL_R8/GL_RED, FogShader.cpp:207) mrt=1 vao=opt (OpenGL::HasVaoSupport, off on the GL2 path) sync=no geom=no tf=no compute=no derivatives=no textureLod_fs=no vertexid=no flat=no arb_fbo=opt (RenderBuffer.cpp:123: CORE or EXT, EXT works; one RGBA8 texture FBO, no blit, no MSAA) srgb_fb=no vtf=no dyn_loops=opt (sprite.frag blur: constant 11-tap loop, unrolls) big_shaders=no (22 shaders in the trace, largest fragment 2.7 KB source)

SCORES: popularity=4 novelty=5 playability_256MB=2 effort=M
  ppc64=builds (endless-sky 0.11.2-1 ppc64 in unstable-main-ppc64; endless-sky-data is arch:all, 396 MB installed) data=free size_MB=~400

### 1. GL needs (source + trace)
Source (upstream master, shallow clone, git date 2026-10-18):
- Context: 3.0 core on Linux, 3.0 ES with ES_GLES (GameWindow.cpp:227-232; CMakeLists.txt:9 option ES_GLES, preset `linux-gles`). If creation fails it retries with profile 0 and, when GL_VERSION starts with "2", calls OpenGL::DisableOpenGL3() and logs "Experimental OpenGL 2 support has been enabled" (GameWindow.cpp:242-247, 306-312). Hard minimum is GL 2.0.
- OpenGL::HasVaoSupport / HasTexture2DArraySupport / HasClearBufferSupport all return "is GL3" (opengl.cpp:43-70, TODO comments say no extension check yet). FBO: ARB_framebuffer_object name or EXT_framebuffer_object, both handled (opengl.cpp:75-85, RenderBuffer.cpp:123-156, 182-230).
- Sprites: one GL_TEXTURE_2D_ARRAY per sprite, all frames as layers, RGBA8, no mipmaps, linear or nearest filter, CLAMP_TO_EDGE (Sprite.cpp:38-90). Fallback is GL_TEXTURE_3D with the same upload. Texture arrays have been used since v0.9.9 (changelog:4630); before that one texture per frame.
- Shaders are data files in shaders/*.vert/*.frag (22 files), GLSL 130-style `in`/`out`, `precision mediump` lines, no #version. sprite.frag: sampler2DArray, a frame mix with `mod`/`floor`, an 11-tap blur loop, a swizzle matrix. GL2 users need shaders in a plugin: "Plugins can now provide custom shaders to support the game running using OpenGL 2" (changelog 0.11.0, line 703; README.md:26; wiki CreatingPlugins: sampler2DArray becomes sampler3D). `.gl` / `.gles` suffixes select shaders per API (GameData.cpp:232-243). I found no shipped GL2 shader set in the tree: UNVERIFIED whether a public plugin exists.
- Everything else is plain: glDrawArrays only (TRIANGLE_STRIP of 4 verts per sprite, triangle lists for the starfield), 11 small static VBOs, per-draw glUniform calls, no instancing, no UBO, no integer attributes.
- Other GL3 spots: glClearBufferfv in RenderBuffer.cpp:211 (guarded by HasClearBufferSupport), GL_R8/GL_RED fog texture (FogShader.cpp:207-212, not guarded), proxy texture size check (Sprite.cpp:46-60, non-ES only).
Trace (apitrace on llvmpipe, which gave a 4.6 context so the GL3 path ran; only the first 4M lines of the dump were read, the game kept running and the trace reached 19 GB, deleted): 1626 frames, main menu, new-pilot dialogs, shipyard, main menu again, planet panel (New Boston). NOT captured: flight (the script ended on the first landing conversation, which needs a choice before Depart works; I did not repeat the run). 799k draws, median 410 per frame, max 1191; GL calls median 1880 per frame. Per frame about 4.6 glUniform calls per draw (934k Uniform2fv, 738k Uniform1f, 716k Uniform1i for 799k glDrawArrays), 214k glBindVertexArray/UseProgram (one program switch per draw). Only GL_TEXTURE_2D_ARRAY (2766 uploads) plus 3 plain 2D (font, render buffer); formats GL_RGBA8 only; one FBO with 1 colour attachment; 22 shaders, none with texelFetch, ints, bitwise, UBO or derivatives. Flight draw counts are higher (every ship, projectile, flare, star layer is its own draw): ESTIMATE 500-1500 per frame.

### 2. Port to GL 2.1/GLES 2 on nv30: 1:1 / emulate / drop; how it looks; effort and work items
Route: use the game's own GL2 mode and write the missing shaders. Nothing is dropped except maybe the fog texture.
1. Shader set in GLSL 1.20 (22 files, mostly 10-30 lines): `attribute`/`varying`, `gl_FragColor`, `texture2D`/`texture3D`, drop `precision` lines, `sampler3D` for sprites with the frame as the third coordinate ((frame+0.5)/frameCount); the blur loop has a constant bound and unrolls (2 texture reads x 11 taps, 4096-instruction limit is fine). Guard: `if` on uniforms in sprite.frag becomes a branch the compiler may flatten.
2. nv30 samples 3D textures (RSX.md: EXT_texture3D sampling works; rendering into them is refused, the game never does). Size limits of 3D textures on nv30 (max depth, NPOT, DXT 3D): UNVERIFIED, test. Sprites with many frames (ships 30-100 layers, explosions) are the risk.
3. Fog map: GL_R8/GL_RED has no ARB_texture_rg on nv30; upload as GL_LUMINANCE8 (one source line, the shader reads .r). Else disable fog (cosmetic starmap fog of unexplored regions).
4. FBO: EXT path exists (RenderBuffer.cpp:154-156). glDrawBuffers(1) is used there; fine on nv30.
5. Batching: one draw + ~5 uniform calls per sprite. X-Moto got 3.7x from batching; a sprite batcher (BatchShader exists for lines) would help the PPE-bound case. Not needed for "works".
Look: identical (the GL2 path is a straight swap of texture type). Texture filtering, motion blur, haze are game options.
Driver-level emulation (Mesa nv30): texture-array to 3D-texture aliasing could let the unmodified GL3 shaders run, but the shaders need GLSL 130+ (`in`/`out`) and sampler2DArray, so a GLSL front end change; much more work than shipping 22 GLSL 1.20 files. Not recommended.
Effort M: about a week for shaders + 3D texture checks + fog fix; the rest is memory.

### 3. Memory: measured (setup) and PS3 estimate, biggest consumers, cuts
Measured on the PC (llvmpipe, Xvfb 1920x1080, Debian sid amd64 0.11.2, window 1820x980, menu then new pilot then planet panel):
- Defaults ("Reduce large graphics" off): peak RSS 2645 MB, VmHWM 2879 MB at 30 s (menu).
- Lowest setting ("Reduce large graphics" = all, haze and motion blur off, prefs in ~/.local/share/endless-sky/preferences.txt): steady 1293 MB RSS, VmHWM 1556-1567 MB, 1325-1392 MB peak tree; reached about 5-10 s into one run, 30+ s in another (background loading threads).
- glcensus: 2766 array textures, 476.5 MB uncompressed RGBA8 (with "all" reduction); buffers 1.5 MB.
- PS3 estimate: PC RSS 1293 - ~100 (llvmpipe baseline) - 477 (textures) = about 700 MB of CPU-side data left, which is not understood (candidates: sprite collision masks and outlines kept per sprite, decoded sounds 45 MB on disk, game data/universe, the images' own CPU copies llvmpipe may double-count). UNVERIFIED which. Textures then go to VRAM/swap: 477 MB RGBA8 is over the whole 256 MB.
- Cuts: DXT5 at upload via patch 0038 / NV30_FORCE_DXT: RGBA8 arrays become about 120 MB (4x), sprites are soft sci-fi art and tolerate DXT5; quarter-size reduction of big images (ShrinkToHalfSize once more, Sprite.cpp:29-35 is a one-line change); "Reduce large graphics = all" already on. A test of what the 700 MB is should come before any decision. Even with textures at 120 MB, a ~700 MB CPU residue is far above the 160 MB no-stall bar: playability 2 (could squeeze only if that residue shrinks 3-4x).

### 4. CPU
Single game thread plus loader threads. In the 50 s menu run the game thread used 20.6 CPU s on a modern x86 core (includes submitting 400+ draws per frame to llvmpipe, so mostly driver and GL call cost), llvmpipe 56-170 s. Simulation (Engine.cpp: ships, AI, projectiles, 60 Hz fixed step) is light C++ with small per-frame work; the PPE-bound cost will be GL submission (about 1900 GL calls per frame in menus, more in flight) times our driver stack cost. ESTIMATE 20-40 fps in flight at 1080p without batching, 60 with a batcher and at 720p. Marked as estimate, flight not measured.

### 5. ppc64 big-endian
Debian ppc64 build: published (endless-sky 0.11.2-1, endless-sky-dbgsym in unstable-main-ppc64). Image loading goes through libpng/libjpeg (ImageBuffer, source/image/); saves, preferences, game data are text (DataFile; preferences.txt and the pilots dir seen on disk are text); audio through OpenAL + libmad (no byte-order code expected). I did not grep the source for hand-written endian code: UNVERIFIED but nothing binary was seen. Sound/music on the PS3 uses OpenAL Soft like the other games.

### 6. Data and licence
Code GPL-3+; images CC-BY-SA-4.0 (images/land: public domain photos), sounds and music CC-BY-SA or similar mix, all free (copyright file, Debian main). Size: images 345 MB, sounds 45 MB, data 11 MB in git; Debian endless-sky-data 344 MB package, 396 MB installed.

### 7. Novelty (PS3 GameOS homebrew port?)
One web search ("Endless Sky PS3 homebrew", PSL1GHT lists) found nothing. Not in the PSL1GHT/ps3dev ecosystem as far as found. Novelty 5. Popularity: top-rated open-source space game, well known on Steam and GitHub, but smaller than Xonotic-class: 4.

### Sources
build/tmp/gl3census/src/es (shallow clone): source/GameWindow.cpp:224-247, 306-312; source/opengl.cpp:43-85; source/image/Sprite.cpp:29-90; source/shader/FogShader.cpp:207; source/RenderBuffer.cpp:123-230; source/shader/Shader.cpp:94-130; source/GameData.cpp:232; shaders/sprite.frag; README.md:17-26; changelog:703, 4630; CMakeLists.txt:9,219. Debian ports Packages (dl/ports). Census reports kept in $B/work/es-census-keep (report.md/json, shaders/). Wiki: github.com/endless-sky/endless-sky/wiki/CreatingPlugins.

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
