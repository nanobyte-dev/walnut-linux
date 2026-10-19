# Backlog: Mesa nv30, ps3rsx and the RSX

Graphics work not done yet: a validator gap, the game-side batching still to do, ps3rsx's cheaper
submission path, the GPU features nobody uses (tiling, Z-cull, vertex texture fetch, ARL,
instancing), and the RSX probes that never ran. The measured state of the driver is in
[DRIVER.md](../DRIVER.md), [RSX.md](../RSX.md) and [HOTPATHS.md](../HOTPATHS.md). Items are in rank
order; the rank is the one in [the ranked list](README.md). Terms: [glossary](../README.md#glossary).

Effort: S = a day or less, M = days, L = weeks. A **wedge** is a graph exception that stops the RSX
until a console reboot (a trip through the XMB), so risky GPU tests go last in a session.

## Safety

### Validator checks for the vertex texture methods

Rank 12. ps3rsx checks the vertex texture units (methods 0x900-0x97c) as it checks the fragment
units.

**Why.** The validator lets 0x900-0x97c through with unchecked data (`ps3rsx/src/validate.c:47`).
Any client can send a bad vertex texture format or offset there and wedge the RSX. Nothing uses
them today (Mesa's vertex sampler count is 0), which is why it is cheap to close now and required
before vertex texture fetch.

- **Payoff:** removes a wedge any client can cause. **Effort:** S.
- **Risk:** low (a reject path; test it with rejects, not with a real wedge).
- **Needs:** host, then PS3 (`rsx-suite.sh all` and a raw-command test that expects the reject).
- **Status:** never tried.
- **Source:** PLAN 4.6, the old work-queue item 6; [archive/RSX-SPU-FEASIBILITY.md, "Vertex texture fetch"](../archive/RSX-SPU-FEASIBILITY.md#vertex-texture-fetch).

**To start.** The checks the research lists: FORMAT's location bits like the other DMA fields; only
the two fp32 formats (2D, no mipmap) when CONTROL0 enables a unit; OFFSET plus size bounds-checked
against the owning buffer, like the fragment textures.

## Fewer CPU cycles per frame

### Game-side batching in the remaining games

Rank 16. Fewer, larger GL calls in Sauerbraten, SuperTuxKart's legacy renderer and OpenTTD's GL
driver.

**Why.** Batching in the games beat driver work by far: X-Moto 44.5 -> 166.5 fps (18,500 -> 165 GL
calls a frame), Extreme Tux Racer 52 -> 59. Where the game is GPU-bound or lightly loaded the gain
is small: dsda-doom +3%, OpenArena opengl1 neutral, ClassiCube -4% steady with better lows until
`CC_GL_CHUNKS=1` (+4.7%, batch 16). Sauerbraten's cube2 0007-0009 (`gluniformcache`, `glbatchtmu`,
`glpartvbo`, all 1 by default since 33148f2) have never been A/B'd on the PS3: the recipe's
`allbatchoff` variant exists and has no row. Sauerbraten spent 72% of its CPU in Mesa in the HOTPATHS
profile (44.9 points of it the vertex conversion that 0033 and cube2 0011 have cut since), SuperTuxKart `lean` runs at a 1% low of 11.4 (row 68), OpenTTD reaches 21 fps with its GL
driver.

- **Payoff:** medium: large where a game is CPU-bound in the driver stack, nothing where it isn't.
- **Effort:** M per game. **Risk:** low: each change behind a switch, pixels checked on llvmpipe.
- **Needs:** host, PS3.
- **Status:** never tried for SuperTuxKart and OpenTTD; Sauerbraten's patches are on but
  unmeasured. The gameplay profiles of batch ssdroot2 P1b, symbolised
  ([HOTPATHS.md](../HOTPATHS.md#inside-the-graphics-stack)), put SuperTuxKart's client arrays at
  17.6% of its CPU (vertex copies 10.1, index scans 6.5: Mesa 0044 takes most of the scans; the
  copies need VBOs in Irrlicht's legacy driver).
- **Source:** PLAN 3.1; [archive/HOTPATHS.md, "ClassiCube's batching measured slower: GPU-bound"](../archive/HOTPATHS.md#classicubes-batching-measured-slower-gpu-bound).

**To start.** Count GL calls and draws per frame on llvmpipe forced to GL 2.1 (`tools/glcount`),
then `perf` the PS3 run with `--proc-map-timeout=20000` (without it most samples land in
`[unknown]`). Each change gets a switch and a recipe variant. For Sauerbraten:
`/var/tmp/bench/run.sh bench sauerbraten "base allbatchoff" 3`.

### ps3rsx shadow_fast

Rank 17. The submission ioctl's state shadow replayed by runs of methods (module parameter
`shadow_fast`, default off).

**Why.** The submission path is 5.4% of every game's CPU on average (HOTPATHS, 9 games);
`ps3rsx_shadow_burst` parses every pushed word into the channel's shadow with load-hit-store stalls.
`shadow_fast` replays bursts by runs of one kind and walks the restore a bitmap word at a time; a
userspace fuzz found the old and new paths identical over 38M bursts and 3.8M submits (x86 and
big-endian ppc64 under qemu). Expected from instruction counts: ~60% of shadow_burst's
instructions, 0.6-1.5% of each game's CPU (ClassiCube's shadow_burst 1.95 -> ~0.8%).

- **Payoff:** low-medium (CPU for every game). **Effort:** S (built; flips at runtime).
- **Risk:** low: a shadow mismatch would garble a channel switch (the fuzz says no).
- **Needs:** PS3.
- **Status:** built (ps3rsx 0.443 in kernel .472, switch `shadow_fast`, off) and measured in batch
  ssdroot2 P2 (`PS3RSX_SHADOW_FAST=1` against `-`, ClassiCube, Sauerbraten, X-Moto, x2): no gain in fps,
  stalls or CPU beyond the spread ([HOTPATHS.md](../HOTPATHS.md#ps3rsxs-submission-path)). The
  profiles put the shadow work at 1.2-2.3% of the CPU, so little can be won; it stays off and the
  rest of the suite is not worth a batch.
- **Source:** PLAN 1.9; [archive/HOTPATHS.md, "ps3rsx's submission path"](../archive/HOTPATHS.md#ps3rsxs-submission-path-ps3rsx-shadow_fast).

Not done after it: a restore of only what other channels changed since this one ran (a per-channel
stale bitmap; needs a wider proof), and the rest of the ioctl (`ps3rsx_validate`, the push copy,
the reservation locks), which `shadow_fast` leaves untouched.

### Indirect constant addressing (ARL) in vertex programs

Rank 22. Indexed constant loads in nv30 vertex programs (ARL, already encoded by nvfx).

**Why.** GPU skinning runs but is unusable: Sauerbraten at 11 fps by its own counter with GPU
skinning against 28 on the CPU, which is why cube2 skins on the CPU by default. Without indirect
addressing, programs compile to if/else trees, which also inflate OpenArena opengl2's shaders and
its compile RAM.

- **Payoff:** medium: GPU skinning becomes usable, smaller shader trees.
- **Effort:** M-L. **Risk:** medium: a wrong program can wedge the RSX; the 512-instruction limit.
- **Needs:** host, PS3. **Status:** never tried.
- **Source:** PLAN 3.4; [archive/GAME-PLAN.md, "Driver work"](../archive/GAME-PLAN.md#driver-work-seeded-now-ranked-by-the-smoke-tests).

### The dcbz memcpy trap

Rank 31. Count the alignment faults of memcpys into VRAM mappings, then replace them with a store
loop.

**Why.** glibc's Cell memcpy (`__memcpy_cell`) uses `dcbz`, which faults on cache-inhibited memory;
the kernel emulates it per 128-byte line, so a memcpy into a VRAM mapping runs at 19 MB/s. On the
desktop, antialiased drawing (the CPU texture-copy path) took glxgears from 58 to ~12 fps.

- **Payoff:** medium for the desktop. **Effort:** S to check, M to fix. **Risk:** low.
- **Needs:** PS3. **Status:** never tried.
- **Source:** PLAN 4.14; [archive/RSX-SPU-FEASIBILITY.md, "Recommended experiments"](../archive/RSX-SPU-FEASIBILITY.md#recommended-experiments) 2; [archive/DESKTOP-PLAN.md, "Later"](../archive/DESKTOP-PLAN.md#later).

**To start.** `perf stat -e alignment-faults,emulation-faults rsx-probe cpu` (without those events,
`CONFIG_PPC_EMULATED_STATS` gives `/sys/kernel/debug/powerpc/emulated_instructions/dcbz`); expect
~65k faults per 8 MiB memcpy. The same on X and a game: thousands a second is this bug. Then a
`std` loop and a 16-byte `vec_st` loop; the fix is a `ps3_vram_write()` helper (aligned 8-byte
stores, or 16-byte `stvx` if E8b shows a gain, never `dcbz`) in Mesa's PS3 CPU paths that write
VRAM.

### Cheaper Mesa 0028 and 0031

Rank 37. The index buffer fenced, and fragment program constants run from GART, only where needed.

**Why.** X-Moto lost 5% on Mesa ps3.299 against ps3.282 (row 47), about a frame each from 0028 and
0031. The latest profiles show them small: 0028's fence refs 0.3% in 2 games, 0031's constants
0.2%. 0028 is a race fix, so it must stay correct.

- **Payoff:** low. **Effort:** S-M. **Risk:** low (switches exist: `NV30_IDXBUF_LATE_REF`,
  `NV30_FP_CONST_VRAM`).
- **Needs:** host, PS3. **Status:** never tried.
- **Source:** PLAN 3.9; [archive/HOTPATHS.md, "Targets"](../archive/HOTPATHS.md#targets).

### CPU hot paths left over

Rank 46. The smaller targets of HOTPATHS.md that no patch took.

- **QuakeSpasm's 2D path in immediate mode** (`glBegin`/`glTexCoord2f`/`glVertex2f`, ~10% of its
  CPU). QuakeSpasm already runs at 91 fps, so this is CPU for the rest of the system.
- **u_vbuf converting static buffers at every draw.** 0033's table made the conversion cheap, and
  Mesa 0043 `NV30_VTX_SNORM8` (built, waiting for batch hotpaths3) leaves SNORM8, the only format
  the profiles show converted, to the vertex program. A cache of the converted copy (needs write
  tracking) would only matter for other formats.
- **Client vertex arrays copied per draw** (`u_upload_data`: SuperTuxKart 10.1%, ETR 2.3%). The
  min/max scans of client index arrays (`vbo_get_minmax_index_mapped`: SuperTuxKart 6.5%,
  OpenArena 1.3%, Yamagi 1.0%) have Mesa 0044 `NV30_MINMAX_VMX` (built, waiting for its A/B).
- **Mesa 0036's staging pool on QuakeSpasm and Yamagi**: measured on ClassiCube only (same fps, CPU
  -4%, row 64); expected -1.5 to -2% CPU there.

- **Payoff:** low (about 1-2% each). **Effort:** S-M. **Risk:** low.
- **Needs:** host, PS3. **Status:** never tried.
- **Source:** [archive/HOTPATHS.md, "Targets"](../archive/HOTPATHS.md#targets).

## GPU throughput and features

### Tiling and Z-cull

Rank 18. Colour and depth tiles and Z-cull through lv1 attributes 0x300/0x301 (RPCS3's encodings).

**Why.** With batching, games stop being CPU-bound: ClassiCube's process is busy 8.5 ms of a
20.7 ms frame and waits on the GPU the rest (HOTPATHS), and it was still slower than upstream's
drawing. So the old "frames are PPE-bound, defer" no longer holds. Estimated +10-30% in fill-bound
scenes.

- **Payoff:** unknown, possibly medium. **Effort:** M-L.
- **Risk:** medium-high: undocumented lv1 attributes; a wrong setup can wedge the RSX.
- **Needs:** host, PS3 (last in a session). **Status:** never tried.
- **Source:** PLAN 3.2; [archive/HARDWARE-LEVERAGE.md, "2. GPU features the stack doesn't use yet"](../archive/HARDWARE-LEVERAGE.md#2-gpu-features-the-stack-doesnt-use-yet); [archive/DRIVER-PLAN.md, "Later"](../archive/DRIVER-PLAN.md#later-pushing-the-hardware-limits-after-the-baseline-runs).

### RSX and SPU probes: E8b, E16, E17

Rank 23. Four probes RSX-SPU-FEASIBILITY.md recommends; none has run.

- **E8b, the cache-inhibited store rate done right** (test module, `rsx_ctx_probe2.c wc2`): 4 MiB of
  `VRAM_SCRATCH` mapped with `ioremap` and `ioremap_wc`, every page pre-touched before timing; time
  guarded 8-byte stores, WC 8-byte stores, WC 16-byte `stvx` and WC `std` pairs with a `sync` every
  4 KiB (`mftb`); read one page's HPTE back (`lv1_read_htab_entries`) and print `hpte_r &
  (HPTE_R_I|HPTE_R_G)` to see whether lv1 kept G=0. It decides two changes: `pgprot_noncached_wc`
  for the VRAM buffer mmap (`ps3rsx/src/mem.c:97`) and `ioremap_wc` for the VRAM swap's chunk
  mapping, each at most ~2x. The user's decision of 2026-10-17: a measurement before either doc is
  relied on.
- **E16, SPU DMA into a VRAM buffer**: 64 `mfc_put` of 16 KiB from an SPU context into a mapped VRAM
  buffer object, then a get back and a compare; rate, correctness, I|G against I only, a GART
  buffer as control. Expected: 1 GB/s or more for puts, the get slow (~16 MB/s); failure shows as a
  DATA_STORAGE event or an MFC class-0 error. It enables SPU decompression and generation straight
  into VRAM and meshes into VBOs ([spu.md](spu.md)).
- **E17, M2MF small-copy latency**: VRAM to GART copies of 1, 8, 32 and 256 pages (one op per page,
  one per contiguous run), submit to ref with the GPU idle, then while glxgears or an OpenArena
  timedemo runs. It decides the VRAM swap's thresholds.
- **spufs dispatch latency**: quoted 20-200 us, never measured here.

- **Payoff:** medium, enabling. **Effort:** S each.
- **Risk:** low: E16 writes only the test's buffer with no GPU involved (worst case an SPE error
  kills the test context); E8b is a test module.
- **Needs:** PS3. **Status:** never tried.
- **Source:** PLAN 3.3; [archive/RSX-SPU-FEASIBILITY.md, "Recommended experiments"](../archive/RSX-SPU-FEASIBILITY.md#recommended-experiments).

Also from that list: the VRAM swap's GPU path with X on a text console (`gpu_owned` false: it must
fall back cleanly) has not been run; swap-ins through it are measured (~10x faster, 0 fallbacks).

### Vertex texture fetch, derivatives, instancing

Rank 50. The NV40 features Mesa leaves off; only when content needs them (none of today's games
does).

- **Fragment derivatives.** GLSL `dFdx`/`dFdy` already reach DDX/DDY in desktop GL 2.1; only GLES2's
  `OES_standard_derivatives` is withheld (`fragment_shader_derivatives = false`, nv30_screen.c:150).
  To turn it on: an `egl-deriv` test (GLES2 and GL 2.1, `dFdx(uv)*k`, `dFdy(uv)*k`, `fwidth`, one
  case writing all four components, one under fp32 export) against softpipe within 1 LSB, then the
  cap on `is_ps3`.
- **Instancing.** The RSX has a per-attribute frequency divider (0x1fc0, frequency in VTXFMT bits
  16-31) but no instance counter. Step 1: with the cap on, `nv30_draw_vbo` loops over instances and
  re-emits VTXBUF per instanced attribute (behind an env variable, an `egl-instanced` test). Step 2,
  only after a raw-command probe (an `rsx-tri` variant, DIVIDE with frequency 3, four triangles in
  one draw of 3 x 4 vertices): the divider, with 0x1fc0 added to the validator allowlist and written
  as 0 whenever instancing is off (state is shared between channels).
- **Vertex texture fetch** (4 units at 0x900 + 0x20 n, fp32 2D nearest): the validator item above
  first; then `nv40_verttex_validate` filled in (today a stub that reads the fragment samplers),
  TXL/TEX in `nvfx_vertprog.c`, 4 vertex samplers; a non-fp32 view bound to a vertex sampler must
  never reach the hardware.

- **Payoff:** low for today's games. **Effort:** M each. **Risk:** medium (exceptions).
- **Needs:** host, PS3. **Status:** never tried.
- **Source:** PLAN 3.12; [archive/RSX-SPU-FEASIBILITY.md, "4. Instancing, vertex texture fetch and derivatives"](../archive/RSX-SPU-FEASIBILITY.md#4-instancing-vertex-texture-fetch-and-derivatives-on-nv40rsx).

### Driver limits not pushed yet

Rank 51. DRIVER-PLAN.md's "Later" rows that stay open.

| Limit | Idea |
|---|---|
| CPU access to VRAM above 224 MB not proven safe | a probe that may crash the kernel, then ps3fb's hardcoded `GPU_DDR_SIZE` lifted |
| 3D state not context-switched by the hardware (E5) | lv1 context attributes or the RAMIN layout for a hardware context save area (RPCS3, lv1 notes) |
| The context limit, and allocating up to it stalls the GPU (E1) | find which step breaks it (the third alloc, the failed fourth, the frees) |
| Exceptions wedge the GPU until a reboot | lv1 attributes not tried, a PGRAPH exception acknowledge |
| 1 MB IO pages (flags 0x820) | flag 0x2 (64 KB pages) for finer GART allocations |
| One boot hung at 11 s (netconsole?) | reproduce; delay netconsole until userspace if it recurs |
| Completion only by polling fences | lv1's "user command" completion interrupt (speculative) |
| ps3fb's GPU error dump prints badly (session task #29) | fix its printing; not done |

Whether a Petitboot-only reboot clears a wedge, and kexec from our Linux, are
[platform.md, "Dev loop questions"](platform.md#dev-loop-questions).

- **Effort:** M-L. **Risk:** medium-high (kernel crashes, wedges). **Needs:** PS3, supervised.
- **Status:** never tried.
- **Source:** [archive/DRIVER-PLAN.md, "Later: pushing the hardware limits"](../archive/DRIVER-PLAN.md#later-pushing-the-hardware-limits-after-the-baseline-runs).

### GL extensions the games worked around

Rank 52. Fixed only once a game needs one; every game that hit them has a workaround.

- **ARB_texture_rg**: needs R8 and R8G8 render targets; the hardware has a B8 colour format, a G8B8
  one is unverified. dsda-doom (a GL_LUMINANCE_ALPHA fallback) and OpenTTD (GL_LUMINANCE8) are
  patched instead.
- **ARB_shader_texture_lod**: NV40 fragment TXL; OpenTTD's shaders were patched instead.
- **ARB_framebuffer_object on nv30** (renderergl2's FBO features, Neverball's effects): the risk is
  one RT format shared by colour and depth, which mixed sizes on swizzled surfaces would
  mis-address.

- **Effort:** M each. **Risk:** medium (render-target layout errors are exceptions).
- **Status:** never tried.
- **Source:** [archive/GAME-PLAN.md, "Driver work"](../archive/GAME-PLAN.md#driver-work-seeded-now-ranked-by-the-smoke-tests).

### A failed buffer allocation must not crash

Rank 75. A full GART or VRAM ended in a segfault in 6 runs.

**Why.** [LOG-FINDINGS.md, item 2](../games/LOG-FINDINGS.md#2-a-failed-buffer-allocation-ends-in-a-segfault):
after ten `ps3rsx: out of GART for a 4096 KiB BO` Sauerbraten (5 runs) died at address 0 inside libc,
and SuperTuxKart (1 run) after 270 `out of VRAM` lines and an incomplete FBO. Not seen in the 40+
runs since, but a player with a bigger map would hit it. Also from the same triage, to watch:
OpenTTD with `NV30_STAGING_POOL_MB=24` logged `no memory to back the GART (56 of 63 MiB)` once in 13
runs since ps3.544.

- **To start.** Force it (the `ps3rsx_gart` command-line size down to 16 MiB), take a backtrace with
  debug symbols, make nv30's buffer and transfer creation return `GL_OUT_OF_MEMORY` or take the CPU
  path; send the fix upstream with the test. Repeat OpenTTD `blit8` with the 24 MB pool x6 and read
  dmesg.
- **Payoff:** no crash at the memory limit. **Effort:** M. **Risk:** low (an error path).
- **Needs:** host, PS3. **Status:** never tried.

## Not worth it (reasoned)

- **A cacheable PPE mapping of VRAM** (E8c): if lv1 refuses the HPTE, `ps3_hpte_insert` BUG()s the
  kernel; any partial line costs a 16 MB/s read-for-ownership and the GPU's writes aren't snooped.
  Ask the user before trying it.
- **Retiring fences from the vsync interrupt** (a stopgap for the lost decrementer): not needed with
  the fixed kernel.
- **Mesa 0035 `NV30_QUERY_WAIT`, 0034's fence pool, 0036's staging pool**: measured neutral in fps
  (rows 58, 64, 65), off by default.

Older versions and the dated history: [archive/HOTPATHS.md](../archive/HOTPATHS.md),
[archive/DRIVER-PLAN.md](../archive/DRIVER-PLAN.md),
[archive/RSX-SPU-FEASIBILITY.md](../archive/RSX-SPU-FEASIBILITY.md),
[archive/HARDWARE-LEVERAGE.md](../archive/HARDWARE-LEVERAGE.md), [archive/IDEAS.md](../archive/IDEAS.md).
