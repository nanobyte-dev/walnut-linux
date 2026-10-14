# E12 / E13: triangle and textured quad on the RSX from raw FIFO words

Status: desk research, not yet run on hardware. Every method and every program
word below is taken from, or cross-checked against, at least two of the sources
in section 0. The ranked list of what could still be wrong is in section C.

Conventions used throughout:

- Header = `(count<<18) | (subch<<13) | method`, subchannel 0 (3D object
  0x31337000, class 0x4097). A non-incrementing header additionally sets bit 30
  (`0x40000000`, PSL1GHT `RSX_METHOD_FLAG_NO_INCREMENT`).
- All words are native big-endian u32 as the PPU writes them, same as the clear
  that already works.
- `FP_OFF`, `FPT_OFF`, `TEX_OFF`, `VB_OFF` are VRAM offsets (DMA 0xfeed0000)
  you pick in free VRAM past the scanout buffer (1920*1080*4 = 0x7E9000 bytes).
  Suggested: `FP_OFF = 0x00800000`, `FPT_OFF = 0x00800100`,
  `TEX_OFF = 0x00801000`, `VB_OFF = 0x00802000`. Alignment: fragment programs
  256 bytes (PSL1GHT uses 64, nv40.h declares the 0x8e4 offset field as
  `0xffffff00`, so 256 satisfies both), textures 128 (rsxtest), vertex buffers 128.

## 0. Sources (all fetched as raw trees on 2026-10-14)

| Tag | Source | Revision |
|---|---|---|
| PSL | github.com/ps3dev/PSL1GHT: `ppu/librsx/commands_impl.h`, `ppu/include/rsx/nv40.h`, `ppu/include/rsx/gcm_sys.h`, `samples/graphics/rsxtest/source/main.cpp`, `tools/cgcomp/source/*.cpp` | f649a08 |
| RP | github.com/RPCS3/rpcs3: `rpcs3/Emu/RSX/gcm_enums.h`, `rsx_methods.cpp`, `rsx_decode.h`, `RSXTexture.cpp`, `Program/RSXVertexProgram.h`, `Program/RSXFragmentProgram.h`, `Program/Assembler/FPToCFG.cpp`, `Program/ProgramStateCache.cpp`, `Core/RSXDrawCommands.cpp`, `NV47/HW/common.cpp` | eda7621 |
| MESA | gitlab.freedesktop.org/mesa/mesa: `src/gallium/drivers/nouveau/nv30/{nvfx_shader.h,nv40_vertprog.h,nvfx_vertprog.c,nvfx_fragprog.c,nv30_fragprog.c,nv30_vertprog.c,nv30_screen.c,nv30_state_validate.c,nv30_draw.c,nv30-40_3d.xml.h}` | 68772a4 |
| ENVY | github.com/envytools/envytools: `rnndb/graph/nv30-40_3d.xml` | HEAD |
| PS3FB | our `build/kernel/src/drivers/video/fbdev/ps3fb.c` `fifo_program[]` (decoded into method writes) | 6.12.112 |

How the program words were produced and checked:

1. **Assembled** with PSL1GHT's own `cgcomp` assembler (the tool homebrew uses
   to make every PSL1GHT shader), built natively from `tools/cgcomp/source`
   and driven through its `-a` (assembly input) code path, with a tiny driver
   that prints `compiler.GetInstructions()` before cgcomp's file-endian swaps.
   cgcomp's encoder is a port of Mesa's `nvfx_vertprog.c` / `nvfx_fragprog.c`.
2. **Re-encoded independently** in Python from the Mesa field definitions
   (`nv40_vertprog.h`, `nvfx_shader.h`). Result: bit-identical to cgcomp for all
   five programs.
3. **Decoded** with the RPCS3 bitfield layouts (`D0..D3` in
   `RSXVertexProgram.h`, `OPDEST/SRC0/SRC1/SRC2` in `RSXFragmentProgram.h`).
   All fields decode to the intended instruction (tables in A.3 / A.4 / B.3).

### 0.1 PS3FB reset state we rely on (not re-sent)

Decoded from `fifo_program[]`; RPCS3 `rsx_methods.cpp` (around line 440-600)
initialises the same values as "realhw" defaults, which is a second witness:

| Method | Value | Meaning |
|---|---|---|
| 0x184 / 0x188 | 0xfeed0000 / 0xfeed0001 | DMA_TEXTURE A (VRAM) / B (XDR). Used by texture FORMAT bits 0-1 **and** by SHADER_PROGRAM bits 0-1 |
| 0x19c / 0x1a0 | 0xfeed0000 / 0xfeed0001 | DMA_VERTEX A / B (vertex arrays, bit 31 of 0x1680+) |
| 0x1e94 | 0x00000011 | TRANSFORM_EXECUTION_MODE ("ENGINE"); Mesa NV40 also writes 0x11 for programmable VP (`nv30_vertprog.c:217`). Leave it. |
| 0x1fc4..0x1fd4 | 06144321 edcba987 0000006f 00171615 001b1a19 | VP output -> FP input routing; identical to Mesa `nv30_screen.c` NV40 init. Leave it. |
| 0x1740+4i | 0x00000002 | VERTEX_DATA_ARRAY_FORMAT: F32, size 0 = array disabled, attribute comes from the immediate registers |
| 0x1d78 | 1 | ZMIN_MAX_CONTROL (cull near/far). This is **not** the viewport; leave it. |
| 0x1a0c+32i | 0x00060000 | TEXTURE_CONTROL0: units disabled, maxlod 12 |
| 0x1a08 / 0x1a14 | 0x00030101 / 0x02052000 | texture address / filter defaults |
| 0x0b40.. | 0 | TEX_COORD_CONTROL |
| 0x02c0.. | 0x0fff0000 | WINDOW_CLIP (0..4095), fine for 1920x1080 |
| 0x0a74, 0x0310, 0x183c, 0x0304, 0x0348 | 0 | depth test, blend, cull, alpha test, stencil all off already |
| 0x1e98 | 0x01000000 | RENDER_ENABLE (on) |
| 0x1fe8 | 0 | NO_PARANOID_TEXTURE_FETCHES = 0, i.e. paranoid (bounds-checked) fetches on. Keep. |

ps3fb does **not** set 0x1d60 (SHADER_CONTROL), 0x8e4 (SHADER_PROGRAM), 0x1ef8
(TRANSFORM_TIMEOUT) or any vertex program, so those must be sent.

## A. Triangle (E12)

### A.1 Command stream (after your working surface setup)

Ready to paste into a `u32[]`. Order matters only in that the program/state
writes precede `BEGIN_END`.

```c
/* A.1.1 render state: mostly restating ps3fb defaults so the test is self-contained */
0x00040a74, 0x00000000,             /* DEPTH_TEST_ENABLE = 0            PSL SetDepthTestEnable, RP 0xa74 */
0x00040310, 0x00000000,             /* BLEND_ENABLE = 0                 PSL SetBlendEnable,     RP 0x310 */
0x0004183c, 0x00000000,             /* CULL_FACE_ENABLE = 0             PSL SetCullFaceEnable,  RP 0x183c */
0x00040304, 0x00000000,             /* ALPHA_TEST_ENABLE = 0            PSL nv40.h 0x304 */
0x00040348, 0x00000000,             /* STENCIL_TEST_ENABLE (front) = 0  PSL nv40.h 0x348 */
0x00040374, 0x00000000,             /* LOGIC_OP_ENABLE = 0              PSL nv40.h 0x374 */
0x00040368, 0x00001d01,             /* SHADE_MODE = SMOOTH (0x1d01)     PSL SetShadeModel, gcm_sys.h GCM_SHADE_MODEL_SMOOTH */
0x00081828, 0x00001b02, 0x00001b02, /* FRONT/BACK_POLYGON_MODE = FILL   PSL nv40.h 0x1828/0x182c */
0x00041d88, 0x00001438,             /* SHADER_WINDOW = (1<<12)|1080     PSL SetSurface last word; RP: only affects WPOS */

/* A.1.2 viewport (PSL SetViewport, values as rsxtest setDrawEnv) */
0x00080a00, 0x07800000, 0x04380000, /* VIEWPORT_HORIZONTAL = 1920<<16|0, VERTICAL = 1080<<16|0 */
0x00080394, 0x00000000, 0x3f800000, /* CLIP_MIN = 0.0f, CLIP_MAX = 1.0f (depth range) */
0x00200a20,                         /* VIEWPORT_OFFSET[0..3], VIEWPORT_SCALE[0..3] */
    0x44700000, 0x44070000, 0x3f000000, 0x00000000,  /* offset = 960, 540, 0.5, 0 */
    0x44700000, 0xc4070000, 0x3f000000, 0x00000000,  /* scale  = 960, -540, 0.5, 0 */
0x00200a20,                         /* identical block again: PSL SetViewport emits it twice; RP rsx_methods.cpp:490 "Realhw emits this sequence twice" */
    0x44700000, 0x44070000, 0x3f000000, 0x00000000,
    0x44700000, 0xc4070000, 0x3f000000, 0x00000000,

/* A.1.3 vertex program (PSL LoadVertexProgramBlock) */
0x00081e9c, 0x00000000, 0x00000000, /* TRANSFORM_PROGRAM_LOAD = 0, TRANSFORM_PROGRAM_START = 0 */
0x00200b80,                         /* TRANSFORM_PROGRAM[0..7] = 2 instructions */
    0x401f9c6c, 0x0040000d, 0x8106c083, 0x6041ff80,  /* MOV o[HPOS], v[0]       */
    0x401f9c6c, 0x0040030d, 0x8106c083, 0x6041ff85,  /* MOV o[COL0], v[3]  END  */
0x00041ff0, 0x00000009,             /* VERTEX_ATTRIB_INPUT_MASK  = (1<<0)|(1<<3) */
0x00041ff4, 0x00000025,             /* VERTEX_ATTRIB_OUTPUT_MASK = FRONTDIFFUSE|BACKDIFFUSE|POINTSIZE */
0x00041ef8, 0x0020ffff,             /* TRANSFORM_TIMEOUT (PSL: <=32 regs; Mesa nv30_screen.c NV40 init same value) */

/* A.1.4 fragment program select (PSL LoadFragmentProgramLocation) */
0x000408e4, FP_OFF | 1,             /* SHADER_PROGRAM = offset | (GCM_LOCATION_RSX + 1) -> DMA A = VRAM */
0x00041d60, 0x02000440,             /* SHADER_CONTROL = 2<<24 (temp regs) | 1<<10 | 0x40 (R0 is fp32 output) */

/* A.1.5 immediate-mode draw: per vertex, colour first, position LAST (writing attr 0 emits the vertex) */
0x00041808, 0x00000005,             /* BEGIN_END = TRIANGLES (GCM_TYPE_TRIANGLES = 5) */
0x00101c30, 0x3f800000, 0x00000000, 0x00000000, 0x3f800000, /* VERTEX_DATA4F_M[3] = (1,0,0,1) red   */
0x00101c00, 0x00000000, 0x3f000000, 0x00000000, 0x3f800000, /* VERTEX_DATA4F_M[0] = (0, 0.5, 0, 1)  */
0x00101c30, 0x00000000, 0x3f800000, 0x00000000, 0x3f800000, /* (0,1,0,1) green */
0x00101c00, 0xbf000000, 0xbf000000, 0x00000000, 0x3f800000, /* (-0.5,-0.5, 0, 1) */
0x00101c30, 0x00000000, 0x00000000, 0x3f800000, 0x3f800000, /* (0,0,1,1) blue */
0x00101c00, 0x3f000000, 0xbf000000, 0x00000000, 0x3f800000, /* (0.5,-0.5, 0, 1) */
0x00041808, 0x00000000,             /* BEGIN_END = STOP */
```

Expected image: a smooth-shaded triangle, apex at pixel (960, 270), base from
(480, 810) to (1440, 810), red at the top, green bottom-left, blue bottom-right.
Window y = 540 - 540*ndc_y because scale_y = -540 (rsxtest uses `h*-0.5f`).
If it shows upside down, only the sign of scale_y is off; nothing else changes.

### A.2 Fragment program images in VRAM

Write these u32 values with plain big-endian stores (e.g. `volatile u32 *p =
vram + FP_OFF/4; p[i] = img[i];`), then barrier (`eieio`/`sync`) before you
bump PUT.

```c
/* FP_COL: MOV R0, f[COL0]  (result.color = fragment.color), 1 instruction */
static const u32 fp_col[] = {
    0x3e010100, 0xc8011c9d, 0xc8000001, 0xc8000001,
    0x00010001, 0x00000000, 0x00000000, 0x00000000,  /* pad: NOP+END, see note */
};
/* FP_CONST: MOV R0, {1,0,1,1}  (solid magenta, no varyings at all), 1 instruction + inline constant */
static const u32 fp_const[] = {
    0x1e010100, 0xc8021c9d, 0xc8000001, 0xc8000001,
    0x00003f80, 0x00000000, 0x00003f80, 0x00003f80,  /* constant (1.0, 0.0, 1.0, 1.0), also halfword-swapped */
    0x00010001, 0x00000000, 0x00000000, 0x00000000,  /* pad */
};
```

SHADER_CONTROL is 0x02000440 for both (cgcomp reports `fp_control=0x40`,
`num_regs=2` for each; PSL adds `1<<10` and clamps regs to >= 2).

Pad note: Mesa appends a NOP with only the END bit after every fragment program
(`nvfx_fragprog.c`, "Append NOP + END instruction for branches to the end of the
program"). libgcm/cgcomp do not. The word `0x00010001` is NOP+END in **both**
halfword orders (logical 0x00000001 swapped is 0x00010000; the extra bit 16 is
the input-attribute field, ignored by NOP), so it is a harmless backstop. It is
never executed when the program is read correctly.

#### Halfword-swap rule (the BE-CPU question)

The RSX fetches fragment-program words with the two 16-bit halves of each u32
exchanged. For a logical instruction word `W` (as written in Mesa/cgcomp/RPCS3
bitfield terms), the PPU must store:

```c
static inline u32 fp_word(u32 W) { return (W << 16) | (W >> 16); }   /* bytes in memory: W[15:8] W[7:0] W[31:24] W[23:16] */
```

The same applies to the 4 inline-constant words that follow an instruction
using a constant. Vertex-program words are **not** swapped; they go into the
FIFO as-is.

Evidence, three independent paths that agree:

1. PSL cgcomp `main.cpp`: on a big-endian host `SWAP32` is identity and each
   fragment word is stored as `endian_fp(W) = (W>>16)|(W<<16)`; on a
   little-endian host it is `endian_fp(bswap32(W))`, which lands in the file as
   bytes `[W1 W0 W3 W2]` (W3 = MSB), the same bytes. rsxtest just `memcpy`s that
   ucode into RSX memory. PSL `SetFragmentProgramParameter` uses
   `swapF32_16()` on constant values for the same reason.
2. Mesa `nv30_fragprog.c` `nv30_fragprog_upload`: on `UTIL_ARCH_BIG_ENDIAN` it
   writes `(insn >> 16) | (insn << 16)` per word; little-endian hosts write
   `insn` unchanged. (NV40 on big-endian PowerPC behaves like the RSX here.)
3. RPCS3 reads guest memory as little-endian u32 then applies
   `OPDEST::from_be32` (`((x & 0x00FF00FF) << 8) | ((x & 0xFF00FF00) >> 8)`;
   `FPToCFG.cpp`: "Fixup of RSX's weird half-word shuffle for FP
   instructions"), and `ProgramStateCache.cpp` finds the END bit at
   `(raw_le >> 8) & 1` and the opcode at `(raw_le >> 16) & 0x3f`. Running our
   memory bytes through that path returns the logical words exactly (checked
   in `verify.py`).

### A.3 Vertex program derivation (NV40 VP, 128 bits = D0..D3)

Assembly fed to cgcomp (`!!VP2.0`): `MOV o[HPOS], v[0]; MOV o[COL0], v[3]; END`.
cgcomp reports `in_mask=0x9`, `out_mask=0x5`.

Unused sources are encoded as `INPUT` type with identity swizzle (Mesa
`nvfx_vertprog.c` and cgcomp both do this). One source selector `S` (17 bits,
Mesa `NV40_VP_SRC_*`, RP `union SRC`): `type[1:0] | tmp[7:2] | swzW[9:8] |
swzZ[11:10] | swzY[13:12] | swzX[15:14] | neg[16]`. For `v[n].xyzw`:
type 2 (INPUT), swizzle X=0,Y=1,Z=2,W=3 -> `S = 2 | (3<<8)|(2<<10)|(1<<12)|(0<<14) = 0x1b02`.
It is split as SRC0 = `D1[7:0]` (S>>9 = 0x0d) + `D2[31:23]` (S&0x1ff = 0x102),
SRC1 = `D2[22:6]`, SRC2 = `D2[5:0]` (S>>11 = 3) + `D3[31:21]` (S&0x7ff = 0x302).

| Word | Field (Mesa name / RP name) | Bits | Insn 0 (HPOS) | Insn 1 (COL0) |
|---|---|---|---|---|
| D0 | VEC_RESULT / vec_result (write to output, not temp) | 30 | 1 | 1 |
| D0 | VEC_DEST_TEMP / dst_tmp (0x3f = none) | 20:15 | 0x3f | 0x3f |
| D0 | COND_TEST_ENABLE, COND_UPDATE | 13, 14 | 0, 0 | 0, 0 |
| D0 | COND / cond (7 = TR, always) | 12:10 | 7 | 7 |
| D0 | COND_SWZ x,y,z,w | 9:8,7:6,5:4,3:2 | 0,1,2,3 | 0,1,2,3 |
| **D0** | | | **0x401f9c6c** | **0x401f9c6c** |
| D1 | SCA_OPCODE (0 = NOP) | 31:27 | 0 | 0 |
| D1 | VEC_OPCODE (1 = MOV) | 26:22 | 1 | 1 |
| D1 | CONST_SRC | 21:12 | 0 | 0 |
| D1 | INPUT_SRC (v[n]) | 11:8 | 0 (POS) | 3 (COL0) |
| D1 | SRC0H | 7:0 | 0x0d | 0x0d |
| **D1** | | | **0x0040000d** | **0x0040030d** |
| D2 | SRC0L / SRC1 / SRC2H | 31:23 / 22:6 / 5:0 | 0x102 / 0x1b02 / 3 | same |
| **D2** | | | **0x8106c083** | **0x8106c083** |
| D3 | SRC2L | 31:21 | 0x302 | 0x302 |
| D3 | SCA_WRITEMASK | 20:17 | 0 | 0 |
| D3 | VEC_WRITEMASK x,y,z,w | 16,15,14,13 | 1111 | 1111 |
| D3 | SCA_DEST_TEMP (0x3f = none) | 12:7 | 0x3f | 0x3f |
| D3 | DEST (output index: 0 POS, 1 COL0, 7 TC0) | 6:2 | 0 | 1 |
| D3 | INDEX_CONST | 1 | 0 | 0 |
| D3 | LAST / end | 0 | 0 | **1** |
| **D3** | | | **0x6041ff80** | **0x6041ff85** |

Masks: VERTEX_ATTRIB_INPUT_MASK bit n = v[n] used (Mesa `vp->ir |= 1<<index`):
`0x9`. VERTEX_ATTRIB_OUTPUT_MASK: COL0 sets FRONTDIFFUSE (bit 0) and
BACKDIFFUSE (bit 2) in cgcomp (`compilervp.cpp:353-354`, Mesa sets bit 0 for
COL0), PSL `LoadVertexProgramBlock` always ORs `GCM_ATTRIB_OUTPUT_MASK_POINTSIZE`
(bit 5): `0x5 | 0x20 = 0x25`. HPOS has no bit.

### A.4 Fragment program derivation (NV40 FP, logical words before the swap)

`!!ARBfp1.0  MOV result.color, fragment.color; END` (identical words from
`!!FP2.0 MOVR R0, f[COL0];`, but only the ARB form makes cgcomp set the 0x40
"R0 is the fp32 output" control bit; RP `CELL_GCM_SHADER_CONTROL_32_BITS_EXPORTS
= 0x40`).

Source selector (Mesa `NVFX_FP_REG_*`, RP `SRC0/1/2`): `type[1:0]` (0 temp,
1 input, 2 const), `idx[7:2]`, `fp16[8]`, `swzX[10:9] swzY[12:11] swzZ[14:13]
swzW[16:15]`, `neg[17]`. Identity swizzle -> `0xe4 << 9 = 0x1c800`.

| Word | Field (Mesa / RP) | Bits | FP_COL | FP_CONST | FP_TEX (E13) |
|---|---|---|---|---|---|
| OPDEST | saturate / no_dest | 31 / 30 | 0 / 0 | 0 / 0 | 0 / 0 |
| OPDEST | opcode | 29:24 | 0x01 MOV | 0x01 MOV | 0x17 TEX |
| OPDEST | precision (0 = fp32) | 23:22 | 0 | 0 | 0 |
| OPDEST | tex unit | 20:17 | 0 | 0 | 0 |
| OPDEST | input attr (0 WPOS, 1 COL0, 4 TC0) | 16:13 | 1 | 0 | 4 |
| OPDEST | writemask xyzw | 12:9 | 0xf | 0xf | 0xf |
| OPDEST | set cc / fp16 dest | 8 / 7 | 0 / 0 | 0 / 0 | 0 / 0 |
| OPDEST | dest reg (R0) | 6:1 | 0 | 0 | 0 |
| OPDEST | PROGRAM_END | 0 | 1 | 1 | 1 |
| **OPDEST** | | | **0x01003e01** | **0x01001e01** | **0x17009e01** |
| SRC0 | cond swizzle xyzw (identity) | 28:21 | 0xe4 | 0xe4 | 0xe4 |
| SRC0 | exec if lt/eq/gt (TR) | 20:18 | 111 | 111 | 111 |
| SRC0 | swizzle / type | 16:9 / 1:0 | 0xe4 / 1 input | 0xe4 / 2 const | 0xe4 / 1 input |
| **SRC0** | | | **0x1c9dc801** | **0x1c9dc802** | **0x1c9dc801** |
| SRC1 | unused: temp R0, identity | | **0x0001c800** | **0x0001c800** | **0x0001c800** |
| SRC2 | addr_reg (inert unless bit 30) | 29:19 | 0 | 0 | 0x7fc |
| **SRC2** | | | **0x0001c800** | **0x0001c800** | **0x3fe1c800** |

Memory form = `fp_word()` of each: FP_COL `3e010100 c8011c9d c8000001 c8000001`,
FP_CONST `1e010100 c8021c9d c8000001 c8000001` + constant words
`fp_word(0x3f800000)=0x00003f80`, `fp_word(0)=0`.

The `0x7fc << 19` in FP_TEX SRC2 is what cgcomp emits for any texcoord input
(`compilerfp.cpp`, "`0x7fc << NV40_FP_OP_ADDR_INDEX_SHIFT`", mirroring real Cg
output). RP decodes it as `addr_reg`, only used when `use_index_reg` (bit 30)
is set. Mesa leaves it 0. Either should be fine; the table uses cgcomp's value
because it is the libgcm-shaped one.

## B. Textured quad (E13)

### B.1 Texture in VRAM: 4x4 A8R8G8B8, linear, pitch 16

A8R8G8B8 texels are bytes `[A, R, G, B]` in memory (rsxtest `init_texture`
writes `buffer[i+0]=A, [i+1]=R, [i+2]=G, [i+3]=B`). A big-endian PPU store of
`0xAARRGGBB` produces exactly that, so write colours as ordinary u32 constants.
This is the same layout as the scanout surface.

```c
static const u32 tex4x4[16] = {   /* row 0 is t=0 (top of quad) */
    0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff,  /* red    green   blue    white   */
    0xff000000, 0xffffff00, 0xff00ffff, 0xffff00ff,  /* black  yellow  cyan    magenta */
    0xff808080, 0xffff8000, 0xff8000ff, 0xff00ff80,  /* grey   orange  purple  spring  */
    0xff404040, 0xffc0c0c0, 0xff800000, 0xff008000,  /* dkgrey ltgrey  maroon  dkgreen */
};
/* at VRAM offset TEX_OFF (128-aligned), rows 16 bytes apart (pitch 16) */
```

Each texel covers 135x135 screen pixels with NEAREST filtering. The pattern is
asymmetric so orientation, linear-vs-swizzled and channel order are each
visible (see C.2).

### B.2 Command stream

Common state and viewport as A.1.1 / A.1.2 (no need to resend if still set).

```c
/* B.2.1 vertex program: MOV o[HPOS], v[0]; MOV o[TEX0], v[8] */
0x00081e9c, 0x00000000, 0x00000000,
0x00200b80,
    0x401f9c6c, 0x0040000d, 0x8106c083, 0x6041ff80,  /* MOV o[HPOS], v[0]      */
    0x401f9c6c, 0x0040080d, 0x8106c083, 0x6041ff9d,  /* MOV o[TEX0], v[8]  END */
0x00041ff0, 0x00000101,             /* INPUT_MASK  = v0 | v8 (GCM_VERTEX_ATTRIB_TEX0 = 8) */
0x00041ff4, 0x00004020,             /* OUTPUT_MASK = TEX0 (1<<14, GCM_ATTRIB_OUTPUT_MASK_TEX0; Mesa vp_or 0x4000<<n) | POINTSIZE */
0x00041ef8, 0x0020ffff,

/* B.2.2 texture unit 0 (PSL LoadTexture + TextureControl + TextureFilter + TextureWrapMode, rsxtest setTexture) */
0x00041fd8, 0x00000001,             /* INVALIDATE texture cache (PSL InvalidateTextureCache, GCM_INVALIDATE_TEXTURE=1; RP 0x1fd8; Mesa TEX_CACHE_CTL) */
0x00081a00, TEX_OFF, 0x0001a529,    /* TEXTURE_OFFSET[0], TEXTURE_FORMAT[0] (derivation below) */
0x00041a10, 0x0000aae4,             /* TEXTURE_CONTROL1[0] remap: identity (rsxtest remap; RP reset default 0xaae4) */
0x00041a18, 0x00040004,             /* TEXTURE_IMAGE_RECT[0] = width<<16 | height = 4,4 */
0x00041840, 0x00100010,             /* TEXTURE_CONTROL3[0] = depth<<20 | pitch = 1, 16 */
0x00041a0c, 0x80060000,             /* TEXTURE_CONTROL0[0] = enable<<31 | minlod 0<<19 | maxlod (12<<8)<<7 | aniso 0<<4 */
0x00041a14, 0x01012000,             /* TEXTURE_FILTER[0] = mag NEAREST(1)<<24 | min NEAREST(1)<<16 | conv QUINCUNX(1)<<13 | bias 0 */
0x00041a08, 0x10030303,             /* TEXTURE_ADDRESS[0] = CLAMP_TO_EDGE(3) s|t<<8|r<<16, zfunc LESS(1)<<28 (rsxtest exact) */
0x00041a1c, 0x00000000,             /* TEXTURE_BORDER_COLOR[0] */

/* B.2.3 fragment program: TEX R0, f[TEX0], texture[0], 2D */
0x000408e4, FPT_OFF | 1,
0x00040b40, 0x00000000,             /* TEX_COORD_CONTROL[0]: 0 = full xyzw texcoord (Mesa nv40 writes 0; libgcm writes 1 only for float2 coords; ps3fb default 0) */
0x00041d60, 0x02000440,

/* B.2.4 two triangles; per vertex texcoord (attr 8 -> 0x1c80) first, position last */
0x00041808, 0x00000005,
0x00101c80, 0x00000000, 0x00000000, 0x00000000, 0x3f800000,  /* st (0,0) */
0x00101c00, 0xbf100000, 0x3f000000, 0x00000000, 0x3f800000,  /* (-0.5625, 0.5) top-left */
0x00101c80, 0x00000000, 0x3f800000, 0x00000000, 0x3f800000,  /* st (0,1) */
0x00101c00, 0xbf100000, 0xbf000000, 0x00000000, 0x3f800000,  /* (-0.5625,-0.5) bottom-left */
0x00101c80, 0x3f800000, 0x00000000, 0x00000000, 0x3f800000,  /* st (1,0) */
0x00101c00, 0x3f100000, 0x3f000000, 0x00000000, 0x3f800000,  /* ( 0.5625, 0.5) top-right */
0x00101c80, 0x3f800000, 0x00000000, 0x00000000, 0x3f800000,  /* st (1,0) */
0x00101c00, 0x3f100000, 0x3f000000, 0x00000000, 0x3f800000,  /* top-right */
0x00101c80, 0x00000000, 0x3f800000, 0x00000000, 0x3f800000,  /* st (0,1) */
0x00101c00, 0xbf100000, 0xbf000000, 0x00000000, 0x3f800000,  /* bottom-left */
0x00101c80, 0x3f800000, 0x3f800000, 0x00000000, 0x3f800000,  /* st (1,1) */
0x00101c00, 0x3f100000, 0xbf000000, 0x00000000, 0x3f800000,  /* ( 0.5625,-0.5) bottom-right */
0x00041808, 0x00000000,
```

Expected image: a 540x540 square, pixels x 690..1230, y 270..810, showing the
4x4 grid with red in the top-left cell and dark green in the bottom-right.

TRIANGLES (2 x 3 vertices) is used instead of QUADS (8) so that E13 adds no
new primitive type on top of E12.

```c
/* FP_TEX image at FPT_OFF: TEX R0, f[TEX0], texture[0], 2D ; fp_control 0x40, 2 regs */
static const u32 fp_tex[] = {
    0x9e011700, 0xc8011c9d, 0xc8000001, 0xc8003fe1,
    0x00010001, 0x00000000, 0x00000000, 0x00000000,  /* pad */
};
```

### B.3 Field derivations for E13

**VP insn 1** (`MOV o[TEX0], v[8]`), differences from A.3 only:
D1 `INPUT_SRC[11:8] = 8` -> `0x0040080d`; D3 `DEST[6:2] = 7`
(`NV40_VP_INST_DEST_TC0`), LAST=1 -> `0x6041ff9d`. cgcomp `out_mask=0x4000`,
`in_mask=0x101`.

**FP_TEX**: column FP_TEX in A.4. Opcode 0x17 = TEX (Mesa
`NVFX_FP_OP_OPCODE_TEX`, RP `RSX_FP_OPCODE_TEX = 0x17`), input attr 4 = TC0,
tex unit 0 at bits 20:17.

**TEXTURE_FORMAT = 0x0001a529** (PSL `LoadTexture`; RP `RSXTexture.cpp`
decoders):

| Bits | Field | Value | Source |
|---|---|---|---|
| 1:0 | location+1 (1 = DMA A = 0xfeed0000 VRAM, 2 = DMA B XDR) | 1 | PSL `(location+1)`, RP `location() = (fmt&3)-1` |
| 2 | cubemap | 0 | PSL, RP `cubemap()` |
| 3 | border type ("NO_BORDER") | 1 | PSL ORs `NV40TCL_TEX_FORMAT_NO_BORDER` always; RP `border_type()` |
| 7:4 | dimension (2 = 2D) | 2 | PSL `GCM_TEXTURE_DIMS_2D`, RP `dimension()` |
| 15:8 | format = 0x85 A8R8G8B8 \| 0x20 LN (linear) = 0xa5 | 0xa5 | RP `CELL_GCM_TEXTURE_A8R8G8B8 = 0x85`, `CELL_GCM_TEXTURE_LN = 0x20`; PSL defines A8R8G8B8 as 5 and ORs 0x8000 separately, same bits. Bit 14 (0x40, UN = unnormalised coords) left 0, so st is in [0,1] |
| 31:16 | mipmap count | 1 | PSL `mipmap << 16` |

Cross-check: RP's realhw reset default for this register is `0x18429`
(B8 = 0x81\|LN, 2D, no border, 1 mip, location 1), which has exactly the same
structure.

**Other texture words**: TEXTURE_CONTROL3 (RP name; PSL `TEX_SIZE1`) =
`depth<<20 | pitch` (RP `pitch() = reg & 0xfffff`, `depth() = reg >> 20`).
RP's reset value `0x100008` (pitch 8) shows a pitch as small as 8 is normal, so
16 for a 4-texel ARGB row is fine. CONTROL0 / FILTER / ADDRESS field shifts are
PSL nv40.h (`TEX_ENABLE_SHIFT 31`, `MINLOD 19`, `MAXLOD 7`, `MAXANISO 4`,
`FILTER_MAG 24`, `MIN 16`, `CONV 13`, `WRAP_S 0`, `T 8`, `R 16`, `UREMAP 12`,
`GAMMA 20`, `ZFUNC 28`) and match RP `RSXTexture.cpp` getters (`enabled()` bit
31 of CONTROL0, `convolution_filter()` bits 15:13, `unsigned_remap()` bits
15:12 of ADDRESS, `gamma()` bits 23:20). Remap 0xaae4: bits 7:0 select input
channel per output (A=0,R=1,G=2,B=3 -> 0b11100100), bits 15:8 = 2 per channel
= REMAP (use the selected channel) (PSL `GCM_TEXTURE_REMAP_*`).

### B.4 Fallback draw path: vertex arrays (if immediate mode does not draw)

Immediate mode is the one draw path no PSL1GHT sample exercises (C.1 #1). The
samples that are known to run on hardware (rsxtest, debugfont_renderer) use
vertex arrays. Same VP/FP/state as E12, vertex data in VRAM at `VB_OFF`,
interleaved 32-byte vertices `{x,y,z,w, r,g,b,a}` as big-endian floats (plain
PPU float stores; rsxtest writes vertices the same way):

```c
0x00041740, 0x00002042,             /* VERTEX_DATA_ARRAY_FORMAT[0] = stride 32<<8 | size 4<<4 | F32 (2)   PSL BindVertexArrayAttrib */
0x00041680, VB_OFF + 0,             /* VERTEX_DATA_ARRAY_OFFSET[0] = location(0 = DMA A VRAM)<<31 | offset */
0x0004174c, 0x00002042,             /* FORMAT[3] */
0x0004168c, VB_OFF + 16,            /* OFFSET[3] (colour at +16) */
0x400c1714, 0, 0, 0,                /* INVALIDATE_VERTEX_FILE x3, non-incrementing header (PSL DrawVertexArray) */
0x00041808, 0x00000005,             /* BEGIN_END = TRIANGLES */
0x00041814, 0x02000000,             /* DRAW_ARRAYS = (count-1)<<24 | first = 3 vertices from 0 */
0x00041808, 0x00000000,
0x00041740, 0x00000002,             /* restore FORMAT[0] to "size 0" (ps3fb/RP default) so later immediate tests use registers */
0x0004174c, 0x00000002,
```

## C. Uncertainties, ranked (probe the top ones first)

### C.1 Ranking

1. **Immediate-mode vertices (0x1c00.. inside BEGIN_END).** Medium-high
   confidence. libgcm exposes it (cellGcmSetVertexData4f, PSL
   `rsxDrawVertex4f` writes exactly these methods), RPCS3 implements it with
   attribute 0 as the provoking write (`RSXDrawCommands.cpp`
   `append_to_push_buffer`: "Enforce ATTR0 as vertex attribute for push
   buffers"; `NV47/HW/common.cpp`: "classic OpenGL 1.x behavior"). But no
   PSL1GHT sample uses it and Mesa only writes 0x1c00 outside BEGIN_END (for
   stride-0 attributes, `nv30_vbo.c`). If E12 draws nothing and the GPU is not
   wedged, switch to B.4. If immediate mode works, the order rule (other
   attributes before attribute 0) matters: the wrong order shifts colours by
   one vertex.
2. **SHADER_CONTROL 0x40 with an R0 output.** High confidence (cgcomp sets 0x40
   whenever the ARB result register is written, RP names it
   `CELL_GCM_SHADER_CONTROL_32_BITS_EXPORTS`). Mesa's NV40 path instead
   writes H0 (fp16) with 0x40 clear; either pairing is self-consistent. If
   colours come out as garbage but the shape is right, try `0x02000400` with
   the FP dest switched to H0 (`OPDEST` bit 7 set: FP_CONST becomes logical
   `0x01001e81`, memory `0x1e810100`).
3. **Fragment program halfword swap.** High confidence (three sources, A.2).
   Do not blindly try the unswapped form: misread words set SRC1 bit 31
   ("opcode is branch") and clear the END bit, which is a plausible way to
   wedge the GPU. Only try it if the swapped form is proven wrong another way.
4. **VERTEX_ATTRIB_OUTPUT_MASK / INPUT_MASK values.** High confidence (cgcomp +
   Mesa agree on bit meanings). RPCS3 drops immediate writes to attributes not
   in INPUT_MASK, so a wrong mask shows up as a missing attribute (black
   triangle) rather than a hang.
5. **Viewport y direction.** Cosmetic only; rsxtest uses negative scale_y.
6. **Texture format bits / pitch 16.** High confidence (B.3; RP default pitch is 8).
   Wrong linear/swizzle bit would scramble texels inside 2x2 blocks, not hang.
7. **Inert bits** (TEX_COORD_CONTROL 0 vs 1, SRC2 `0x7fc`, the trailing pad
   instruction, writing VIEWPORT twice, SHADER_WINDOW). Low risk either way.
8. **SURFACE_FORMAT without log2 width/height.** libgcm `SetSurface` puts
   log2(w) in bits 23:16 and log2(h) in 31:24 (`0x0a0a0148` for 1920x1080).
   The clear works with `0x148`, and those fields matter for swizzled surfaces;
   if draws (but not clears) misbehave, try the libgcm value.

### C.2 Suggested probe order on hardware

Each step only adds one new thing; check for a GPU exception or a hang after
each before going on.

1. Send A.1.1 + A.1.2 only (plain register writes, no draw). Then send A.1.3
   (VP upload) and A.1.4 with **FP_CONST**. No draw yet. These are all state
   writes libgcm does every frame.
2. Empty draw: `BEGIN_END = 5`, `BEGIN_END = 0` with no vertices.
3. E12 with **FP_CONST** (solid magenta 0xFFFF00FF triangle). This tests VP,
   viewport, rasteriser and FP fetch without depending on varyings.
4. E12 with **FP_COL** (colour-interpolated triangle). Tests VP output
   routing to FP inputs.
5. If 3 shows nothing: B.4 vertex-array path with FP_CONST.
6. E13 with **FP_CONST** but texture unit 0 fully set up (tests the texture
   registers without sampling), then E13 with **FP_TEX**.

Reading the E13 result:

| What you see in the top-left cell | Likely cause |
|---|---|
| red (0xffff0000) | correct |
| blue | channel order reversed (remap or byte order); check the u32 stores |
| cyan-ish / colours shifted by one channel | bytes rotated: check the texture was written as BE u32, not byte-swapped |
| grid looks right but some 2x2 blocks transposed | LN bit (0x2000 of FORMAT) not taking effect, texture read as swizzled |
| red at bottom-left | st/t direction or viewport y sign |
| solid single colour | texcoords not arriving (OUTPUT_MASK bit 14, FP input attr 4) |

### C.3 Things not covered here

- No vertex-program constants, so no 0x1efc (TRANSFORM_CONSTANT_LOAD) writes.
- No depth buffer use (depth test off). ZMIN_MAX_CONTROL stays at ps3fb's 1;
  all vertices have z=0 -> window z 0.5, inside [0,1].
- After the PPU writes the FP or texels into VRAM: barrier before advancing
  PUT, and send 0x1fd8=1 (texture cache) before a draw that samples new texels.
  Mesa notes that re-writing 0x8e4 is what makes the GPU re-read a fragment
  program (`nv30_fragprog.c`: "TEX_CACHE_CTL magic is not enough"), so resend
  0x8e4 whenever FP memory changes.

## D. Tooling used (outside the repo, reproducible)

- Sources cloned into `/tmp/e12src/{PSL1GHT,rpcs3,mesa,envytools}` (sparse
  for rpcs3/mesa).
- `/tmp/e12build/cgcomp`: PSL1GHT cgcomp built with
  `g++ -I tools/cgcomp/include tools/cgcomp/source/*.cpp -ldl`.
- `/tmp/e12build/dump`: driver around `CVPParser/CCompilerVP` and
  `CFPParser/CCompilerFP` printing logical words, masks, fp_control, regs.
  Inputs `tri.vp`, `tex.vp`, `tri2.fp` (ARB MOV), `const.fp`
  (`#const c[0] = 1 0 1 1` + `MOV result.color, c[0]`), `tex2.fp`
  (`TEX result.color, fragment.texcoord[0], texture[0], 2D`).
- `/tmp/e12build/verify.py`: Mesa-field re-encoder + RPCS3-layout decoder +
  memory-form round trip. `/tmp/e12build/gen.py`: emits the command arrays above.

cgcomp syntax gotchas found on the way: the texture operand must be spelled
`texture[0]` (not `TEX0`, which silently produces an all-zero instruction), and
inline `{...}` constants are not parsed; use a `#const c[N] = x y z w` comment
line as Cg's own output does.
