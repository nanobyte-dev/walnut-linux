# DXT5 in libspeq (speq_s3tc_compress_v1)

Item 5 of docs/IDEAS.md, parked item 2 of SPU-OFFLOAD-PLAN.md. Mesa turns generic compressed
RGBA requests (`GL_COMPRESSED_RGBA`: Teeworlds, TORCS, any game asking for compressed alpha
textures) and `RGBA_DXT5` into DXT5, and encoded them with its own encoder on the PPE. Built
2026-10-17, host-tested, not run on the PS3; "estimate" marks numbers nobody measured.

Files: `lib/dxt-ref.c` (the reference: the PPE path, what the SPU must match byte for byte, and
a decoder), `spu/dxt.c` (the SPU encoder, SIMD, and a scalar copy for big-endian hosts),
`spu/dxt-op.c` (dxt.spu's job, mode `DXT_JOB_DXT5`), `lib/s3tc.c` (the policy),
`tools/speq-dxt.c` (`--fmt dxt5`), `spu/dxt-test.c` (fuzzing, SPU cycles), `tests/mesa-dxt.c`,
`tests/mesa-compare.sh`, `tests/make-corpus.py` (the alpha sets),
`mesa/patches/0032-s3tc-dxt5-libspeq.patch` (Mesa's call).

## 1. The format and what decoders do

16 bytes a block: alpha0, alpha1, 16 3-bit indices (48 bits, little-endian, pixel 0 in the low
bits of byte 2), then a DXT1 colour block. The alpha palette: alpha0 > alpha1, the two and six
values between, `(a0 (8 - c) + a1 (c - 1)) / 7`; otherwise four between, `/ 5`, then 0 and 255
(Mesa's `fetch_2d_texel_rgba_dxt5`, integer division). The colour block is always decoded in
4-colour mode (Mesa's `dxt135_decode_imageblock` with `dxt_type` 2; NVIDIA hardware the same),
whatever the order of color0 and color1, so an encoder must never rely on 3-colour mode. Ours
puts the larger 565 end first, and when the ends are equal every index is 0: both readings then
give the same colour, so the RSX and Mesa agree whichever mode the RSX takes.

## 2. The encoder (dxt-ref.c's comment has it exactly)

- **Colour**: van Waveren's box over the 16 pixels with its inset (range >> 4), then the box's
  diagonal: red and blue swap their ends when they fall against green (the sign of the sum of
  (c - centre)(g - centre), centre (lo + hi + 1) >> 1); color0 the larger end; indices by
  Manhattan distance as in DXT1. The same without the inset is a second candidate, taken when its
  sum of distances is strictly smaller (two-colour blocks, white on black, come out exact; with
  the inset white became 240). Transparent pixels count: bilinear filtering blends their colour
  into the edges.
- **Alpha**: four endpoint pairs with the decoder's palette each: A (max, min); B (lo, hi) of
  the values other than 0 and 255 in 6-value mode, so 0 and 255 stay exact; C, A inset by
  (max - min) >> 5; D, B inset by (hi - lo) >> 4. Nearest entry per pixel (the lowest index on a
  tie), the pair with the smallest squared error wins (the earlier on a tie). A flat block (most
  blocks: opaque or clear) is (v, v) with every index 0 at once.
- Chosen on the corpus below: the squared error picks better than the sum of distances (0.15-0.3
  dB of alpha), B and D only when the block holds 0 or 255 loses 0.02-0.12 dB, van Waveren's own
  DXT5 alpha (A inset by >> 5 only) is 0.4-6 dB worse (it loses 0 and 255).
- **SPU** (dxt.c): the colour block is DXT1's opaque path plus the diagonal (channel differences
  as halfwords, mpy/mpyhh products summed across words) and the swap; the alpha block gathers the
  16 alphas in one quadword, takes the four extremes with byte min/max in halving steps (two
  reductions per vector), the candidates' palettes with halfword multiplies (`(x * 9363) >> 16`
  is x / 7 for x <= 1785, `(x * 13108) >> 16` is x / 5 for x <= 1275, checked for every x), the
  nearest entries of all 16 pixels at once (absd and 7 compare-selects), the squared error with
  mpyu/mpyhhu; indices packed with byte shuffles and word shifts; one quadword store a block.
  dxt.spu grows from 16.9 to 21.8 KB of code. DXT5 needs a 16-aligned destination and stride on
  the SPEs (whole blocks in quadwords; Mesa's GART BOs are page-aligned, strides 64-aligned);
  otherwise the PPE (RGB sources: the SPEs too since doc/dxt-rgb.md).

## 3. Quality against Mesa's encoder (tests/mesa-compare.sh, host)

The corpus (`make-corpus.py`, generated under build/tmp): `oa-a5`, 24 OpenArena textures with
any alpha; `png-a5`, 40 PNGs with alpha picked evenly from SuperTuxKart's data, Neverball and
Extreme Tux Racer (build/work's source trees); `synth-a5`, gradients with alpha ramps, noise,
cut-outs; `oa-alpha`, the 12 alpha-tested OpenArena textures. RMSE over R, G, B, A of all
pixels; PSNR of the set's summed squared error.

| set | n | mean RMSE ours | Mesa | images ours better | Mesa better | PSNR ours | Mesa |
|---|---|---|---|---|---|---|---|
| oa-a5 | 24 | 3.026 | 3.088 | 13 | 9 | 35.92 | 35.63 |
| png-a5 | 40 | 4.772 | 4.416 | 12 | 27 | 31.37 | 32.55 |
| synth-a5 | 6 | 2.558 | 4.002 | 6 | 0 | 39.45 | 35.95 |
| oa-alpha | 12 | 3.317 | 3.360 | 6 | 6 | 36.39 | 36.23 |

- Alpha alone (PSNR): oa-a5 40.98 against 40.68, png-a5 48.77 against 49.27, synth-a5 37.48
  against 37.14, oa-alpha 43.49 against 43.06.
- The PNG set's gap is colour (30.1 against 31.3 dB), nearly all of it under transparent
  pixels: weighted by alpha the colour errors are equal (4.53 against 4.51 RMSE). Mesa's base
  colour search tries more endpoint pairs; the worst image for us is SuperTuxKart's
  `stk_asianSunRay_a` (24.8 against 20.0). DXT1 is unchanged (the diagonal would gain nothing on
  OpenArena's opaque textures: 4.765 against 4.766).

## 4. Speed

- PPE path (the reference, small images and no SPEs), x86 host: 34.6 Mpixel/s against Mesa's
  39.3 on an opaque 512x512, 53.5 against 34.4 on SuperTuxKart's 1024x1024 logo, 11.0 against
  12.3 on alpha noise: about Mesa's speed (the PPE's ratio is not measured).
- SPU, estimate from the code: a block whose alpha is flat runs 424 instructions (291 on the
  even pipe), a soft-alpha block 752 (516 even). With DXT1's ratio of measured cycles to
  even-pipe instructions (135 cycles for ~103), ~390 and ~680 cycles a block: ~130 and ~75
  Mpixel/s per SPE, 300-520 on DXT's four (DXT1: 379 per SPE measured); through glTexImage the
  upload path measured 352 Mpixel/s.

## 5. Mesa's side

Patch 0011's hook calls `speq_s3tc_compress_v1(fmt, ...)` from `_mesa_texstore_rgb_dxt1` and
`_mesa_texstore_rgba_dxt1` only, so DXT5 never reached libspeq. `mesa/patches/0032-s3tc-dxt5-
libspeq.patch` adds the same call with `GL_COMPRESSED_RGBA_S3TC_DXT5_EXT` before
`tx_compress_dxt5` in `_mesa_texstore_rgba_dxt5` (sRGB DXT5 goes the same way: only decoding
differs). An older libspeq returns -ENOTSUP for DXT5 and Mesa's encoder runs as before. The
switch is libspeq's, no Mesa rebuild: `SPEQ_S3TC_DXT5=off` gives DXT5 back to Mesa's encoder,
`SPEQ_S3TC=off` every format. Committed to build/work/mesa (tag `exported` moved), not built.

## 6. Tests (host, 2026-10-17)

- `speq-dxt --check` (ppe, and spu and auto on the fake backend): golden blocks (flat red, a
  clear block), DXT5 blocks (alpha ramps, a cut-out with soft edges, 0 and 255 only, red falling
  as green rises, alpha noise: error bounds, 0 and 255 exact where expected, color0 >= color1,
  every index 0 on equal ends), images, and the path matrix with DXT5 in it: 25 sizes up to 4096
  wide, source offsets, padded and packed strides, 8-mod-16 destinations (DXT5 to the PPE), RGB
  sources, a faulting DXT5 job (redone on the PPE), all byte for byte the reference.
- `make -f spu/dxt-test.mk check`: the SIMD path (dxt-emu.h, with mpyhh, mpyhhu, avgb added) and
  the scalar copy against the reference: the embedded images at every source offset in the three
  modes, and 6 million fuzzed blocks (2 million DXT5, with alpha kinds: 0 and 255 next to soft
  values, narrow bands, colours against each other): 0 differ.
- dlopen-check: DXT5 through dlsym as Mesa 0032 calls it.
- `make test-mesa`: the table above.
- Big-endian (the ppc64 package chroot under qemu-user, `-O2 -mcpu=cell`): `make test`, the
  `speq-dxt --check` paths included (the reference on the PPE's byte order, the scalar SPU copy),
  0 failures.

## 7. On the PS3 (to run)

- `speq-dxt --check --path spu`, then `--path ppe` (the reference's DXT5 on big-endian PPE
  code), `speq-dxt --bench --fmt dxt5` (Mpixel/s on the PPE and the SPEs, against
  `--fmt dxt1a`); `spurun dxt-test.spu result` (make -f spu/dxt-test.mk) for cycles a block.
- After Mesa 0032 is built: `SPEQ_DEBUG=1` on a game that asks for compressed RGBA (Teeworlds,
  TORCS) or OpenArena with `r_ext_compressed_textures 1` and an alpha shader: `speq: s3tc dxt5`
  lines, `spu` for the large ones; the picture (webcam): sprites, foliage and HUD icons with
  their alpha, no dark fringes or greyed white; VRAM (vram.csv) against `SPEQ_S3TC_DXT5=off`.
