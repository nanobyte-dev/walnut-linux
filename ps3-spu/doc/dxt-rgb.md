# RGB sources on the SPEs (speq_s3tc_compress_v1)

The stall recorder (2026-10-18) put the games' mid-game hitches on CPU-bound texture loads, and
the game logs showed why: `speq: s3tc dxt1 1024x1024 comps 3 stride 2048: ppe (RGB source), 0
jobs, 165231 us`. libspeq sent every 3-byte (RGB) source to its PPE encoder: 165 ms for a
1024x1024, 43 ms for a 512x512. Since 2026-10-18 dxt.spu takes them. Host-tested, not run on the
PS3; "estimate" marks numbers nobody measured.

Files: `spu/dxt.c` (`dxt_rgb_expand`), `spu/dxt-op.c` (the RGB units), `lib/dxt-job.h`
(`DXT_JOB_SRC_RGB`), `lib/s3tc.c` (policy, `SPEQ_S3TC_RGB`), `tools/speq-dxt.c` (`--rgb`,
`--comps`), `spu/dxt-test.c` (the expansion against a plain loop, its cycles).

## 1. What reaches libspeq

Mesa's texstore (`src/mesa/main/texcompress_s3tc.c`, patches 0011 and 0032) calls the hook with:

| Mesa function | GL formats | srccomps | source |
|---|---|---|---|
| `_mesa_texstore_rgb_dxt1` | RGB DXT1, sRGB DXT1 | 3 for `GL_RGB` data, 4 for `GL_RGBA` | the caller's pixels when they are `GL_UNSIGNED_BYTE`, tightly packed rows, no pixel transfer; anything else (BGR, luminance, other types, padded rows) Mesa first converts to a temporary tightly packed R, G, B image (`MESA_FORMAT_RGB_UNORM8`) and passes 3 |
| `_mesa_texstore_rgba_dxt1` | RGBA DXT1 | always 4 | the same, converted to R, G, B, A bytes |
| `_mesa_texstore_rgba_dxt5` | RGBA DXT5 | always 4 | the same |

So only two layouts ever arrive: R, G, B, A bytes and R, G, B bytes, rows `srccomps * w` bytes
apart. BGR, luminance and luminance-alpha never do: Mesa converts them (on the PPE, in its own
code) before the hook. The `stride` in the debug line is the destination's: a DXT1 block row of
a 1024 wide image is 256 blocks of 8 bytes, 2048 bytes, so "stride 2048" is right; the source has
no stride argument (3072 bytes a row for that image).

Which games: Sauerbraten's JPG textures and other images without alpha (`GL_RGB` data). Our
ioquake3 (opengl1 and opengl2) hands its textures over as `GL_RGBA` data even for opaque ones
(`GL_COMPRESSED_RGBA_S3TC_DXT1_EXT` internal format), so OpenArena's should be `comps 4`; count
the `comps 3` lines in a `SPEQ_DEBUG=1` log before reading anything into an OpenArena A/B.

## 2. Design: expand in local store

Two ways to feed the SPEs an RGB image:

- **PPE staging**: expand to RGBA into a buffer, then submit as before. The PPE reads 3 and writes
  4 bytes a pixel (7 MB of traffic for a 1024x1024, plus the cache misses of a buffer it then
  hands away), and the SPEs read 4 MB instead of 3. It is PPE time on the thread that is
  uploading the texture, the very thing the hitch is made of. `speq-dxt --bench --comps 3 --path
  ppe` prints this copy alone (`expand` lines) so the PS3 can put a number on it.
- **SPU expansion** (taken): the job DMAs the 3-byte rows (a quarter less than RGBA) and turns
  them into RGBA rows in local store with shuffles, then runs the unchanged encoders on them. The
  PPE does nothing new except one flag in the job.

`dxt_rgb_expand` works in steps of 16 pixels: 48 source bytes (three quadwords) become four RGBA
quadwords. Output quadword k takes the 12 bytes at `off + 12k` from the aligned base; they lie in
the quadword pair (Q[k-1], Q[k]) or (Q[k], Q[k+1]), the later one when `off >= 4k`, chosen once per
call with selects; the shuffle pattern picks R, G, B and makes alpha 0xff (shufb's 0xc0 control
bytes). Q3 and Q4 of a step are the next step's Q0 and Q1. The loop spu-elf-gcc 16.2 makes at
-O3 is 23 instructions: 3 loads, 4 shuffles, 4 stores and the branch on the odd pipe, 6 selects
and the counters on the even one, so about 12-14 cycles a step (estimate), under a cycle a pixel,
against ~135 cycles for the DXT1 block those 16 pixels make: ~10% more SPU time, a quarter less
DMA in. `spurun dxt-test.spu result` measures it (`rgb expand N cyc/16px`).

dxt-op.c keeps its units:
- contiguous (width up to 1024): k block rows, one DMA from the 16-byte boundary below the start,
  `12 * w * k` bytes (RGBA: 16), expanded into `xbuf` (16-aligned, offset 0) as one stream;
- strips (wider): a strip's four pixel rows are 3 * w bytes apart, not a multiple of 16, so each
  row's DMA has its own misalignment (`fetch` returns four offsets) and each row is expanded on
  its own, into rows RS apart in `xbuf`, in order (a row's 48-byte overrun lands in the next row,
  which is written after).

The encoder then reads `xbuf` exactly as it reads an RGBA unit with offset 0, and the reference
reads an RGB pixel as alpha 255, so both give the bytes an RGBA source with alpha 255 gives. In
RGBA DXT1 and DXT5 (Mesa never sends RGB there, the API allows it) that is opaque, as on the PPE.

Local store: `xbuf` adds 16.1 KB (4 * RS + 64: the expansion writes up to 48 bytes past a unit,
the encoder reads 16 past). dxt.spu: text 22.6 KB, bss 68.2 KB. The expansion reads up to 80
bytes past a unit's source, inside the source buffer's slack (16480 bytes for at most 12303).

Job ABI: `DXT_JOB_SRC_RGB` (0x100) ORed into `arg[DXT_ARG_MODE]`. A dxt.spu without it refuses
the mode with -EINVAL and the image is redone on the PPE (libspeq0 depends on the same version
of ps3-spu-programs, so that only happens with a hand-copied dxt.spu).

On the PPE side `touch` (the page pre-touch before submit) now reads the source as
`4 * srccomps * w` bytes a block row; with 16 it would have read past an RGB image's end.

## 3. Switches

- `SPEQ_S3TC=auto|spu|ppe|off`: unchanged, and now covers RGB sources too (auto: the SPEs from
  `SPEQ_S3TC_MIN` pixels, spu: every image with an aligned destination).
- `SPEQ_S3TC_RGB=ppe`: RGB sources go to libspeq's PPE encoder whatever `SPEQ_S3TC` says, the old
  behaviour, for A/B runs (debug line `ppe (SPEQ_S3TC_RGB=ppe)`); `off`: RGB sources back to Mesa's
  encoder (-ENOTSUP); `auto` (default): as RGBA.
- Bench recipes: `rgbspu` (the default path with `SPEQ_DEBUG=1`) and `rgbppe`
  (`SPEQ_S3TC_RGB=ppe SPEQ_DEBUG=1`) in `games/bench/recipes/sauerbraten.sh` and `openarena.sh`.

## 4. Tests (host, 2026-10-18)

- `speq-dxt --check` (ppe; spu and auto on the fake backend, which runs dxt-op.c and the SIMD
  path through dxt-emu.h): the path matrix now runs every size, format, offset and stride case
  with an RGBA and an RGB source (322 images, 271 on the SPEs with `--path spu`), RGB sources at
  16 offsets and widths 4..64 and 1028..1088 (strips), then 400 random cases (widths to 4400,
  source offsets 0..127, RGB or RGBA, every format, packed, padded, 64-aligned and 8 mod 16
  strides, destinations 0 and 8 mod 16), a faulting RGB job (redone on the PPE), RGB images in the
  four-thread test: all byte for byte the reference. `--rgb ppe`: every RGB image stays on the
  PPE (checked); `--rgb off`: refused. A pattern broken for one source offset fails 5+ cases.
- `make -f spu/dxt-test.mk check`: `dxt_rgb_expand` (SIMD through dxt-emu.h, and the scalar
  copy) against a plain loop at every source offset 0..15 and 13 lengths from 4 to 1028 pixels,
  nothing written past its 64-byte groups; the broken pattern fails it too.
- `make -C ps3-spu test` passes; the package build runs it big-endian (scalar path) in the ppc64
  chroot.

## 5. On the PS3 (to run)

After `make deploy PKGS="libspeq0"` (the user's call), as tibi over SSH, desktop stopped
(`ps3-bench.sh run LABEL -- cmd` or by hand):

    speq-dxt --check --path spu                 # RGB cases on the SPEs: "path spu, RGB sources auto", 0 failures
    speq-dxt --check --path spu --rgb ppe       # the switch: RGB stays on the PPE, 0 failures
    speq-dxt --check --path ppe                 # the reference on big-endian PPE code
    speq-dxt --bench --fmt dxt1 --comps 3       # RGB DXT1: ppe hot/fresh, ppe expand (the staging copy alone), spu hot/parked/fresh
    speq-dxt --bench --fmt dxt1 --comps 4       # the same with RGBA, to compare
    SPEQ_DEBUG=1 SPEQ_S3TC=spu gl-dxt           # through glTexImage (RGBA)

`dxt-test.spu` (cycles of the expansion): on the host `make -f ps3-spu/spu/dxt-test.mk
O=build/tmp/dxt-test`, copy `build/tmp/dxt-test/dxt-test.spu` over, `spurun dxt-test.spu
result`: `PASS ...; rgb expand N cyc/16px`.

Games (`make bench-push`, then on the PS3 in `/var/tmp/bench`), same boot, interleaved:

    STALLS=1 ./run.sh bench sauerbraten "rgbspu rgbppe" 3
    STALLS=1 ./run.sh bench openarena "rgbspu rgbppe" 3      # first: grep -c 'comps 3' in a game.log

Look at the webcam pictures (bench-cam), then `report.py` on the run directories: stall_cpu_s,
longest_ms, load; and in game.log the `speq: s3tc ... comps 3` lines: `spu` and a few hundred us
for the big ones in rgbspu, `ppe (SPEQ_S3TC_RGB=ppe)` and the old 43-165 ms in rgbppe.
Expected (estimate): an RGB 1024x1024 in about 1 ms on four SPEs instead of 165 on the PPE.
