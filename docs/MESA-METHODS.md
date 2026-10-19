# Mesa nv30 against the RSX: method audit

This is a desk audit, written on 2026-10-14 before anything of Mesa had run on the PS3. We read every method that Mesa's nv30 driver can send to a chipset 0x47 (NV47, class 0x4097) and compared it with what RPCS3 and libgcm know about the RSX. The tables are the lasting part: which methods Mesa emits and where, which handles need replacing, the init sequence, and the per-class method list. Most of the audit's risks were then settled on the hardware, and the per-class list became the source of the driver's validator allowlist ([DRIVER.md, "The validator"](DRIVER.md#the-validator)).

The sources were Mesa main `src/gallium/drivers/nouveau/nv30/*` (screen, state, state_validate, draw, vbo, push, vertprog, fragprog, fragtex, verttex, clear, query, transfer, context, headers) and RPCS3 master `gcm_enums.h`, `rsx_methods.cpp` and `NV47/HW/nv4097.cpp`, all fetched on 2026-10-14. Line numbers below are in those copies, not in the Mesa we ship (26.2.4 plus our patch series). For chipset 0x47 the `CURIE_4097_CHIPSET` bit 7 is set, so the oclass is NV40_3D_CLASS (0x4097); the other objects are M2MF 0x0039 (NV03_M2MF), surf2d 0x0062 (NV10_SURFACE_2D), swz 0x309e (NV40_SURFACE_SWZ) and SIFM 0x3089 (NV40_SIFM). The nv30-only branches (chipset below 0x40) are skipped throughout.

Method offsets are byte offsets inside each class (what the FIFO header carries in bits 2 to 12). RPCS3 keys its registers by `subchannel<<13 | method`, with the libgcm subchannel layout 0 = 4097, 1 = 0039, 3 = 3062, 4 = 309e, 5 = 308a, 6 = 3089. The class base added in the tables is only there to look names up.

## What became of the audit

The measured results are in [RSX.md](RSX.md); the raw FIFO words of the first triangle and textured quad are in [RSX-PROGRAMS.md](RSX-PROGRAMS.md).

| Audit point | Outcome on the hardware |
|---|---|
| Handles need replacing (risk 1, section 2c) | Done as listed. Mesa's patch series writes the PS3 handles at allocation time and ps3rsx accepts only that handle set ([DRIVER.md](DRIVER.md#ioctls-abi16c-nvifh)) |
| Init values that differ from libgcm (risk 2) | The GPU accepted all of them. `rsx-mesainit` replayed Mesa's whole init stream, including 0x1450 = 4, 0x1eac = 0xff800006, 0x1ef8 and 0x1d64, and a 3D clear worked afterwards (E15) |
| Subchannel numbering (risk 3) | Mesa's layout (3D 7, M2MF 2, SF2D 3, SSWZ 4, SIFM 5) works as it is (E15). The kernel's own M2MF copies use subchannel 6 |
| Fence semaphore (risk 4) | Confirmed. The 0x1d70 back-end release stores the value with bytes 0 and 2 swapped, writing a pre-swizzled value stores the intended one (E4), and Mesa's fence emit pre-swizzles. The notifier also writes at reports+0 (E14), so the fence uses another label offset |
| Timer queries (risk 5) | Not a problem: `GET_REPORT` type 1 writes `{u64 GPU timestamp, u32 value, u32 0}` at reports+0x1400+offset on the hardware, which is what Mesa reads. The RPCS3 reading (type 1 as ZPASS count) only describes the emulator. Timer queries stay on |
| M2MF NOP at 0x0100 (risk 6) | Accepted by the GPU, along with RT_ENABLE 0x12 (`rsxdrm-smoke quirks`). No patch was needed |
| Data-range items (risk 7) | Vertex format type 0 (B8G8R8A8_UNORM) does raise a graph exception: the validator rejects it and Mesa never emits it on the PS3. RT_ENABLE 0x12 was accepted; no later failure pointed at the CONTROL2 word |
| Fragment program halfword swap, big-endian FIFO (risk 8) | Confirmed: fragment programs are stored halfword-swapped (see [RSX-PROGRAMS.md](RSX-PROGRAMS.md)); Mesa's patches do the same for the blit program |

Section 4's list is the table the validator's method allowlist is generated from.

## 1. The audit's ranked risks

Written before any run; the outcomes are in the table above. In order of how likely each was to hurt:

1. **Object and DMA handles.** Every `0xbeefXXXX` handle written to method 0x0000 and to the DMA-context slots has to become the PS3/libgcm handle (section 2c). A wrong handle is an invalid-object exception, so these are the first to patch and the last to forget. The `NV05_SIFM_SURFACE` write in transfer.c takes an object handle (surf2d or swzsurf), not a DMA handle.
2. **Init values that differ from libgcm's cellGcmInit defaults** (RPCS3 reset state, rsx_methods.cpp:150-570): 0x1450 (Mesa 0x4, libgcm 0x80003), 0x1d64 (Mesa 0x01d300d4, libgcm 0x02000000), 0x1eac SCULL_CONTROL (Mesa 0xff800006, libgcm 0xff000002) and 0x1ef8 TRANSFORM_TIMEOUT (Mesa 0x0020ffff, no libgcm default found). These are the "UNK" writes. They are accepted names, but a value the RSX does not like could wedge it. The advice was to replay them one at a time and prefer libgcm's values for the first run.
3. **Subchannel numbering.** Mesa puts 3D on subchannel 7, M2MF 2, SF2D 3, SSWZ 4, SIFM 5, while libgcm uses 0/1/3/4/6 (the RPCS3 layout). Binding is by method 0, so any subchannel should work, but it was untested on this RSX. The cheapest insurance was to replay with the libgcm layout.
4. **Fence.** `FENCE_OFFSET` and `FENCE_VALUE` (0x1d6c, 0x1d70) are the RSX semaphore methods. The offset must be 16-byte aligned (Mesa uses 0), DMA 0x1a4 must be 0x66606660, and RPCS3 shows the written value is channel-swizzled: `(v & 0xff00ff00) | ((v & 0xff) << 16) | ((v >> 16) & 0xff)` (nv4097.cpp:840). Mesa compares the readback with its sequence number, so sequences with distinct low and third bytes would not read back equal.
5. **Queries.** TIMESTAMP and TIME_ELAPSED use GET_REPORT type 1, which RPCS3 treats as ZPASS_PIXEL_CNT rather than a timer (it returns a timestamp only for unknown types). That would be wrong data, not a wedge. Report offsets are bytes into the 0x66626660 report area.
6. **M2MF NOP**, `NV04_GRAPH(M2MF, NOP)`, is method 0x0100 on the M2MF subchannel, used between OFFSET_IN and OFFSET_OUT in every M2MF copy. RPCS3 does not know it for 0039. The suggested fix was to use a 4097 NOP (0x0100 on the 3D subchannel is `NV4097_NO_OPERATION`) or drop it.
7. **Data-range items** with moderate risk: RT_ENABLE after `& ~fp->rt_enable` (state_validate.c:349) can yield values gcm never uses (gcm: 0, 1, 2, 0x13, 0x17, 0x1f; Mesa's clear and transfer paths only write 0x1); the texture CONTROL2 filter word 0x2dc4 against libgcm's 0x2dc8; and vertex-format type 0 (Mesa B8G8R8A8_UNORM), which maps to RPCS3's "ub256" default.
8. **Not method-related but relevant to replay.** Fragment programs and vertex programs go through memory or VP_UPLOAD. Fragment program bytes need 16-bit halfword swaps on a little-endian host (Mesa does this). The PS3 FIFO is big-endian.

What the audit could not confirm from RPCS3 (it only emulates) was the hardware's tolerance of the out-of-default UNK values in risk 2; E15 settled that.

Every method Mesa emits for a 0x4097 3D class, M2MF, surf2d, swz and SIFM has an NV4097/NV0039/NV3062/NV309E/NV3089 counterpart in RPCS3's `gcm_enums.h`, with the same offset and field layout. Only one method has no RPCS3 name (the M2MF NOP at 0x0100, section 2a). Mesa's 4097 offsets are the NV40 ones, which is also what libgcm uses. So the risk was in values and handles, not in method numbers.

## 2. Flagged methods

### 2a. Used by Mesa, not known to RPCS3 (highest wedge risk)

| Class (subch) | Off | Mesa name | Data | Used in | Where |
|---|---|---|---|---|---|
| 0039 (M2MF) | 0x0100 | NV04_GRAPH_NOP | 0 | xfer (every M2MF copy) | nv30_transfer.c:558, 750, 774 |

Nothing else. (nv30-only 0x08fc RC_ENABLE, 0x17e0, 0x1f80 are unknown to RPCS3 too, but only occur in the `oclass < NV40` branch, nv30_screen.c:642-657, so are skipped for 0x47.) Query enable `q->enable` (nv30_query.c:123,130) resolves to 0x17cc and 0x1804, both known (SET_ZPASS_PIXEL_COUNT_ENABLE, SET_ZCULL_STATS_ENABLE).

### 2b. Known, but semantics or data may differ on RSX

| Off | RPCS3 name | Mesa value | libgcm/RPCS3 default or rule | Where | Note |
|---|---|---|---|---|---|
| 0x1450 | SET_PERFORMANCE_PARAMS | 0x00000004 | 0x00080003 (rsx_methods.cpp:249) | nv30_screen.c:664 | Different value; Mesa's FP path does not rewrite it on NV40 |
| 0x1d64 | SET_INDEXED_CONSTANT_READ_LIMITS | 0x01d300d4 | 0x02000000 (:250) | nv30_screen.c:685 | Different value |
| 0x1ea4 | SET_ZCULL_CONTROL0 | 0x00000010 | 0x10 (:561) | nv30_screen.c:667 | Matches |
| 0x1ea8 | SET_ZCULL_CONTROL1 | 0x01000100 | 0x1000100 (:562) | same | Matches |
| 0x1eac | SET_SCULL_CONTROL | 0xff800006 | 0xff000002 (:563) | same | Different value (third word of the 3-word write) |
| 0x1ef8 | SET_TRANSFORM_TIMEOUT | 0x0020ffff | none found | nv30_screen.c:683 | Unverified |
| 0x03b0 | SET_CONTROL0 | 0x00100000 | 0x100000 (:221) | nv30_screen.c:688 | Matches. Mesa calls it MIPMAP_ROUNDING; on RSX it is also the depth-format control, so later writes of other values would change the depth type |
| 0x1fc4 | SET_ATTRIB_COLOR | 0x06144321 | 0x6144321 (:225) | nv30_screen.c:673 | Matches |
| 0x1fc8/cc | SET_ATTRIB_TEX_COORD(_EX) | 0xedcba987, 0x6f | same (:226-227) | :675 | Matches |
| 0x1fd0/d4 | SET_ATTRIB_UCLIP0/1 | 0x171615, 0x1b1a19 | same (:228-229) | :678,680 | Matches |
| 0x1e94 | SET_TRANSFORM_EXECUTION_MODE | 0x11 | 0x11 (:248) | vertprog.c:211,217 | Matches |
| 0x1ff0/f4 | SET_VERTEX_ATTRIB_INPUT/OUTPUT_MASK | vp->ir, vp->or\|fp->vp_or | defaults 0xffff / 0x3fffff | vertprog.c:214 | Data comes from compiled VP; output mask must be within 22 bits |
| 0x1d80 | SET_SURFACE_COMPRESSION | not written on NV40 | libgcm writes 3 | - | Mesa skips it; first-run gap vs libgcm |
| 0x0b00+4u | SET_TEXTURE_CONTROL2 | 0x00002dc4 (context.c:237) | 0x2dc8 (:240s) | fragtex.c:140 | 1-bit difference |
| 0x0220 | SET_SURFACE_COLOR_TARGET | rt_enable (0x1, 0x11..), `& ~fp->rt_enable` | gcm legal: 0,1,2,0x13,0x17,0x1f | state_validate.c:49-51,349 | Masked value can be non-gcm (e.g. 0x12) |
| 0x1740+4i | SET_VERTEX_DATA_ARRAY_FORMAT | type 0 = B8G8R8A8_UNORM, 1,2,3,4,5,7 | RPCS3 enum 1..7, 0 treated as ub256 | vbo.c:213, draw.c:362 | Type 0 faults; so does type 7 (UB256) with 3 components (0x106, Mesa 0043 before its fix): only UB256 x4 is safe, as RPCS3 asserts |
| 0x1800 | GET_REPORT | (report<<24)\|offset, report=1 for timestamp/time elapsed | type 1 = ZPASS_PIXEL_CNT; 2..5 zcull stats; others bad (nv4097.cpp:644) | query.c:158,187 | Wrong semantics for timer queries |
| 0x1d6c/70 | SET_SEMAPHORE_OFFSET / BACK_END_WRITE_SEMAPHORE_RELEASE | 0, sequence | offset%16==0, value channel-swizzled (nv4097.cpp:820-842) | screen.c:414-417 | Fence readback differs from written value |
| 0x1da4 | SET_CLIP_ID_TEST_ENABLE | 0 | 0 | state_validate.c:105 | Fine; clip-id DMA is 0 |
| 0x1e98 | SET_RENDER_ENABLE | 0x01000000 / 0x02000000\|offset | default 0x1000000 (:510) | query.c:252,263 | Conditional rendering; offset into report DMA |
| 0x0110 | WAIT_FOR_IDLE | 0 | - | query.c:259 | Fine |
| 0x023c / FP_REG_CONTROL | - | - | - | - | nv30-only, not emitted on 0x47 |

### 2c. DMA-object and object handle writes (patch these to PS3 handles)

PS3 handles: 0xfeed0000 (local/VRAM), 0xfeed0001 (main/GART), 0x66606660 (semaphore), 0x66626660 (report), 0x66604200 (notify), 0x0 (null / unused slot). The libgcm values below are RPCS3's cellGcmInit emulation (rsx_methods.cpp:166-181, 539-558, 1173-1198).

| Subch/class | Off | Mesa writes | Replace with |
|---|---|---|---|
| 3D | 0x0000 | eng3d handle (0xbeef3097) | 0x31337000 |
| 3D | 0x0180 DMA_NOTIFY | ntfy (0xbeef0301) | 0x66604200 |
| 3D | 0x0184 DMA_TEXTURE0 | fifo->vram | 0xfeed0000 |
| 3D | 0x0188 DMA_TEXTURE1 | fifo->gart | 0xfeed0001 |
| 3D | 0x018c DMA_COLOR1 | fifo->vram | 0xfeed0000 |
| 3D | 0x0190 DMA_UNK190 (RSX: CONTEXT_DMA_STATE) | null->handle | 0x0 |
| 3D | 0x0194 DMA_COLOR0 | fifo->vram | 0xfeed0000 |
| 3D | 0x0198 DMA_ZETA | fifo->vram | 0xfeed0000 |
| 3D | 0x019c DMA_VTXBUF0 | fifo->vram | 0xfeed0000 |
| 3D | 0x01a0 DMA_VTXBUF1 | fifo->gart | 0xfeed0001 |
| 3D | 0x01a4 DMA_FENCE (RSX: SEMAPHORE) | fence notifier (0xbeef1e00) | 0x66606660 |
| 3D | 0x01a8 DMA_QUERY (RSX: REPORT) | query notifier (0xbeef0351) | 0x66626660 |
| 3D | 0x01ac DMA_UNK1AC (RSX: CLIP_ID) | null->handle | 0x0 |
| 3D | 0x01b0 DMA_UNK1B0 (RSX: CULL_DATA) | null->handle | 0x0 |
| 3D | 0x01b4 DMA_COLOR2 | fifo->vram | 0xfeed0000 |
| 3D | 0x01b8 DMA_COLOR3 | fifo->vram | 0xfeed0000 |
| M2MF | 0x0000 | m2mf (0xbeef3901) | 0x31337303 |
| M2MF | 0x0180 | ntfy | 0x66604200 |
| M2MF | 0x0184/0188 DMA_BUFFER_IN/OUT (per copy, transfer.c:537,728) | vram or gart via PUSH_RELOC OR | 0xfeed0000 / 0xfeed0001 (libgcm default IN=feed0001, OUT=feed0000) |
| SF2D | 0x0000 | surf2d (0xbeef6201) | 0x313371c3 |
| SF2D | 0x0180 | ntfy | 0x66604200 |
| SF2D | 0x0184/0188 IMAGE_SOURCE/DESTIN (transfer.c:464) | vram or gart | 0xfeed0000/0xfeed0001 |
| SSWZ | 0x0000 | swzsurf (0xbeef5201) | 0x31337a73 |
| SSWZ | 0x0180 | ntfy | 0x66604200 |
| SSWZ | 0x0184 DMA_IMAGE (transfer.c:475) | vram or gart | 0xfeed0000/0xfeed0001 |
| SIFM | 0x0000 | sifm (0xbeef7701) | 0x3137af00 |
| SIFM | 0x0180 | ntfy | 0x66604200 |
| SIFM | 0x0184 DMA_IMAGE (transfer.c:485) | vram or gart | 0xfeed0000/0xfeed0001 |
| SIFM | 0x0198 SURFACE (transfer.c:472,481) | surf2d or swzsurf handle | 0x313371c3 (surf2d) or 0x31337a73 (swz) |

Not handles but location selectors using the same convention as RSX (0 = local, 1 = main, DMA0/DMA1 = location+1): FP_ACTIVE_PROGRAM (0x08e4, bits 0-1), TEX_FORMAT (0x1a04 bits 0-1), VTXBUF offset bit 31, IDXBUF_FORMAT (0x1820 bit 0 + type<<4). These need no patch.

## 3. Ordered init sequence from nv30_screen_create (chipset 0x47)

Format: `[subch] method xN : data`. Subchannels are Mesa's (3D=7, M2MF=2, SF2D=3, SSWZ=4, SIFM=5); data in braces is the value after patching for PS3. File:line is nv30_screen.c. Object creation (`nouveau_object_new`) is not a FIFO method; only the pushes are listed. Suggested replay batches are marked B1..B6.

Pre-FIFO (kernel ioctls, not methods): NULL object (class 0x30, handle 0), fence notifier 0xbeef1e00, ntfy 0xbeef0301, query notifier 0xbeef0351, 3D object 0xbeef3097, then m2mf 0xbeef3901, surf2d 0xbeef6201, swz 0xbeef5201, sifm 0xbeef7701.

B1 (bind 3D and DMA slots, line 625-639):
1. `[7] 0x0000 x1` : eng3d handle {0x31337000}
2. `[7] 0x0180 x13` : {0x66604200, 0xfeed0000, 0xfeed0001, 0xfeed0000, 0x0, 0xfeed0000, 0xfeed0000, 0xfeed0000, 0xfeed0001, 0x66606660, 0x66626660, 0x0, 0x0}  (NOTIFY, TEX0, TEX1, COLOR1, UNK190, COLOR0, ZETA, VTXBUF0, VTXBUF1, FENCE, QUERY, UNK1AC, UNK1B0)

B2 (NV40 branch, lines 659-689):
3. `[7] 0x01b4 x2` : {0xfeed0000, 0xfeed0000}  (COLOR2, COLOR3)
4. `[7] 0x1450 x1` : 0x00000004  (libgcm: 0x00080003)
5. `[7] 0x1ea4 x3` : 0x00000010, 0x01000100, 0xff800006  (ZCULL; third = SCULL_CONTROL, libgcm 0xff000002)
6. `[7] 0x1fc4 x1` : 0x06144321
7. `[7] 0x1fc8 x2` : 0xedcba987, 0x0000006f
8. `[7] 0x1fd0 x1` : 0x00171615
9. `[7] 0x1fd4 x1` : 0x001b1a19
10. `[7] 0x1ef8 x1` : 0x0020ffff
11. `[7] 0x1d64 x1` : 0x01d300d4  (libgcm 0x02000000)
12. `[7] 0x03b0 x1` : 0x00100000  (MIPMAP_ROUNDING_MODE_DOWN)

B3 (M2MF, lines 697-699):
13. `[2] 0x0000 x1` : m2mf handle {0x31337303}
14. `[2] 0x0180 x1` : ntfy {0x66604200}

B4 (surface 2D, lines 707-709):
15. `[3] 0x0000 x1` : surf2d handle {0x313371c3}
16. `[3] 0x0180 x1` : {0x66604200}

B5 (swizzled surface, lines 722-724):
17. `[4] 0x0000 x1` : swzsurf handle {0x31337a73}
18. `[4] 0x0180 x1` : {0x66604200}

B6 (SIFM, lines 737-743, then PUSH_KICK):
19. `[5] 0x0000 x1` : sifm handle {0x3137af00}
20. `[5] 0x0180 x1` : {0x66604200}
21. `[5] 0x02fc x1` : 0x00000001  (NV05_SIFM_COLOR_CONVERSION_TRUNCATE)

Notes: the 3D engine has no default-state writes beyond this; everything else (RT format, viewport, blend, depth, vertex format, program upload) comes from nv30_state_validate / draw. The lines 641-657 block (0x03b0=0x00100000, 0x1d80=3, 0x1e98, 0x17e0, 0x1f80, RC_ENABLE) is the pre-NV40 branch and is not run. Fence emit (nv30_screen.c:414-417, per fence, subch 7): `0x1d6c x2 : {0 (offset), sequence}`.

Suggested first-run test order after B1: try the minimal libgcm-compatible set (steps 1-3, 6-9, 12) before adding 4, 5, 10, 11, whose values differ from libgcm.

## 4. Per-class method list

Everything Mesa can emit on 0x4097.

`Used in` tags: init = screen_create, state = state objects and validate, draw = vbo/draw/push, shader = vp/fp upload, tex = texture, clear, xfer = transfer (blit), query. `n` = max words per BEGIN. Indexed arrays (VTXBUF, VTXFMT, VTX_ATTR_xF, TEX_*, VTXTEX_*, stencil face 0/1) are shown at index 0; the RSX has the same strides. Mesa 0x4097-class fixed set; RPCS3 name is the first name covering that offset. Not in the table because they are not methods: fragment program body (memory, referenced by 0x08e4), query objects (memory).

Extra methods outside the generator's table: 0x17cc `SET_ZPASS_PIXEL_COUNT_ENABLE` (query enable, data 0/1, query.c:171,192), 0x1804 `SET_ZCULL_STATS_ENABLE` (zcull queries), M2MF 0x0100 NOP (flagged in 2a), 3D 0x1d6c/0x1d70 fence (screen.c:414). Object-bind method 0x0000 appears in every class (init only).

| Class (subch) | Off | Mesa name | n | Used in | RPCS3 name (first) | Example site |
|---|---|---|---|---|---|---|
| 4097 (7) | 0x0000 | OBJECT | 1 | init | NV4097_SET_OBJECT | nv30_screen.c:625 |
| 4097 (7) | 0x0110 | UNK_0110 | 1 | query | NV4097_WAIT_FOR_IDLE | nv30_query.c:259 |
| 4097 (7) | 0x0180 | DMA_NOTIFY | 13 | init | NV4097_SET_CONTEXT_DMA_NOTIFIES | nv30_screen.c:627 |
| 4097 (7) | 0x01b4 | DMA_COLOR2 | 2 | init | NV4097_SET_CONTEXT_DMA_COLOR_C | nv30_screen.c:660 |
| 4097 (7) | 0x0200 | RT_HORIZ | 5 | clear,state,xfer | NV4097_SET_SURFACE_CLIP_HORIZONTAL | nv30_clear.c:157 |
| 4097 (7) | 0x020c | COLOR0_PITCH | 3 | clear,state | NV4097_SET_SURFACE_PITCH_A | nv30_clear.c:161 |
| 4097 (7) | 0x0214 | ZETA_OFFSET | 1 | clear | NV4097_SET_SURFACE_ZETA_OFFSET | nv30_clear.c:231 |
| 4097 (7) | 0x0218 | COLOR1_OFFSET | 2 | state | NV4097_SET_SURFACE_COLOR_BOFFSET | nv30_state_validate.c:146 |
| 4097 (7) | 0x0220 | RT_ENABLE | 1 | clear,state,xfer | NV4097_SET_SURFACE_COLOR_TARGET | nv30_clear.c:155 |
| 4097 (7) | 0x022c | ZETA_PITCH | 1 | clear,state | NV4097_SET_SURFACE_PITCH_Z | nv30_clear.c:228 |
| 4097 (7) | 0x0280 | COLOR2_PITCH | 1 | state | NV4097_SET_SURFACE_PITCH_C | nv30_state_validate.c:159 |
| 4097 (7) | 0x0284 | COLOR3_PITCH | 1 | state | NV4097_SET_SURFACE_PITCH_D | nv30_state_validate.c:170 |
| 4097 (7) | 0x0288 | COLOR2_OFFSET | 1 | state | NV4097_SET_SURFACE_COLOR_COFFSET | nv30_state_validate.c:156 |
| 4097 (7) | 0x028c | COLOR3_OFFSET | 1 | state | NV4097_SET_SURFACE_COLOR_DOFFSET | nv30_state_validate.c:167 |
| 4097 (7) | 0x02b8 | VIEWPORT_TX_ORIGIN | 4 | state,xfer | NV4097_SET_WINDOW_OFFSET | nv30_state_validate.c:111 |
| 4097 (7) | 0x0300 | DITHER_ENABLE | 1 | state,xfer | NV4097_SET_DITHER_ENABLE | nv30_state.c:61 |
| 4097 (7) | 0x0304 | ALPHA_FUNC_ENABLE | 3 | state,xfer | NV4097_SET_ALPHA_TEST_ENABLE | nv30_state.c:265 |
| 4097 (7) | 0x0310 | BLEND_FUNC_ENABLE | 3 | state,xfer | NV4097_SET_BLEND_ENABLE | nv30_state.c:94 |
| 4097 (7) | 0x031c | BLEND_COLOR | 1 | state | NV4097_SET_BLEND_COLOR | nv30_state_validate.c:185 |
| 4097 (7) | 0x0320 | BLEND_EQUATION | 1 | state | NV4097_SET_BLEND_EQUATION | nv30_state.c:101 |
| 4097 (7) | 0x0324 | COLOR_MASK | 1 | state,xfer | NV4097_SET_COLOR_MASK | nv30_state.c:113 |
| 4097 (7) | 0x0328 | STENCIL_ENABLE(0)/STENCIL_ENABLE(1) | 3 | clear,state,xfer | NV4097_SET_STENCIL_TEST_ENABLE | nv30_clear.c:97 |
| 4097 (7) | 0x0334 | STENCIL_FUNC_REF(0)/STENCIL_FUNC_REF(1) | 1 | state | NV4097_SET_STENCIL_FUNC_REF | nv30_state_validate.c:209 |
| 4097 (7) | 0x0338 | STENCIL_FUNC_MASK(0)/STENCIL_FUNC_MASK(1) | 4 | state | NV4097_SET_STENCIL_FUNC_MASK | nv30_state.c:239 |
| 4097 (7) | 0x0368 | SHADE_MODEL | 1 | state,xfer | NV4097_SET_SHADE_MODE | nv30_state.c:144 |
| 4097 (7) | 0x0374 | COLOR_LOGIC_OP_ENABLE | 2 | state,xfer | NV4097_SET_LOGIC_OP_ENABLE | nv30_state.c:53 |
| 4097 (7) | 0x037c | UNK_037c | 1 | state | NV4097_SET_BLEND_COLOR2 | nv30_state_validate.c:188 |
| 4097 (7) | 0x0380 | DEPTH_BOUNDS_TEST_ENABLE | 3 | state | NV4097_SET_DEPTH_BOUNDS_TEST_ENABLE | nv30_state.c:228 |
| 4097 (7) | 0x0394 | DEPTH_RANGE_NEAR | 2 | draw,state,xfer | NV4097_SET_CLIP_MIN | nv30_draw.c:355 |
| 4097 (7) | 0x03b0 | MIPMAP_ROUNDING | 1 | init | NV4097_SET_CONTROL0 | nv30_screen.c:688 |
| 4097 (7) | 0x03b8 | LINE_WIDTH | 2 | state | NV4097_SET_LINE_WIDTH | nv30_state.c:173 |
| 4097 (7) | 0x08c0 | SCISSOR_HORIZ | 2 | clear,state,xfer | NV4097_SET_SCISSOR_HORIZONTAL | nv30_clear.c:73 |
| 4097 (7) | 0x08e4 | FP_ACTIVE_PROGRAM | 1 | shader,xfer | NV4097_SET_SHADER_PROGRAM | nv30_fragprog.c:116 |
| 4097 (7) | 0x090c | VTXTEX_ENABLE(unit) | 1 | tex | NV4097_SET_VERTEX_TEXTURE_CONTROL0 | nv40_verttex.c:43 |
| 4097 (7) | 0x0a00 | VIEWPORT_HORIZ | 2 | draw,state,xfer | NV4097_SET_VIEWPORT_HORIZONTAL | nv30_draw.c:358 |
| 4097 (7) | 0x0a20 | VIEWPORT_TRANSLATE_X | 8 | draw,state,xfer | NV4097_SET_VIEWPORT_OFFSET | nv30_draw.c:346 |
| 4097 (7) | 0x0a60 | POLYGON_OFFSET_POINT_ENABLE | 3 | state | NV4097_SET_POLY_OFFSET_POINT_ENABLE | nv30_state.c:163 |
| 4097 (7) | 0x0a68 | POLYGON_OFFSET_FILL_ENABLE | 1 | xfer | NV4097_SET_POLY_OFFSET_FILL_ENABLE | nv30_transfer.c:286 |
| 4097 (7) | 0x0a6c | DEPTH_FUNC | 3 | state | NV4097_SET_DEPTH_FUNC | nv30_state.c:222 |
| 4097 (7) | 0x0a70 | DEPTH_WRITE_ENABLE | 2 | xfer | NV4097_SET_DEPTH_MASK | nv30_transfer.c:266 |
| 4097 (7) | 0x0a78 | POLYGON_OFFSET_FACTOR | 2 | state | NV4097_SET_POLYGON_OFFSET_SCALE_FACTOR | nv30_state.c:168 |
| 4097 (7) | 0x0b00 | TEX_FILTER_OPTIMIZATION(unit) | 1 | tex | NV4097_SET_TEXTURE_CONTROL2 | nv30_fragtex.c:140 |
| 4097 (7) | 0x0b40 | UNK_0b40 | 1 | shader,xfer | NV4097_SET_TEX_COORD_CONTROL | nv30_fragprog.c:129 |
| 4097 (7) | 0x0b80 | VP_UPLOAD_INST(0) | 4 | draw,shader,xfer | NV4097_SET_TRANSFORM_PROGRAM | nv30_draw.c:339 |
| 4097 (7) | 0x142c | VERTEX_TWO_SIDE_ENABLE | 1 | state | NV4097_SET_TWO_SIDE_LIGHT_EN | nv30_state.c:181 |
| 4097 (7) | 0x1450 | UNK_1450 | 1 | init | NV4097_SET_PERFORMANCE_PARAMS | nv30_screen.c:664 |
| 4097 (7) | 0x1454 | FLATSHADE_FIRST | 1 | state | NV4097_SET_FLAT_SHADE_OP | nv30_state.c:187 |
| 4097 (7) | 0x1478 | VP_CLIP_PLANES_ENABLE | 1 | state,xfer | NV4097_SET_USER_CLIP_PLANE_CONTROL | nv30_state_validate.c:292 |
| 4097 (7) | 0x147c | POLYGON_STIPPLE_ENABLE | 1 | state,xfer | NV4097_SET_POLYGON_STIPPLE | nv30_state.c:183 |
| 4097 (7) | 0x1480 | POLYGON_STIPPLE_PATTERN(0) | 32 | state | NV4097_SET_POLYGON_STIPPLE_PATTERN | nv30_state_validate.c:220 |
| 4097 (7) | 0x1500 | VTX_ATTR_3F(8)/VTX_ATTR_3F(attr) | 3 | draw,xfer | NV4097_SET_VERTEX_DATA3F_M | nv30_transfer.c:369 |
| 4097 (7) | 0x1680 | VTXBUF(0)/VTXBUF(i) | 1 | draw | NV4097_SET_VERTEX_DATA_ARRAY_OFFSET | nv30_draw.c:131 |
| 4097 (7) | 0x1710 | VTX_CACHE_INVALIDATE_1710 | 1 | draw,state | NV4097_INVALIDATE_VERTEX_CACHE_FILE | nv30_state_validate.c:507 |
| 4097 (7) | 0x1718 | R1718 | 1 | state | NV4097_PIPE_NOP | nv30_state_validate.c:514 |
| 4097 (7) | 0x173c | VB_ELEMENT_BASE | 1 | draw | NV4097_SET_VERTEX_DATA_BASE_INDEX | nv30_vbo.c:473 |
| 4097 (7) | 0x1740 | VTXFMT(0) | 16 | draw | NV4097_SET_VERTEX_DATA_ARRAY_FORMAT | nv30_draw.c:362 |
| 4097 (7) | 0x17c8 | QUERY_RESET | 1 | query | NV4097_CLEAR_REPORT_VALUE | nv30_query.c:165 |
| 4097 (7) | 0x1800 | QUERY_GET | 1 | query | NV4097_GET_REPORT | nv30_query.c:158 |
| 4097 (7) | 0x1808 | VERTEX_BEGIN_END | 1 | draw,xfer | NV4097_SET_BEGIN_END | nv30_draw.c:141 |
| 4097 (7) | 0x180c | VB_ELEMENT_U16 | 1 | draw | NV4097_ARRAY_ELEMENT16 | nv30_draw.c:154 |
| 4097 (7) | 0x1810 | VB_ELEMENT_U32 | 1 | draw | NV4097_ARRAY_ELEMENT32 | nv30_draw.c:145 |
| 4097 (7) | 0x1814 | VB_VERTEX_BATCH | 1 | draw | NV4097_DRAW_ARRAYS | nv30_draw.c:189 |
| 4097 (7) | 0x1818 | VERTEX_DATA | 1 | draw | NV4097_INLINE_ARRAY | nv30_push.c:101 |
| 4097 (7) | 0x181c | IDXBUF_OFFSET | 2 | draw | NV4097_SET_INDEX_ARRAY_ADDRESS | nv30_vbo.c:485 |
| 4097 (7) | 0x1824 | VB_INDEX_BATCH | 1 | draw | NV4097_DRAW_INDEX_ARRAY | nv30_vbo.c:501 |
| 4097 (7) | 0x1828 | POLYGON_MODE_FRONT | 6 | state,xfer | NV4097_SET_FRONT_POLYGON_MODE | nv30_state.c:148 |
| 4097 (7) | 0x183c | CULL_FACE_ENABLE | 1 | xfer | NV4097_SET_CULL_FACE_ENABLE | nv30_transfer.c:281 |
| 4097 (7) | 0x1840 | TEX_SIZE1(0)/TEX_SIZE1(unit) | 1 | tex,xfer | NV4097_SET_TEXTURE_CONTROL3 | nv30_fragtex.c:91 |
| 4097 (7) | 0x1880 | VTX_ATTR_2F(attr) | 2 | draw | NV4097_SET_VERTEX_DATA2F_M | nv30_vbo.c:69 |
| 4097 (7) | 0x1900 | VTX_ATTR_2I(0) | 1 | xfer | NV4097_SET_VERTEX_DATA2S_M | nv30_transfer.c:373 |
| 4097 (7) | 0x1a00 | TEX_OFFSET(0)/TEX_OFFSET(unit) | 8 | tex,xfer | NV4097_SET_TEXTURE_OFFSET | nv30_fragtex.c:127 |
| 4097 (7) | 0x1a0c | TEX_ENABLE(unit) | 1 | tex | NV4097_SET_TEXTURE_CONTROL0 | nv30_fragtex.c:143 |
| 4097 (7) | 0x1c00 | VTX_ATTR_4F(attr) | 4 | draw | NV4097_SET_VERTEX_DATA4F_M | nv30_vbo.c:56 |
| 4097 (7) | 0x1d60 | FP_CONTROL | 1 | shader,xfer | NV4097_SET_SHADER_CONTROL | nv30_fragprog.c:121 |
| 4097 (7) | 0x1d64 | UNK_1d64 | 1 | init | NV4097_SET_INDEXED_CONSTANT_READ_LIMITS | nv30_screen.c:685 |
| 4097 (7) | 0x1d78 | DEPTH_CONTROL | 1 | state | NV4097_SET_ZMIN_MAX_CONTROL | nv30_state.c:190 |
| 4097 (7) | 0x1d7c | MULTISAMPLE_CONTROL | 1 | state | NV4097_SET_ANTI_ALIASING_CONTROL | nv30_state_validate.c:338 |
| 4097 (7) | 0x1d88 | COORD_CONVENTIONS | 1 | state | NV4097_SET_SHADER_WINDOW | nv30_state_validate.c:350 |
| 4097 (7) | 0x1d8c | CLEAR_DEPTH_VALUE | 3 | clear | NV4097_SET_ZSTENCIL_CLEAR_VALUE | nv30_clear.c:106 |
| 4097 (7) | 0x1d90 | CLEAR_COLOR_VALUE | 2 | clear | NV4097_SET_COLOR_CLEAR_VALUE | nv30_clear.c:171 |
| 4097 (7) | 0x1d94 | CLEAR_BUFFERS | 1 | clear | NV4097_CLEAR_SURFACE | nv30_clear.c:239 |
| 4097 (7) | 0x1da4 | UNK_1da4 | 1 | state | NV4097_SET_CLIP_ID_TEST_ENABLE | nv30_state_validate.c:105 |
| 4097 (7) | 0x1dac | PRIM_RESTART_ENABLE | 2 | draw | NV4097_SET_RESTART_INDEX_ENABLE | nv30_vbo.c:630 |
| 4097 (7) | 0x1db0 | PRIM_RESTART_INDEX | 1 | draw | NV4097_SET_RESTART_INDEX | nv30_vbo.c:643 |
| 4097 (7) | 0x1db4 | LINE_STIPPLE_ENABLE | 2 | state | NV4097_SET_LINE_STIPPLE | nv30_state.c:176 |
| 4097 (7) | 0x1e40 | VTX_ATTR_1F(attr) | 1 | draw | NV4097_SET_VERTEX_DATA1F_M | nv30_vbo.c:74 |
| 4097 (7) | 0x1e94 | ENGINE | 1 | draw,shader,xfer | NV4097_SET_TRANSFORM_EXECUTION_MODE | nv30_draw.c:367 |
| 4097 (7) | 0x1e98 | UNK_1e98 | 1 | query | NV4097_SET_RENDER_ENABLE | nv30_query.c:252 |
| 4097 (7) | 0x1e9c | VP_UPLOAD_FROM_ID | 1 | draw,shader,xfer | NV4097_SET_TRANSFORM_PROGRAM_LOAD | nv30_draw.c:335 |
| 4097 (7) | 0x1ea0 | VP_START_FROM_ID | 1 | draw,shader,xfer | NV4097_SET_TRANSFORM_PROGRAM_START | nv30_draw.c:365 |
| 4097 (7) | 0x1ea4 | UNK_1ea4 | 3 | init | NV4097_SET_ZCULL_CONTROL0 | nv30_screen.c:667 |
| 4097 (7) | 0x1ee0 | POINT_SIZE | 1 | state | NV4097_SET_POINT_SIZE | nv30_state.c:185 |
| 4097 (7) | 0x1ee8 | POINT_SPRITE | 1 | state | NV4097_SET_POINT_SPRITE_CONTROL | nv30_state_validate.c:376 |
| 4097 (7) | 0x1ef8 | UNK_1ef8 | 1 | init | NV4097_SET_TRANSFORM_TIMEOUT | nv30_screen.c:683 |
| 4097 (7) | 0x1efc | VP_UPLOAD_CONST_ID | 5 | shader,state | NV4097_SET_TRANSFORM_CONSTANT_LOAD | nv30_state_validate.c:284 |
| 4097 (7) | 0x1fc4 | UNK_1fc4 | 1 | init | NV4097_SET_ATTRIB_COLOR | nv30_screen.c:673 |
| 4097 (7) | 0x1fc8 | UNK_1fc8 | 2 | init | NV4097_SET_ATTRIB_TEX_COORD | nv30_screen.c:675 |
| 4097 (7) | 0x1fd0 | UNK_1fd0 | 1 | init | NV4097_SET_ATTRIB_UCLIP0 | nv30_screen.c:678 |
| 4097 (7) | 0x1fd4 | UNK_1fd4 | 1 | init | NV4097_SET_ATTRIB_UCLIP1 | nv30_screen.c:680 |
| 4097 (7) | 0x1fd8 | TEX_CACHE_CTL | 1 | state,xfer | NV4097_INVALIDATE_L2 | nv30_state_validate.c:510 |
| 4097 (7) | 0x1ff0 | VP_ATTRIB_EN | 2 | draw,shader,xfer | NV4097_SET_VERTEX_ATTRIB_INPUT_MASK | nv30_draw.c:370 |
| 0039 (2) | 0x0000 | OBJECT | 1 | init | NV0039_SET_OBJECT | nv30_screen.c:697 |
| 0039 (2) | 0x0180 | NV03_M2MF_DMA_NOTIFY | 1 | init | NV0039_SET_CONTEXT_DMA_NOTIFIES | nv30_screen.c:699 |
| 0039 (2) | 0x0184 | NV03_M2MF_DMA_BUFFER_IN | 2 | xfer | NV0039_SET_CONTEXT_DMA_BUFFER_IN | nv30_transfer.c:537 |
| 0039 (2) | 0x030c | NV03_M2MF_OFFSET_IN | 8 | xfer | NV0039_OFFSET_IN | nv30_transfer.c:548 |
| 0039 (2) | 0x0310 | NV03_M2MF_OFFSET_OUT | 1 | xfer | NV0039_OFFSET_OUT | nv30_transfer.c:560 |
| 3062 (3) | 0x0000 | OBJECT | 1 | init | NV3062_SET_OBJECT | nv30_screen.c:707 |
| 3062 (3) | 0x0180 | NV04_SURFACE_2D_DMA_NOTIFY | 1 | init | NV3062_SET_CONTEXT_DMA_NOTIFIES | nv30_screen.c:709 |
| 3062 (3) | 0x0184 | NV04_SURFACE_2D_DMA_IMAGE_SOURCE | 2 | xfer | NV3062_SET_CONTEXT_DMA_IMAGE_SOURCE | nv30_transfer.c:464 |
| 3062 (3) | 0x0300 | NV04_SURFACE_2D_FORMAT | 4 | xfer | NV3062_SET_COLOR_FORMAT | nv30_transfer.c:467 |
| 309e (4) | 0x0000 | OBJECT | 1 | init | NV309E_SET_OBJECT | nv30_screen.c:722 |
| 309e (4) | 0x0180 | NV04_SURFACE_SWZ_DMA_NOTIFY | 1 | init | NV309E_SET_CONTEXT_DMA_NOTIFIES | nv30_screen.c:724 |
| 309e (4) | 0x0184 | NV04_SURFACE_SWZ_DMA_IMAGE | 1 | xfer | NV309E_SET_CONTEXT_DMA_IMAGE | nv30_transfer.c:475 |
| 309e (4) | 0x0300 | NV04_SURFACE_SWZ_FORMAT | 2 | xfer | NV309E_SET_FORMAT | nv30_transfer.c:477 |
| 3089 (5) | 0x0000 | OBJECT | 1 | init | NV3089_SET_OBJECT | nv30_screen.c:737 |
| 3089 (5) | 0x0180 | NV03_SIFM_DMA_NOTIFY | 1 | init | NV3089_SET_CONTEXT_DMA_NOTIFIES | nv30_screen.c:739 |
| 3089 (5) | 0x0184 | NV03_SIFM_DMA_IMAGE | 1 | xfer | NV3089_SET_CONTEXT_DMA_IMAGE | nv30_transfer.c:485 |
| 3089 (5) | 0x0198 | NV05_SIFM_SURFACE | 1 | xfer | NV3089_SET_CONTEXT_SURFACE | nv30_transfer.c:472 |
| 3089 (5) | 0x02fc | NV05_SIFM_COLOR_CONVERSION | 1 | init | NV3089_SET_COLOR_CONVERSION | nv30_screen.c:741 |
| 3089 (5) | 0x0300 | NV03_SIFM_COLOR_FORMAT | 8 | xfer | NV3089_SET_COLOR_FORMAT | nv30_transfer.c:487 |
| 3089 (5) | 0x0400 | NV03_SIFM_SIZE | 4 | xfer | NV3089_IMAGE_IN_SIZE | nv30_transfer.c:496 |

Older versions and the dated history: [archive/MESA-METHODS.md](archive/MESA-METHODS.md).
