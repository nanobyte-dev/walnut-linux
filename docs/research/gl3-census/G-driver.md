# G. Driver-level GL 3.x subset in Mesa nv30: feature table and verdict (2026-10-19)

Question: would one piece of work in our Mesa nv30 (a per-game "GL 3.x subset" version override,
NIR lowering passes, and the NV40/RSX hardware features Mesa leaves off) unlock several GL 3+
games at once, compared with porting each game's renderer to GL 2.1?

Sources read: Mesa 26.2.4 as we ship it (build/src/mesa/mesa-26.2.4, patches 0001-0039 applied,
`.pc/applied-patches`); RPCS3 master `rpcs3/Emu/RSX` (sparse shallow clone, 2026-10-19, in
`/mnt/data_linux/Temp/ps3/gl3census/src/rpcs3`); envytools master `rnndb/graph/nv30-40_3d.xml`
(same place); ps3rsx `src/validate.c`; NVIDIA GPU Gems 2 chapter 30 "The GeForce 6 Series GPU
Architecture" (fetched); our docs RSX.md, MESA-METHODS.md, research/RSX-SPU-FEASIBILITY.md s.4,
backlog/graphics.md, research/HARDWARE-LEVERAGE.md, glxinfo-nv47.txt. One census trace (Taisei,
`$B/traces/taisei/c2/report.md`) was available as a concrete GL 3.3 core example.

Labels: **src** = read in source (file:line; Mesa paths relative to `src/gallium/drivers/nouveau/nv30/`
unless they start with `src/`), **doc** = from a document (named), **est** = estimate, **UNVERIFIED**
= no source confirms it.

## Verdict

Yes for a bounded subset, no as a general GL 3.3. The pieces that matter most to "GL 3.3 core but
simple" games are cheap because the hardware already has them (instancing divider, FP16/FP32
render targets and textures, TXL/TXD/DDX/DDY, primitive restart, per-MRT blend enable) or because
they are compile-time rewrites with no per-draw CPU cost (GLSL 1.30-3.30 syntax with ints as
floats, texelFetch, textureSize, gl_VertexID). Mesa already creates a core 3.3 context on nv30
when told to (MESA_GL_VERSION_OVERRIDE plus MESA_GLSL_VERSION_OVERRIDE, src read below), so the
"core profile" part costs nothing; what costs is making the features behind it real.

About 7-9 weeks of driver work (est, the ordered list at the end, items 1-10) gives an honest
"GL 3.3 subset" profile: VAOs, GLSL 330 syntax, small-int maths, texelFetch/textureSize, instancing
with gl_InstanceID, UBOs that fit the constant space, R8/RG8, FP16/FP32 render targets, mixed-size
FBO attachments, derivatives, textureLod/textureGrad in fragment shaders. A game that stays inside
that set then needs no renderer change, only the usual memory, CPU and big-endian work. Taisei
(the one census trace in so far) is the shape that benefits: core 3.3, 17 shaders all `#version
330 core`, instanced sprite quads (`glDrawArraysInstanced` x680, `glVertexAttribDivisor` x20),
GL_R8 textures, 4 draw buffers, derivatives and one textureLod, no UBO/GS/TF/arrays.

It does not help games whose renderers need what the RSX lacks: bitwise-heavy shaders (hashes,
bit packing, `floatBitsToUint`), integer textures and render targets, geometry shaders and
transform feedback at game rates, compute, texture arrays with mipmaps, mixed-format G-buffers,
more than 8 varyings (vec4). Those still need renderer ports, and for them the driver work only
shrinks the port (the port can keep GLSL 330 syntax, VAOs and instancing).

Break-even (est): one GL 3.3 renderer port to GL 2.1 is M to L per game (brief). If the other
census parts find 3 or more novel games whose GL 3 use is inside the subset, the driver work pays;
with 1-2, port those games instead and do only the cheap items (1, 2, 3, 5 below), which are also
upstreamable to nv30 for every NV4x card (test vehicle: the Core 2 + 7600 GT box with piglit).

## How the override works today (src)

- `MESA_GL_VERSION_OVERRIDE=3.3` (or `3.3FC` / `3.3COMPAT`) sets the screen's max core version
  before any extension check: `src/gallium/frontends/dri/dri_util.c:162-175` (api_mask gets
  `__DRI_API_OPENGL_CORE`), and `get_version` returns the override as is
  (`src/mesa/state_tracker/st_manager.c:1350-1360`). The context's version is overridden in
  `_mesa_initialize_context` (`src/mesa/main/context.c:1002`) before st computes one, and
  `_mesa_compute_version` then skips its clamp (`src/mesa/main/version.c:598-599`, `if
  (ctx->Version) goto done`). So a core 3.3 context is created on nv30.
- The GLSL version is not raised with it: the compiler accepts versions up to
  `ctx->Const.GLSLVersion` (`src/compiler/glsl/glsl_parser_extras.cpp:232-241`), which stays at the
  caps' 120 (`nv30_screen.c:102-103`) unless `MESA_GLSL_VERSION_OVERRIDE=330` is also set
  (`src/mesa/main/version.c:227-243`, called from `st_extensions.c:1216`).
- With the GLSL override, integers stay emulated: `NativeIntegers` is decided from the caps'
  version (local `GLSLVersion`, `st_extensions.c:1209-1213`, then `if (GLSLVersion >= 130)
  NativeIntegers = TRUE`, line 1284-1285), so it stays false; int uniforms are stored as floats
  (`src/mesa/main/uniform_query.cpp:987`, `UniformBooleanTrue = 1.0f` at `st_extensions.c:1356`),
  and nir_to_tgsi runs `nir_lower_int_to_float` (`src/gallium/auxiliary/nir/nir_to_tgsi.c:3982-3990`)
  because nv30 sets `.no_integers = true` (`nv30_screen.c:362`).
- GLES 3: `MESA_GLES_VERSION_OVERRIDE=3.0` is enough for ESSL 300 to parse
  (`glsl_parser_extras.cpp:249-253`, `_mesa_is_gles3_compatible`).
- driconf has no GL version option. It has `force_glsl_version` (only lowers, `st_extensions.c:1218-1220`),
  `force_compat_profile`, `allow_higher_compat_version` (`src/util/driconf.h:191-272`).
  `MESA_EXTENSION_OVERRIDE` force-enables single extensions (`src/mesa/main/extensions.c:95-202`).
- What the override alone does not do: it turns on nothing behind the version. Extensions stay
  off, `GL_MAX_*` stay at nv30's values, and features gated by version alone get through to a driver
  that can't run them (failure modes below).

What `compute_version` requires (`src/mesa/main/version.c:249-336`), and nv30's status:
- 3.0: GLSL 130, 4 colour attachments, MaxSamples >= 4 or FakeSWMSAA, ARB_color_buffer_float
  (compat only), ARB_depth_buffer_float, ARB_shader_texture_lod, ARB_texture_float, ARB_texture_rg,
  RGTC (have), EXT_draw_buffers2, ARB_framebuffer_object, EXT_framebuffer_sRGB, EXT_packed_float,
  EXT_texture_array, EXT_texture_integer, EXT_texture_shared_exponent, EXT_transform_feedback,
  NV_conditional_render (have). nv30 misses 13 of these.
- 3.1: GLSL 140, ARB_draw_instanced, ARB_uniform_buffer_object, EXT_texture_snorm,
  NV_primitive_restart (have), 16 vertex texture units.
- 3.2: GLSL 150, ARB_seamless_cube_map, ARB_texture_multisample (FakeSWMSAA counts,
  `st_extensions.c:1520-1527`), EXT_vertex_array_bgra; the rest present.
- 3.3: GLSL 330, ARB_blend_func_extended, ARB_instanced_arrays, ARB_shader_bit_encoding,
  ARB_texture_rgb10_a2ui, ARB_vertex_type_2_10_10_10_rev; explicit_attrib_location, timer query
  and texture swizzle are present.

## Feature table

Columns: RSX hardware (evidence) / Mesa nv30 now (why) / route and machinery / cost (S days,
M 1-2 weeks, L a month) and RSX risk / fidelity. Notes [n] follow the table.

| Feature | RSX / NV40 hardware | nv30 now | Route | Cost, risk | Fidelity |
|---|---|---|---|---|---|
| GLSL 1.30-3.30 syntax (in/out, layout, `#version 330 core`) | n/a | GLSL 1.20 (`nv30_screen.c:102`) | override; compiler already parses it (src above) | S (gate) | exact for syntax; what the shader *does* is the rows below |
| Core profile context | n/a | none (max core 0.0, glxinfo) | override already works (src above) | S | exact; dangerous without the rest [F1] |
| VAOs | n/a | ARB_vertex_array_object present (glxinfo) | nothing | 0 | exact |
| Sync (fences) | n/a | ARB_sync present (glxinfo) | nothing | 0 | exact |
| Primitive restart | hardware: 0x1dac/0x1db0 (RPCS3 `gcm_enums.h:719-720`, rnndb `nv30-40_3d.xml:637-638`) | on: `primitive_restart`, `_fixed_index` (`nv30_screen.c:139-141`), emitted in `nv30_vbo.c:796-818`, NV_primitive_restart in glxinfo; validator allows 0x1dac-0x1db8 (`validate.c:65`) | nothing | 0 | exact (indices > 65535 disable the 16-bit shortening, `nv30_vbo.c:800`) |
| Integer ops (+ - * / compare, i2f, min/max/abs) | none: FP32 ALUs only | `no_integers`; `nir_lower_int_to_float` covers iadd/isub/imul/idiv/ilt/ige/ieq/ine/ult/uge/iabs/ineg/imin/imax/umin/umax/i2f/f2i/b2i/bcsel (`src/compiler/nir/nir_lower_int_to_float.c:86-251`) | already runs under the override [1] | S to fill the gaps | exact up to 2^24; idiv via rcp is off by one near exact multiples [1] |
| `%`, udiv, shifts, bitwise, bitfield*, findMSB, bitCount | none | not in the pass: hits the `default:` assert, ntt emits MOD/UMOD/SHL/USHR/AND/OR/XOR/NOT (`nir_to_tgsi.c:1494-1565`), nvfx has no such opcodes and fails ("invalid opcode", `nvfx_fragprog.c:844-846`) | `%`, udiv, shift by constant, `& (2^n-1)`: float rewrites (S). General AND/OR/XOR: bit-serial loops, ~100+ instructions each (est) | S for the cheap ones; general bitwise not worth it | exact for the rewritten forms below 2^24; uint wraparound (hashes, RNGs) can never be exact |
| `floatBitsToInt/Uint`, `intBitsToFloat`, `uintBitsToFloat` | none (FP has PK*/UP* pack ops, RPCS3 `RSXFragmentProgram.h:182-191`, which pack into a float register) | ARB_shader_bit_encoding off | none general; packHalf2x16/packUnorm4x8 and unpacks could map to PK2/UP2/PKB/UPB in FS only (est) | M for the pack subset | bit casts: never |
| texelFetch (txf) | no texel-fetch op; TEX/TXL with normalized coords; RECT targets take unnormalized coords | TGSI TXF unsupported in nvfx (opcode list `nvfx_fragprog.c` cases) | NIR: `txf(p, lod)` -> `txl((p+0.5)/size(lod), lod)` with the size from driver constants (row below); RECT samplers: unnormalized directly [2] | M with textureSize | exact at texel centres while the sampler's LOD clamp/base level match; not for MSAA or buffer textures |
| textureSize (txs) | none | TXQ unsupported in nvfx | NIR pass: txs -> reserved constant slots the driver fills at validate from the bound views [2]; nothing like it in `nir_lower_tex` (options `src/compiler/nir/nir.h:6180-6420`) | M (shared with texelFetch) | exact |
| Texel offsets (textureOffset, texelFetchOffset) | none | min/max_texel_offset 0 (`nv30_screen.c:159-160`) | coordinate add of offset/size (needs txs) | S after txs | exact for nearest; linear filtering also exact (offset applied before sampling) |
| textureLod / textureGrad in FS (ARB_shader_texture_lod) | TXL 0x2F and TXD 0x19 in the FP ISA (RPCS3 `RSXFragmentProgram.h:185-188`, nvfx `nvfx_shader.h:263`) | TXL already translated (`nvfx_fragprog.c:756-758`) but cap off (`fragment_shader_texture_lod = false`, `nv30_screen.c:146`; `st_extensions.c:1079`); no TXD encoding | cap on; add TXD encoding (or `lower_txd` in nir_lower_tex, approximate) | S | exact with TXD |
| Derivatives (dFdx/dFdy/fwidth) | DDX 0x15/DDY 0x16 (`nvfx_shader.h:233-234`) | work in desktop GLSL already; cap only withholds GLES `OES_standard_derivatives` (`nv30_screen.c:147`) | `egl-deriv` test then cap on (backlog item) | S | coarse per-quad (as softpipe) |
| Instancing (ARB_instanced_arrays, draw_instanced) | per-attribute frequency divider: 0x1fc0 mask + VTXFMT bits 16-31 (RPCS3 `gcm_enums.h:737`, `:86-87`; RSX-SPU-FEASIBILITY s.4); no instance counter. GPU Gems 2 ch.30: "vertex stream frequency" | caps off (`nv30_screen.c:152-153`, `start_instance` 178); `instance_count` passed but drawn once (`nv30_vbo.c:802, 830`) | step 1: driver loop per instance, VTXBUF re-emit for divisor attributes; step 2: divider for list primitives [3] | M + M; step 2 needs 0x1fc0 in the validator allowlist (today `{0x1fc4,0x1fd8}`, `validate.c:69`) and 0x1fc0 = 0 whenever off (state shared between channels, RSX.md E5) | exact |
| gl_InstanceID | no register | `vs_instanceid = false` | loop: one VP constant per instance (~4 words); divider: an extra attribute reading a 0..65535 float buffer with DIVIDE = count | with instancing | exact |
| gl_VertexID | no register (RPCS3 VP inputs are the 16 attributes) | not supported (a VP using it fails translation and falls to the CPU draw path, `nv30_vertprog.c:81-85`) | static VRAM buffer of floats 0..65535 bound as a hidden attribute; for indexed draws the fetched element is the index, which is gl_VertexID's meaning [4] | S-M; costs one of 16 attribute slots | exact up to the buffer's length (256 KB holds 65,536 indices; 1 MB holds 262,144) |
| UBOs | no constant buffers: VP constants uploaded (0x1efc, 468 vec4 on NV40), FP constants live inside the program and are patched (our 0031) | `max_const_buffers = 1` (`nv30_screen.c:68, 83`) so `MaxUniformBlocks = 0` (`st_extensions.c:265-268`); `can_ubo` also needs 16 KB blocks and `indirect_const_addr` (`st_extensions.c:193-194, 324-328`), nv30 has 3584 B (`src/gallium/auxiliary/util/u_screen.c:169-170`) | flatten: NIR pass rewrites `load_ubo(k, off)` into constbuf 0 at a per-shader base; the driver copies the used ranges from the buffers at validate [5] | M-L; st needs a small patch to accept MaxUniformBlockSize < 16384 so oversize blocks fail at link instead of silently | exact while the used ranges fit 462 vec4 (VS) / ~224 vec4 (FS) minus default uniforms |
| Float textures (ARB_texture_float) | W16Z16Y16X16_FLOAT 0x9A, W32Z32Y32X32_FLOAT 0x9B, X32_FLOAT 0x9C, Y16_X16_FLOAT 0x9F (RPCS3 `gcm_enums.h:155-160`); fp16 filtered, fp32 unfiltered (GPU Gems 2 ch.30) | texture formats are in the table (`nv30_format.c:271-274`) but bindings are commented out (`//SBV_`, `//SRV_`, lines 120-130); ext needs RGBA16F and RGBA32F sampling (`st_extensions.c:840-845, 1746-1748`) | turn on S for RGBA16F/RG16F/R32F/RGBA32F; force nearest for fp32 in sampler validate; set `texture_half_float_linear` | S-M; big-endian texel order of fp16x4 untested (0012 fixed 2-byte texels only); fp32 + linear filter on the RSX UNVERIFIED (could be an exception: Mesa must never send it) | fp16 exact; fp32 linear sampled as nearest (visible on magnified fp32 data) |
| FP16/FP32 render targets | surfaces F_W16Z16Y16X16 11, F_W32Z32Y32X32 12, F_X32 13 (RPCS3 `gcm_enums.h:945-947`); fp16 blending yes, fp32 "don't support blending" (GPU Gems 2 ch.30) | RT entries exist (`nv30_format.c:150-152`), bindings off | bindings on: RGBA16F `SB`, R32F/RGBA32F `SR` (no BLENDABLE), driver forces blend off on fp32 | S-M; the validator checks RT_FORMAT only for 1-pixel swizzled (`validate.c:237`); add float formats to that check | fp16 exact; fp32 with blending requested: no blend (wrong if the game blends) |
| Packed float, RGB9E5 (R11G11B10F, shared exponent) | none | absent | st falls back to RGBA16F storage when the packed format is unsupported (est, from st_format.c fallback lists) | 0-S | exact values, 2x memory |
| RG textures (ARB_texture_rg) | textures B8 0x81, G8B8 0x8B (`gcm_enums.h:134, 142`); surfaces B8 9, G8B8 10 (`:943-944`; Mesa's header knows B8 only, `nv30-40_3d.xml.h:137`) | R8 and RG8 already sample through L8 / A8L8 with swizzles (`nv30_format.c:236, 258`); ext needs them renderable (`st_extensions.c:794-796`) | (a) render R8/RG8 into B8/G8B8 with a fragment-output swizzle variant [6], or (b) promote renderable R8/RG8 to BGRA8 storage (st option) | (a) M, RT format risk; (b) S, 4x/2x VRAM | (a) exact; (b) exact, more memory |
| ARB_framebuffer_object semantics | one RT_FORMAT for colour + zeta; swizzled surfaces share log2 width/height (RT_FORMAT bits 16-31, `nv30_state_validate.c` validate_fb); linear surfaces have own pitches | off: Mesa ties it to `mixed_framebuffer_sizes` (`st_extensions.c:1056`, cap false `nv30_screen.c:202`) | allow mixed sizes when every attachment is linear; make FBO-attachable miptrees linear (or re-check at validate and fall back to a linear copy) [7] | M; mis-sized swizzled targets mis-address memory (backlog "GL extensions the games worked around") | exact; MSAA renderbuffers stay off (below) |
| MRT | 4 targets, "must be the same format and size" (GPU Gems 2 ch.30; `CELL_GCM_MRT_MAXCOUNT = 4`, `gcm_enums.h:14`); per-MRT blend enable 0x36c and colour mask 0x370 (`gcm_enums.h:608-609`, rnndb `:249`) | 4 MRT present; `indep_blend_enable = false` (`nv30_screen.c:149`) although `nv30_state.c:64-88` already emits MRT_BLEND_ENABLE and MRT colour masks; `mixed_colorbuffer_formats = false` | turn on `indep_blend_enable` after a test (EXT_draw_buffers2) | S | exact; mixed-format G-buffers (RGBA8 + RGBA16F) are never possible: FBO incomplete (visible) |
| sRGB framebuffer (EXT_framebuffer_sRGB) | SET_SHADER_PACKER 0x1fec = sRGB output (RPCS3 `gcm_enums.h:746`, `rsx_decode.h:1718-1720`) | `dest_surface_srgb_control` unset; 0x1fec not in the validator allowlist | cap + 0x1fec emit + allowlist | S-M | conversion exact; blending happens on encoded values (DX9-class behaviour, est) |
| BGRA vertex colours (EXT_vertex_array_bgra) | no BGRA type; vertex type 0 raises a graph exception (RSX.md) | absent (`st_extensions.c:1015`) | VP variant that swizzles that input (.zyxw) | S-M | exact |
| Integer vertex attributes (glVertexAttribIPointer) | types: S16 normalized, F32, F16, U8 normalized, S16 scaled, CMP, U8 scaled (RPCS3 `gcm_enums.h:1143-1149`); no 32-bit int, no u16 | not exposed | 8-bit and s16 map to USCALED/SSCALED (shader ints are floats anyway); 32-bit and u16 need a cached CPU conversion | M | exact below 2^24 |
| 2_10_10_10_REV vertex | CMP is 11:11:10, not 10:10:10:2 | absent | CPU conversion (u_vbuf) or VP unpack (no bit ops: needs floor/fract arithmetic) | M | exact but slow on the CPU path |
| flat varyings | SHADE_MODEL is global (`nv30_state.c:144-146`); per-varying flat UNVERIFIED (assumed colour-only) | nvfx ignores interpolation qualifiers (no INTERP handling in `nvfx_fragprog.c`): `flat` silently becomes smooth | for ints: interpolate and `round()` in the FS; correct when the value is constant per primitive (material/instance IDs) | S | approximate: wrong where a flat value differs between a triangle's vertices |
| Vertex texture fetch | 4 units at 0x900 + 0x20n (RPCS3 `gcm_enums.h:627-634`); fp32 X32/W32Z32Y32X32 only, nearest (GPU Gems 2 ch.30 format table) | stub `nv40_verttex.c:31-52`, no VP TXL (`NV40_VP_INST_VEC_OP_TXL` 0x19 unused, `nvfx_shader.h:72`), 0 vertex samplers (glxinfo) | backlog item: validator format checks for 0x900-0x97c first (today only offset alignment, `validate.c:176-179`), then Mesa | M; exception risk | exact for fp32 2D nearest; other formats need an RGBA32F shadow copy (blit); GL 3.1 asks for 16 units, we have 4 |
| Texture arrays | none | `max_texture_array_layers = 0` (`nv30_screen.c:150`) | (a) split into N 2D textures + if/else ladder on the layer (16 FS units); (b) 3D texture, layer -> r at layer centres; (c) vertical atlas in a 2D texture with TXD for gradients [8] | M each | (a) exact for few layers, N fetches per lookup; (b) level 0 exact, mips blend neighbouring layers (wrong at distance); (c) seams at layer edges with linear filtering |
| Integer textures / RTs | none | EXT_texture_integer off (`st_extensions.c:1333`) | sample R8UI/R16UI as UNORM and scale; R32UI as R32F converted on upload | M-L | 8/16-bit sampling exact; integer render targets never |
| Buffer textures (TBO, GL 3.1) | none (`texture_buffer_objects = false`) | absent | copy into a 4096-wide 2D texture, `texelFetch(i)` -> `(i mod 4096, i div 4096)` | M | exact below 2^24 texels; a GPU copy per buffer update |
| MSAA FBOs / texture multisample | MS surfaces: CENTER_1, DIAGONAL_CENTERED_2, SQUARE_CENTERED_4, SQUARE_ROTATED_4 (RPCS3 `gcm_enums.h:931-934`) | `NV30_MAX_MSAA` defaults to 0 (`nv30_screen.c:507-509`), so no EXT_framebuffer_multisample; `texture_multisample = false` | `fake_sw_msaa` satisfies 3.0/3.2's checks (`st_extensions.c:1520-1527`); real MS FBO + resolve untested on the PS3 | S to fake; M+ to make real | faked: single-sampled (visible as no AA, which is fine for us) |
| Seamless cube maps | none on NV40 (est) | off | ignore | 0 | seams at cube edges with mip filtering (visible, small) |
| Depth float (ARB_depth_buffer_float) | 24-bit float depth bit (CELL_GCM_DEPTH_FORMAT_FLOAT, `gcm_enums.h:896`; `rsx_decode.h:4410`); no Z32F | absent | map Z32F to D24F with stencil (est) | M; untested format | approximate precision |
| Geometry shaders | none | none; under a 3.2+ override GL lets the shader through (`src/mesa/main/context.h:458-462`) and st calls `pipe->create_gs_state` (`src/mesa/state_tracker/st_program.c:554`), which nv30 does not set: NULL call (est: crash) | gallium draw module on the CPU, without LLVM on the PS3 (debian/rules:176-186): tgsi_exec, VS and GS both on the PPE [9] | L; per-vertex CPU | correct but 10-50 ms per 10k vertices (est): not for real time |
| Transform feedback | none | none | draw module stream output on the CPU, same cost | L | same |
| Compute | none | none | SPUs or render-to-texture passes only (HARDWARE-LEVERAGE.md) | n/a | never as GL compute |
| Dynamic loops (uniform counts) | VP: branches, 65,536 dynamic instructions; FP: LOOP/REP with counts in the instruction (GPU Gems 2 ch.30, RPCS3 `RSXFragmentProgram.h:122-129`) | unrolled by NIR, `max_unroll_iterations = 32` (`nv30_screen.c:360`); dynamic counts fail | out of scope here; noted because GL 3 shaders loop over light counts | L | n/a |

### Notes

[1] Integer emulation. Covered ops are listed in the table. Gaps and their float forms (est,
straightforward NIR): `imod/irem` -> `x - y*trunc(x/y)` (or floor for imod), `udiv` -> `floor(x/y)`,
`umod` -> `x - y*floor(x/y)`, `ishr/ushr` by constant n -> `floor(x * 2^-n)`, `ishl` by a variable ->
`x * exp2(n)` (`ishl` by a constant already becomes `imul`, `src/compiler/nir/nir_opt_algebraic.py:191`
under `lower_bitops`), `iand x, 2^n-1` -> mod, `inot` -> `-x-1`. idiv uses `trunc(x * rcp(y))`
(`nir_lower_int_to_float.c:169-177`); with fp32 rcp, `7/7` can come out as 0.99999 and truncate
to 0 (est, a +0.5ulp bias fixes it). Anything above 2^24 loses bits, and unsigned overflow never
wraps. In a vertex shader an unsupported op makes the VP fail translation, and the draw falls to
the CPU path (`nv30_vertprog.c:81-85`), where tgsi_exec runs real integer AND/XOR on float bit
patterns: silently wrong values (est). First step: run piglit's glsl-1.30/1.40/3.30 lists on the
7600 GT box with both overrides to find every op that reaches the assert.

[2] texelFetch/textureSize. nv30 compiles from NIR through `nir_to_tgsi` inside the driver
(`nv30_fragprog.c:205`, `nv30_vertprog.c:248`, `nv30_shader_cache.c:173`), so a driver NIR pass
before that call sees every txf/txs. The pass assigns one constant per (sampler, lod) used; at
validate the driver writes `max(size >> lod, 1)` from the bound sampler view. Vertex constants go
through the existing compare-and-upload (`nv30_vertprog.c:176-194`); fragment constants are
patched in the program copy (0031). The values change only when textures are rebound, so the CPU
cost is a compare per bound sampler per draw (est < 0.2 µs). texelFetch at the texel centre with
TXL returns the exact texel under nearest and under linear filtering (zero weights on neighbours),
unless the coordinate rounds (fine for sizes <= 4096 in fp32).

[3] Instancing. Step 1 (loop) costs per instance about 2 words per instanced attribute, ~6 for a
gl_InstanceID constant and 4-8 for the draw: 15-20 words, est 0.3-0.5 µs of PPE time per instance
(Mesa loop + push + ps3rsx validator), against est ~8 µs for a separate GL draw with its state (from
X-Moto: 1,920 fewer draws and ~18,300 fewer GL calls saved ~16 ms a frame, docs/games/README.md).
A 2,000-instance sprite batch would still cost ~1 ms a frame in the loop (est), which is why step 2
matters for Taisei-like games: one draw of `count x instances` vertices, per-vertex attributes
MODULO count, per-instance DIVIDE count x divisor, list primitives only, `count <= 65535`; indexed
draws need the index list replicated with `+ inst * (max_index+1)` (cache it per index buffer and
instance count: est 12 KB and ~10 µs to build for 6 indices x 1,000 instances, once). Interaction
with BASE_INDEX 0x173c is unverified. Raw-command probe first (rsx-tri variant, backlog).

[4] gl_VertexID. For non-indexed draws the buffer is bound at offset `start * 4`; for indexed draws
the RSX fetches element `index` (`RSXVertexFetch.glsl` in RPCS3, RSX-SPU-FEASIBILITY s.4), which is
gl_VertexID (plus basevertex, which BASE_INDEX already adds). 256 KB of VRAM for 65,536 floats.
No per-draw CPU cost beyond one VTXBUF/VTXFMT pair when the hidden attribute is first bound.

[5] UBO flattening. st already turns default uniforms into UBO 0 (`lower_uniforms_to_ubo = true`,
`nv30_screen.c:358`; `st_glsl_to_nir.cpp:723-725`) and, because nv30 lacks `indirect_const_addr`,
lowers indirect UBO derefs into if/else trees (`st_glsl_to_nir.cpp:535-546`), so every UBO load
has a constant offset by the time the driver sees it. A UBO buffer created on GL_UNIFORM_BUFFER
has `PIPE_BIND_CONSTANT_BUFFER`, which is in neither nouveau's vidmem nor sysmem bindings
(`nouveau_screen.c:460-469`, nv30 adds only vertex/index, `nv30_screen.c:580-585`), so it lands in
plain CPU memory (`nouveau_buffer.c:729-757`): the driver can read it at memcpy speed. A buffer
first created on another target and later bound as a UBO may sit in VRAM, where the PPE reads at
10 MB/s (RSX.md): it needs a CPU shadow (est S extra). Per draw where a UBO changed: memcpy of the
used range plus, for the VS, 4 words per changed vec4 through 0x1efc (0025 uploads consecutive
runs); for the FS, a program-copy patch per changed constant (0031). A per-frame camera block of
16 vec4 costs est ~1-2 µs a draw it changes on; per-object blocks updated every draw cost about
what glUniform does today. Limits: VS 462 vec4, FS 224 vec4 shared with default uniforms and the
txs constants; GL 3.1 promises 12 blocks of 16 KB each per stage, which is a lie we should not tell
(see F3).

[6] R8/RG8 rendering. B8 stores one channel and G8B8 two; which shader output component lands
where, and the byte order against GL's R-then-G (A8L8 sampling needed 0012's swap), are
UNVERIFIED. The fragment program would need an output-swizzle variant keyed on the bound RT
format (nv30 has no FP variant key today), and blend/colour mask swizzled the same way. Option (b)
(BGRA8 storage when an R8/RG8 texture may be rendered) is a few lines in `st_format.c:1416-1440`
behind an option, exact, and costs 4x (R8) or 2x (RG8) VRAM, which matters on our 256 MB.

[7] Mixed-size FBOs. `nv30_validate_fb` writes one RT_FORMAT with log2 width/height only for
swizzled targets; linear targets carry their own pitch. Rendering clips to the smallest
attachment, which is what ARB_fbo specifies. The work is choosing linear layout for anything that
may be attached with a different-size partner (or every FBO texture: linear costs texture-cache
locality, est small at our CPU-bound frame rates) and a validator check that refuses a swizzled
RT_FORMAT whose log2 size does not match the bound surfaces' allocations.

[8] Texture arrays. (b) 3D: max 512 deep (glxinfo `GL_MAX_3D_TEXTURE_SIZE = 512`), 3D mip levels
halve the depth too, so level k averages 2^k layers. 3D textures are swizzled and cannot be render
targets (`nv30_screen.c:318-319`), so layer rendering needs a copy. (c) atlas: 4096 tall, so
16 layers of 256x256; wrap needs `fract()` per layer, and TXD with the unwrapped gradients keeps the
mip choice right; linear filtering still bleeds half a texel across layer seams. (a) is the only
exact one and is fine when a material array has <= ~8 layers and the index is uniform per draw
(a compile-time constant index folds to one fetch; a uniform or per-pixel index costs N fetches,
est).

[9] GS/TF through the draw module. nv30 already falls back to draw for vertex programs it cannot
translate (`nv30_render_vbo`, `nv30_draw.c:382-451`; draw created at `nv30_draw.c:509-530`). Draw
can run a GS and stream output, but our PS3 Mesa is built without LLVM (`debian/rules:176-186`), so
both VS and GS run in tgsi_exec on the PPE. Est: 1-5 µs per vertex for a 50-instruction VS+GS on
the PPE, so 10k vertices = 10-50 ms a frame. Usable only for a loading-time or tiny effect (a few
hundred particles), and the PPE is already the limit (driver stack 30-55%).

## Failure modes: where an override makes a game misbehave silently

F1. **Version without features.** With only the env overrides, a game that checks `GL_VERSION >= 3.3`
then: draws instanced geometry once (`glDrawArraysInstanced` is dispatched in every desktop context,
`src/mesa/main/draw.c:504-519` checks no extension; nv30 ignores `instance_count`); gets fragment
programs that fail to translate (txf, txs, bitwise: "invalid opcode", `nvfx_fragprog.c:844-846`),
after which the previous program stays bound (brief, RSX.md) with no GL error, because gallium
compiles at bind/validate time and cannot fail the link; gets wrong vertex results on the CPU
fallback path [1]; crashes on the first geometry shader (est, [9]); and sees `flat` silently smooth.
So the plain env overrides should never be shipped for a game; only the gated profile below.

F2. **Integer semantics.** Shaders that hash with uint multiply/xor (noise, dithering, random
sprites) compile into something that does not wrap: patterns repeat or go flat, no error. Values
above 16,777,216 (packed IDs, bit masks) lose low bits.

F3. **Fake limits.** Games that size arrays from `GL_MAX_UNIFORM_BLOCK_SIZE`,
`GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS`, `GL_MAX_ARRAY_TEXTURE_LAYERS`, `GL_MAX_SAMPLES`,
`GL_MAX_VARYING_COMPONENTS` or `GL_MAX_DRAW_BUFFERS` and get GL 3's minimums (16 KB, 16, 256, 4,
64, 8) will build shaders or FBOs the RSX cannot run. The profile must report the true numbers
(16 KB is not true: ~3.5 KB in the FS), and for that st needs a small patch so `can_ubo` and the
version logic accept smaller values; then oversize shaders fail at link, where games notice.

F4. **Formats.** Unsupported internal formats (RGBA16UI, RGB10_A2UI, Z32F, mixed-format MRT) give
incomplete textures (black) or FRAMEBUFFER_UNSUPPORTED; many GL 3 games never check FBO status.
fp32 targets with blending requested render without blending.

F5. **Vertex texture formats.** A non-fp32 texture bound to a vertex sampler must be converted or
refused in Mesa; sent as is, an unknown format could raise a graph exception, and on the RSX that
wedges the GPU until a console reboot (RSX.md). Same for fp32 + linear filtering until tested.

## Gating

- A driconf option in our Mesa, e.g. `nv30_gl3_subset` (bool), set per executable in a drirc file
  shipped with each game's package (`<application executable="taisei">`), plus env
  `NV30_GL3_SUBSET=1/0` that wins over driconf for testing and opt-out. `get_version` in
  `st_manager.c:1350` and the screen's version query in `dri_util.c:162-170` are where it plugs
  in, the same places the env overrides use; driconf options are already passed to
  `st_init_extensions` (`st_extensions.c:1218` reads `options->force_glsl_version`).
- With the option on: report 3.3 core and compat, GLSL 330, the emulated caps on, true limits.
  With it off: exactly today's 2.1.
- Every emulation that can be inexact logs once per shader to stderr, with the game named
  (`nv30-gl3: shader 5e62b74b: uint multiply (no wraparound)`), and a hard failure (fragment program
  not translated) logs loudly every time, so a census run shows what a game hits.
- Unsupported stages (GS, tessellation, compute) must fail cleanly: return 0 from
  glCreateShader with GL_INVALID_ENUM by keeping `_mesa_has_geometry_shaders` false under the
  profile (a st/main patch), never a NULL `create_gs_state` call.
- Do not set `MESA_GL_VERSION_OVERRIDE` in launchers or game mode; keep it a developer tool.

## Minimum honest "GL 3.3 subset" profile

Covers: GLSL 330 syntax, core and compat contexts, VAOs, sync, primitive restart, small-int
arithmetic (+ - * / % compare, shifts by constants, low-bit masks), texelFetch, textureSize, texel
offsets, instancing (divisor and instanced draws) with gl_InstanceID, gl_VertexID, UBOs up to the
real constant space, R8/RG8 textures and render targets, FP16 (blended, filtered) and FP32
(unblended, nearest) textures and render targets, packed-float formats as FP16, mixed-size FBO
attachments, 4 same-format MRT with per-target blend enable, derivatives, textureLod/textureGrad
in fragment shaders, 4 vertex texture units (fp32 nearest), flat ints via round.

Never covers: general bitwise ops and bit casts, uint wraparound, integer textures and render
targets (8/16-bit sampling at most), geometry shaders and transform feedback at game rates,
compute, texture arrays with mipmaps (exact only for a few layers via split textures), MSAA FBO
resolve (until tested; faked as single-sample), texture multisample, seamless cubes, mixed-format
MRT, more than 8 vec4 varyings (nv30 `max_inputs = 8`, `nv30_screen.c:79`, comment "should be
possible to do 10"), 16 vertex texture units, dynamic loop counts in fragment shaders, fp32
blending.

### Ordered work items (sizes est)

1. **Gate and honesty plumbing** (S): driconf/env option, version and GLSL 330 under it, true
   limits (st patch for small UBO sizes and the version checks), GS/TF/compute refused cleanly,
   the once-per-shader log. No RSX risk.
2. **GLSL 1.30+ maths** (S-M): extend `nir_lower_int_to_float` ([1]), idiv bias, `round()` for flat
   ints; piglit glsl-1.30/1.40/3.30 on the 7600 GT box (upstreamable for all no-integer drivers).
   Compile-time only: no CPU cost per draw; GPU cost a few instructions per emulated op.
3. **Derivatives cap and ARB_shader_texture_lod** (S): `egl-deriv` test, TXD encoding 0x19.
   Compile-time only.
4. **texelFetch, textureSize, texel offsets** (M): driver NIR pass before `nir_to_tgsi`, constant
   slots filled at validate. CPU: est < 0.2 µs per draw (compares), more only when textures change.
5. **Float textures and render targets** (S-M): format bindings on, fp32 forced nearest and
   unblended, big-endian texel test (extend `gl-texfmt`), validator: allow only known RT and texture
   formats. VRAM: an RGBA16F 1080p target is 16.6 MB, RGBA32F 33 MB, real memory here (VRAM is swap).
6. **Instancing step 1 with gl_InstanceID** (M): loop in `nv30_draw_vbo`, behind the profile;
   `egl-instanced` against softpipe. CPU: est 0.3-0.5 µs per instance.
7. **gl_VertexID** (S-M): hidden attribute buffer. CPU: none per draw.
8. **R8/RG8** (S for BGRA8 promotion; M for B8/G8B8 surfaces, after a raw-command probe).
9. **Independent blend enable** (S): cap on, test (code exists in `nv30_state.c:64-88`).
10. **UBO flattening** (M-L): NIR pass + validate-time copy + CPU shadow for VRAM buffers. CPU: est
    1-2 µs per draw on which a used block changed.
11. **ARB_framebuffer_object** (M): linear FBO attachments, validator size check.
12. **Instancing step 2, the divider** (M): rsx-tri probe first, 0x1fc0 in the validator, cached
    replicated index lists. CPU: one draw per batch.
13. Later, only if a census game needs it: vertex texture fetch (M, validator first), BGRA vertex
    arrays (S-M), integer vertex attributes (M), sRGB framebuffers via 0x1fec (S-M), buffer textures
    (M), texture arrays by split textures (M).

Items 1-10 are est 7-9 weeks for one person; 1-5 alone (3-4 weeks) already let a game with GLSL 330
shaders, VAOs and float/R8 targets run without its renderer touched, if it doesn't instance or use
UBOs. Every item that emits new methods or formats (5, 8, 11, 12, 13) needs its validator check in
ps3rsx before it runs on the PS3, because one bad push wedges the RSX until a reboot.

## Sources

- Mesa 26.2.4 + our patches: `src/gallium/drivers/nouveau/nv30/nv30_screen.c`, `nv30_format.c`,
  `nv30_vbo.c`, `nv30_state.c`, `nv30_state_validate.c`, `nv30_vertprog.c`, `nv30_fragprog.c`,
  `nvfx_fragprog.c`, `nvfx_vertprog.c`, `nvfx_shader.h`, `nv30_draw.c`, `nv40_verttex.c`,
  `nouveau_buffer.c`, `nouveau_screen.c`; `src/mesa/main/version.c`, `context.c`, `context.h`,
  `draw.c`, `extensions.c`; `src/mesa/state_tracker/st_extensions.c`, `st_manager.c`,
  `st_glsl_to_nir.cpp`, `st_format.c`, `st_program.c`; `src/gallium/frontends/dri/dri_util.c`;
  `src/compiler/nir/nir_lower_int_to_float.c`, `nir_opt_algebraic.py`, `nir.h` (nir_lower_tex
  options); `src/gallium/auxiliary/nir/nir_to_tgsi.c`; `src/util/driconf.h`; `debian/rules`.
- RPCS3 master (2026-10-19): `rpcs3/Emu/RSX/gcm_enums.h`, `rsx_decode.h`, `rsx_methods.h`,
  `Program/RSXFragmentProgram.h`, `Program/RSXVertexProgram.h`, `Program/VertexProgramDecompiler.cpp`.
- envytools master: `rnndb/graph/nv30-40_3d.xml`.
- ps3rsx: `ps3rsx/src/validate.c` (allowlist lines 43-70, data checks 176-250).
- NVIDIA, GPU Gems 2 ch. 30, "The GeForce 6 Series GPU Architecture" (fp16 filtering and blending,
  fp32 unblended/unfiltered, 4 vertex textures fp32 nearest, MRT same format and size, stream
  frequency, branching costs).
- Our docs: RSX.md, MESA-METHODS.md, research/RSX-SPU-FEASIBILITY.md s.4, backlog/graphics.md,
  research/HARDWARE-LEVERAGE.md, glxinfo-nv47.txt, games/README.md; census trace
  `$B/traces/taisei/c2/report.md`.
