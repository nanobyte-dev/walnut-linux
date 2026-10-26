# CPU hot paths of the graphics stack

Where the CPU of a frame goes in Mesa nv30, its winsys and ps3rsx. Two rankings: 9 games from the
same-boot profiling batch of 2026-10-18 (`PERF=1 MEM=1`, Mesa ps3.299, kernel 6.12.111+ps3.178,
ps3rsx 0.249), and 7 games from the SSD-root batch of 2026-10-19 (Mesa ps3.476, kernel .472, ps3rsx
0.443: after 0033's table, 0036 and the games' batching patches), [below](#gameplay-profiles-on-the-ssd-root-batch-ssdroot2-p1b).
Of the changes the first one led to, one is a clear win and on by default: Mesa 0033's lookup table
for 8-bit vertex attributes (BananaBread (Debian's cube2) +53%). The fence pool (0034), the query wait (0035) and the
staging pool (0036) measured neutral and stay off; ps3rsx's `shadow_fast` showed no gain in its A/B
(batch ssdroot2, x2 on three games) and stays off. The second ranking led to Mesa 0041-0044
([third round](#third-round-submissions-snorm8-attributes-index-scans)): fewer submissions
(`NV30_FENCE_WORK_KICK`, `NV30_QUERY_KICK`), SNORM8 attributes normalized in the vertex program
(`NV30_VTX_SNORM8`) and AltiVec index scans (`NV30_MINMAX_VMX`), built in Mesa
26.2.4-1+ps3.545~dev, off by default. 0041, 0042 and 0044 are measured ([below](#measured-0041-0042-0044)): 0041 and
0042 neutral, 0044 +10% on SuperTuxKart at n=5 (a candidate default) and neutral elsewhere. 0043 as built there wedged the RSX
on its first run (graph exception 0x106: [below](#snorm8-attributes-in-the-vertex-program-mesa-0043));
the fixed 0043 is in Mesa 26.2.4-1+ps3.570~dev (`build/debs/hotpaths3b/mesa`), not yet run. How the profiles are taken is in
[PROFILING.md](PROFILING.md); nv30, ps3rsx and the other terms are in the [glossary](README.md#glossary).

## Method

### Why the PS3-side text files were unusable

Two problems turned up before any ranking could be made. Both are fixed on the PS3 side since
([PROFILING.md](PROFILING.md#pitfalls)), but this batch's text files stay wrong:

- **`perf.folded` and `perf-leaf.txt` hold only a few samples** (1 to 621 per run, from the first
  0.1-2 s), so report.py's `perf_*` columns are wrong for the whole batch (OpenArena 011212 shows
  `perf_unk=100` from a single sample). `perf script` stopped early every time, right after
  `cmd_addr2line_configure ... addr2line configuration failed` in `perf-post.err`. `perf-report.txt`
  and `perf-threads.txt` (perf report) are complete.
- **perf missed every mmap made while the record was disabled.** ps3-bench.sh starts perf with `-D -1`
  during the game's load and enables it at the window, and libraries the game dlopens in that time
  (libGLX_mesa and libgallium come in at context creation) got no MMAP2 record, so their samples are
  `[unknown]` in the PS3's own report. In OpenArena 011212 that was 25.7% of the game's samples;
  Luanti, BananaBread, X-Moto and Armagetron lost libgallium the same way.

On this batch's perf.data the fixed perf-post.sh now gives this tool's class shares to the tenth.

### Symbolised again on the PC

`bench/hotpaths/` reads `perf.data` directly (big-endian PERFILE2, call chains, MMAP2 records
with build ids) and symbolises it on the PC:

- the game's maps are perf's MMAP2 records, with the holes filled from the run's `mem/mid.smaps`
  (MEM=1), which has libgallium where perf has nothing;
- user frames come from the debug files of the exact versions (build ids match): Mesa's dbgsym debs
  from `build/debs/mesa`, Debian's `libc6-dbg` 2.43-7, the runtime `.so`s for the ELFv1 function
  descriptors (`.opd`); the kernel from the 6.12.111+ps3.178 image's System.map, ps3rsx and drm from
  their `.ko`, placed at the module addresses perf recorded;
- ppc64's call chain stores the link register as the second entry, which duplicates the caller when
  the leaf has saved it already. perf on the PS3 checks that against the CFI; this tool drops the
  entry when it equals the next one or falls in the leaf's own function;
- checked against the PS3's `perf-report.txt`: the same top symbols with the same self shares
  (ps3rsx_shadow_burst 1.73% here against 1.78% there in OpenArena, __memcpy_cell 2.22/2.22%,
  RB_StageIteratorGeneric, Resample_Cubic_C), apart from the samples the PS3 had as `[unknown]`.

Classes are report.py's (`perf_game`, `perf_mesa`, ...), by the DSO of the sampled instruction, with
libc's memcpy/memset family split out (`libcmem`). nv30's winsys (nouveau_pushbuf, nouveau_bo, fences)
is built into libgallium, so `drm` (libdrm itself) is nearly empty and the winsys counts as `mesa`.

Hot paths: each sample gets the category of its **innermost Mesa frame that matches a rule** (scanning
from the leaf outwards), so memcpy, kernel and ps3rsx time under a Mesa function counts for that
function's work: `memcpy < u_upload_data` is vertex upload, the kernel under `pushbuf_flush` is
submission, `nouveau_bo_new` under `nouveau_fence_new` is a fence's buffer. The rules are in
`categorize.py`. Cross-game numbers are the **mean over the games of the % of each game's CPU** (every
game weighs the same), with the number of games where the path takes at least 1% (0.5% for single
functions).

### Runs used and left out

The run list was `build/tmp/prof-runs.txt` (ephemeral). Webcam photos (60 s and 150 s into each run)
were checked for every run:

| Run | Picture | Used |
|---|---|---|
| openarena 011212 (PERF+MEM) | loading at 60 s (warm-up), gameplay at 150 s | yes, the OpenArena profile |
| openarena 001600, 003017 (PERF) | gameplay | cross-check only: no MEM smaps, but perf had Mesa's maps there; class shares within 1% of 011212 |
| openarena 001126, 002131, 002559, 003614 | gameplay | overhead runs, no profile |
| armagetronad 004040 | gameplay at 60 s, **black screen with a cursor at 150 s** | **no**: samples only in the first 20 s of the 120 s window (3973 + 502, then none) |
| openttd 011738 | **main menu with the survey dialog**, not gameplay | **no**, and perf recorded no samples |
| dsda-doom 005026 | gameplay | **no**: perf recorded no samples (`perf_win` empty) |
| classicube, etr, luanti, neverball, quakespasm, sauerbraten, xmoto, yquake2 | gameplay (QuakeSpasm dark but in the level) | yes |

So 9 games: OpenArena, ClassiCube, ETR, Luanti, Neverball, QuakeSpasm, BananaBread, X-Moto, Yamagi
Quake II. perf's cost on fps is within the noise here: OpenArena 50.7, 50.5, 52.7 fps profiled against
52.7 and 50.2 without (and 52.9, 54.2 with the stall recorder).

## Per game: CPU by class

% of the game process's samples (all its threads) in the window. `cpu` = threads busy on average
(samples / 499 Hz / window); `gfx` = samples with a Mesa frame on the stack or a ps3rsx/drm leaf (the
whole graphics stack, game-side GL calls included); `xorg` = Xorg's samples as % of the game's.

| Game | fps | cpu | game | mesa | drm | kgpu | kernel | libc mem* | libc | other | gfx | xorg |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| [OpenArena](../gamedb/games/openarena/) | 52.7 | 0.73 | 23.2 | 27.3 | 0.0 | 4.7 | 10.8 | 3.7 | 3.2 | 27.0 | 39.7 | 3.8 |
| [ClassiCube](../gamedb/games/classicube/) | 43.4 | 0.38 | 16.1 | 41.3 | 0.1 | 8.0 | 18.0 | 1.1 | 3.7 | 11.6 | 65.0 | 7.5 |
| [ETR](../gamedb/games/etr/) | 56 | 0.81 | 13.1 | 15.5 | 0.0 | 3.0 | 8.1 | 4.0 | 10.6 | 45.6 | 23.6 | 3.6 |
| [Luanti](../gamedb/games/luanti/) | 45 | 1.00 | 44.7 | 14.8 | 0.0 | 2.5 | 13.6 | 6.2 | 7.8 | 10.4 | 21.0 | 3.0 |
| [Neverball](../gamedb/games/neverball/) | 80.5 | 0.40 | 10.1 | 40.9 | 0.1 | 8.6 | 13.1 | 2.8 | 10.1 | 14.3 | 60.8 | 12.2 |
| [QuakeSpasm](../gamedb/games/quakespasm/) | 74.4 | 0.51 | 24.3 | 42.9 | 0.1 | 5.5 | 14.0 | 0.8 | 6.3 | 6.2 | 60.5 | 9.5 |
| [BananaBread](../gamedb/games/bananabread/) | 22.1 | 1.10 | 7.3 | 72.0 | 0.1 | 3.6 | 6.2 | 0.8 | 2.6 | 7.5 | 80.3 | 1.6 |
| [X-Moto](../gamedb/games/xmoto/) | 154 | 0.91 | 39.9 | 22.0 | 0.1 | 3.6 | 9.1 | 6.4 | 7.6 | 11.2 | 39.0 | 11.0 |
| [Yamagi Quake II](../gamedb/games/yquake2/) | 55.3 | 0.69 | 30.5 | 24.6 | 0.1 | 3.8 | 11.4 | 2.7 | 5.5 | 21.4 | 38.1 | 6.0 |

What `other` is: OpenAL and libdeflate in OpenArena (17% and 5%), freetype + harfbuzz (29%) and SFML
audio in ETR, LuaJIT in Luanti. The game process's `kernel` time is mostly system calls: DRM ioctls
4.7% on average, read/poll/futex 3.8%, mmap/munmap 1%. Xorg's own CPU (6.5% of the games' on average)
is a third kernel and a sixth its own code; its glamor/ps3rsx part is small (shadow_restore 0.18%,
the channel switch back to the game).

These fps are of that night's builds and demos; current ones are in [games/README.md](games/README.md).

## Cross-game ranking of the graphics hot paths

Mean % of a game's CPU, number of games at 1% or more, and per game:

| Hot path (innermost Mesa frame) | mean | games | OA | CC | ETR | Lu | NB | QS | Sb | XM | YQ2 | what is under it |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| st/mesa per-draw state (`st_prepare_draw`, `_mesa_update_*`, texture state, ff programs, u_vbuf bookkeeping, buffer binds) | 13.1 | 9 | 15.1 | 18.3 | 8.2 | 7.5 | 22.6 | 12.4 | 8.6 | 12.6 | 12.5 | own code 11.1 |
| **vertex format translation** (u_vbuf `generic_run`, SNORM8 unpack) | 5.7 | 2 | 0.1 | 0 | 0 | 0 | 0 | 6.1 | **44.9** | 0 | 0 | own code |
| **submission** (`pushbuf_flush`: the pushbuf ioctl) | 5.4 | 9 | 5.3 | 8.4 | 3.4 | 3.1 | 10.3 | 5.6 | 4.3 | 4.3 | 4.1 | ps3rsx 3.9, kernel 1.3 |
| GL API entry and immediate mode (`_mesa_*`, dispatch stubs) | 5.0 | 9 | 2.4 | 5.1 | 2.9 | 2.5 | 6.7 | 15.3 | 2.8 | 2.1 | 5.0 | QuakeSpasm: glBegin/glTexCoord2f/glVertex2f ~10% |
| nv30 state validation (`nv30_state_validate` and its `*_validate`) | 3.6 | 9 | 4.1 | 6.3 | 2.3 | 2.1 | 6.0 | 3.2 | 2.3 | 3.2 | 2.7 | own code |
| **buffer allocation and free** (`nouveau_bo_new`/`_ref`) | 2.4 | 8 | 1.8 | 6.3 | 0.9 | 1.0 | 1.9 | 3.8 | 2.2 | 1.5 | 2.5 | kernel 1.3, ps3rsx/drm 0.8 |
| winsys bookkeeping (`pushbuf_kref`, `pushbuf_validate`, bufctx, fence refs) | 2.2 | 8 | 1.9 | 5.9 | 0.9 | 1.4 | 2.7 | 1.8 | 1.7 | 1.4 | 1.6 | own code |
| vertex/index upload (`u_upload_data`, min/max index scans) | 1.9 | 4 | 3.4 | 0.2 | 2.3 | 0.4 | 0.9 | 0.8 | 0.9 | 5.9 | 2.7 | memcpy 1.1 |
| swap and present (DRI3) | 1.9 | 7 | 1.2 | 2.2 | 0.9 | 1.0 | 3.5 | 2.8 | 0.5 | 3.5 | 1.6 | kernel 1.0 (X round trips) |
| Gallium HUD (the bench harness's) | 1.3 | 5 | 0.8 | 1.8 | 0.7 | 0.7 | 2.3 | 1.8 | 0.3 | 2.3 | 1.1 | number formatting |
| nv30 draw emit (`nv30_draw_vbo`) | 1.2 | 3 | 1.4 | 4.2 | 0.4 | 0.6 | 1.3 | 0.7 | 0.5 | 0.4 | 0.8 | own code |
| **query result wait** (`nv30_query_result` spin) | 1.2 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | **10.3** | 0 | 0 | own code: a busy loop |
| texture upload (transfer map, texstore, pack) | 1.0 | 3 | 0.7 | 1.8 | 0.1 | 0.1 | 0.8 | 3.4 | 0.3 | 0.2 | 1.8 | |
| buffer map (`nouveau_bo_map`, staging) | 0.6 | 2 | 0.4 | 3.1 | 0 | 0 | 0.1 | 1.0 | 0 | 0 | 0.9 | kernel |
| fence wait | 0.6 | 2 | 0.4 | 1.2 | 0.1 | 0.3 | 1.2 | 1.0 | 0 | 0.7 | 0.7 | kernel |
| fragment program constants (0031) | 0.2 | 0 | 0.1 | 0.1 | 0.1 | 0.2 | 0.3 | 0.4 | 0.2 | 0.3 | 0.1 | |

Top single functions (self time; memcpy counted at its Mesa caller), mean % and games at 0.5%+:

| Function | mean | games | notes |
|---|---|---|---|
| util_format_r8g8b8a8_snorm_unpack_rgba_float | 2.5 | 2 | BananaBread 18.2, QuakeSpasm 4.5 |
| util_format_r8g8b8_snorm_unpack_rgba_float | 1.8 | 1 | BananaBread 16.4 |
| ps3rsx_shadow_burst (kernel) | 1.3 | 9 | the software context switch's per-word parse of every push |
| nv30_query_result | 1.1 | 1 | BananaBread 10.3 |
| nv30_state_validate | 1.1 | 9 | ClassiCube 3.0 |
| memcpy < u_upload_data | 1.1 | 4 | X-Moto 5.3, ETR 1.9: client vertex arrays |
| nv30_draw_vbo | 1.0 | 6 | ClassiCube 3.9 |
| _mesa_update_texture_state | 0.9 | 5 | fixed-function texture state per state change |
| generic_run | 0.9 | 2 | the translate loop around the unpacks |
| nv30_vbo_validate | 0.7 | 3 | |
| u_vbuf_draw_vbo | 0.6 | 7 | |
| pushbuf_kref | 0.6 | 5 | |
| ps3rsx_shadow_restore (kernel) | 0.6 | 4 | channel switches (Xorg/game) |
| util_set_vertex_buffers_mask | 0.6 | 1 | ClassiCube 2.9 |
| st_prepare_draw | 0.5 | 3 | |

Inclusive, for scale: `glXSwapBuffers` 9.0% on average (Neverball 18, ClassiCube 16), mostly the flush
it does; `pushbuf_flush` 6.4% (9 games); `nv30_draw_vbo` 6.9%; `st_prepare_draw` 5.7%;
`nv30_context_kick_notify` 0.9%, of which `nouveau_fence_new` 0.7.

## Targets

The five targets the ranking gave, numbered as the code and other docs cite them, and what became of
each. Estimated savings were % of each game's CPU, from the profile shares and what the change removes;
a frame gets that much shorter only where the time sits on the game's render thread and the game is
CPU-bound. X-Moto (154 fps) and Neverball (80) had headroom; BananaBread (22), Luanti (45) and
ClassiCube (43) didn't.

| # | Target | Share | Built | Outcome |
|---|---|---|---|---|
| 1 | u_vbuf's SNORM8 vertex conversion | BananaBread 44.9%, QuakeSpasm 6.1% | Mesa 0033 `NV30_TRANSLATE_LUT` | BananaBread +53% ([byte-normals](../gamedb/engines/cube2/investigations/2026-10-18-byte-normals.md)); **on by default** |
| 2 | The submission ioctl in ps3rsx | 5.4% in all 9 games | ps3rsx `shadow_fast` | no gain resolved in the A/B (ClassiCube, BananaBread, X-Moto, x2), stays off |
| 3 | Per-draw state work in st/mesa and nv30 | about 20% together, diffuse | nothing in Mesa; game-side batching | no single patch takes more than ~1% here; the lever is fewer draws in the games |
| 4 | A buffer allocation and free per fence | 1.3% average | Mesa 0034 `NV30_FENCE_BO_POOL=N` | neutral, off |
| 5 | A query result waited for mid-frame | 10.3% of BananaBread | Mesa 0035 `NV30_QUERY_WAIT` | no fps gain, off; the game-side lever is cube2 0010 ([water-query-wait](../gamedb/engines/cube2/investigations/2026-10-18-water-query-wait.md)) |

Smaller ones from the same profiles: the staging buffer per texture upload (ClassiCube 5.4,
QuakeSpasm 2.2, Yamagi 1.7: Mesa 0036 `NV30_STAGING_POOL`, neutral); client vertex arrays copied per
draw (X-Moto 5.5, fixed in the game: [client-array-copies](../gamedb/games/xmoto/investigations/2026-10-18-client-array-copies.md); ETR 2.3, not done); the min/max scans of client index
arrays (`vbo_get_minmax_index_mapped`: OpenArena 1.3, Yamagi 1.0; the index side is 0030's). The
Gallium HUD costs 1.3% in bench runs only (number formatting), not in play. 0028 (index fencing) shows
as `nouveau_fence_ref < nv30_state_validate`, 0.3% in 2 games; 0031's fragment program constants are
down to 0.2%.

### Target 1: the SNORM8 vertex conversion (Mesa 0033)

nv30 has no signed 8-bit vertex type (RSX: S1, F, SF, UB, S32K, CMP, UB256), so u_vbuf turns
GL_BYTE-normalized normals and tangents into 32-bit floats on every draw, for the whole index range,
static VBOs included. translate_generic does that with the format's unpack function and an emit
function per vertex and attribute, and every byte passes an integer-to-float conversion, which the
PPE does through memory (store, load into an FPR, a load-hit-store stall of tens of cycles per
component). The estimate was 35-40% of BananaBread's CPU and 4-5% of QuakeSpasm's.

**0033 translate: 8-bit vertex attributes to floats through a table (`NV30_TRANSLATE_LUT`).** In
translate_generic, an element whose input is a plain 8-bit array format with identity swizzle (R8 to
R8G8B8A8; UNORM, SNORM, USCALED, SSCALED) and whose output is R32..._FLOAT with as many channels gets
a table per channel: 256 entries holding the bits the format's own unpack function returns for each
byte value. The per-vertex loop then does one table load and one store per channel, no float
conversion, no calls. Bit-identical by construction. Checked on the PC by building translate_generic
with the generated format tables and running all 16 formats through `run`, `run_elts`, `run_elts16`
and `run_elts8` (index clamping, two buffers, mixed with copied attributes) with the switch off and
on: identical output, the table path taken for all 16 formats when on and never when off. llvmpipe
can't show it (it fetches SNORM8 itself, no u_vbuf). On by default since Mesa ps3.408;
`NV30_TRANSLATE_LUT=0` turns it off (shown as `base@translate_lut0`).

Where the conversion came from, BananaBread's world normals as signed bytes, and the game-side fix
(cube2 0011, +17% on top of 0033) are the engine's investigation
[byte-normals](../gamedb/engines/cube2/investigations/2026-10-18-byte-normals.md).

### ps3rsx's submission path

Target 2. Under `ps3rsx_ioctl_gem_pushbuf` (ClassiCube 7.9% of its CPU, BananaBread 3.9, X-Moto 4.0) the biggest
leaf is `ps3rsx_shadow_burst` (1.95, 0.85, 0.93), then `ps3rsx_validate` (0.4 on average), the restore
at channel switches (0.26-0.62), the push copy (0.3) and the reservation locks (0.3). An instruction
histogram of shadow_burst (ClassiCube) put ~40% of it in the copy/2D object path (the row multiply
redone per word, two read-modify-writes of bitmap words per word) and ~45% in the vertex program and
constant upload windows (`vp_ptr`/`vp_word` loaded and stored through memory every word, the slot's
set and dirty bits set again for every word of a slot): load-hit-store stalls on the PPE. The restore
is already skipped while the same channel keeps the GPU (`last_chan`); it runs at every game/Xorg
alternation.

- **ps3rsx `shadow_fast`** (module parameter, bool, 0644, default off; in ps3rsx since 0.250, shipped
  in 0.443 for kernel .472). `burst_fast()` replays a burst by runs of methods of one kind (a table
  `run3d` built at load): 3D state runs as a copy and one mask OR per bitmap word, the upload windows
  with the pointer and word in registers and each slot's bits set once, copy/2D objects with the row
  computed once and runs split at their side-effect methods (0x100; 0x328 for M2MF, 0x40c for SIFM)
  and 0x410. `restore_fast()` emits the same restore stream in the same order, walking the bitmaps a
  word at a time inline and comparing slots as two u64. The old code stays the default path. The
  parameter is read at every burst and restore and both paths leave identical state, so it can be
  flipped at run time: `echo 1 | sudo tee /sys/module/ps3rsx/parameters/shadow_fast` (the bench's
  ENVS token `PS3RSX_SHADOW_FAST=1` does it per run).
- Equivalence, `packages/walnut/drivers/ps3rsx/test/` (`make && make run`; `make test ONLY=ps3rsx-shadow`): shadow.c built in
  userspace twice (slow and fast path) and fuzzed with Mesa-like pushes (binds of every object, host
  methods, inc and non-inc bursts, upload pointers in and out of range and near 2^32, runs across
  every split point, all subchannels), plus whole submits on 3 channels as submit.c runs them (sync,
  restore with overflow, take_restored, pushes, take, kernel-copy writes, forgotten GPU state): 38M
  burst/push cases and 3.8M submits on x86, under ASan/UBSan and on big-endian ppc64 (qemu), the
  shadow structs and the restore buffers identical after every step. 14 of 15 planted bugs were
  caught (the 15th changes nothing).
- Expected (ppc64 instructions per payload word, `packages/walnut/drivers/ps3rsx/test/count.sh`): 2D runs -51%, vertex program
  uploads -77%, constants -69%, short 3D state runs -35%, one restore -56%. Weighted by the histogram
  that is ~60% of shadow_burst's instructions, more in cycles (the removed work is the load-hit-store
  kind). Per game ~0.6-1.5% of the CPU (ClassiCube's shadow_burst 1.95 -> ~0.8, restore ~-0.3), less
  than the 2-3% target: validate, the push copy and the reservation locks are untouched.

**Measured (batch ssdroot2).** ps3rsx 0.443 on kernel .472, `PS3RSX_SHADOW_FAST=1` against off,
`STALLS=1`, x2 interleaved, the same boot:

<!-- gamedb:batch-table 2026-10-19-ssdroot2-shadow-fast games=classicube,bananabread,xmoto metrics=fps_steady,low1_play,stall_play_s,cpu -->
| Game | Variant | `fps_steady` (fps) | `low1_play` (fps) | `stall_play_s` (s) | `cpu` (s) |
|---|---|---|---|---|---|
| [ClassiCube](../gamedb/games/classicube/) | base@rec10 | 46.1 | 30.1 | 0 | 80.3 |
| [ClassiCube](../gamedb/games/classicube/) | base@rec10+rsx_shadow_fast1 | 46.1 | 29.7 | 0 | 86.2 |
| [BananaBread](../gamedb/games/bananabread/) | base@rec10 | 39.2 | 25.6 | 0.1 | 233.7 |
| [BananaBread](../gamedb/games/bananabread/) | base@rec10+rsx_shadow_fast1 | 39.7 | 26.1 | 0.1 | 229.4 |
| [X-Moto](../gamedb/games/xmoto/) | base@rec10 | 158.4 | 98.1 | 0 | 303.5 |
| [X-Moto](../gamedb/games/xmoto/) | base@rec10+rsx_shadow_fast1 | 159 | 97.4 | 0 | 301.1 |

Numbers: gamedb:batch/2026-10-19-ssdroot2-shadow-fast (batch ssdroot2 P2: ps3rsx shadow_fast (PS3RSX_SHADOW_FAST=1 against off, STALLS=1), 2026-10-19; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

No difference beyond the runs' spread (ClassiCube's CPU alone ranged 79.8-86.6 s over the boot with the
same configuration). The estimate said 0.6-1.5% of the CPU, which fps cannot show at n=2 in games that
are limited elsewhere (ClassiCube by the GPU, X-Moto at 158 fps, BananaBread by Mesa). The switch stays
off; the profiles of the same night put `ps3rsx_shadow_burst` at 1.2-2.3% of the CPU and
`ps3rsx_validate` at 0.5-1.4% (ClassiCube 3.6% together), so even all of it removed would be
under 2%. Running it on the rest of the suite would add no information a profile has not given
([TESTS.md](TESTS.md), "Next").

### Target 3: per-draw state work

Earlier Mesa rounds (0016-0025) took the nv30 part down. What is left: st-validate 13.1 +
nv30-validate 3.6 + winsys bookkeeping 2.2 + draw emit 1.2, about 20% together, spread over dozens of
functions of 0.2-1% each. The biggest single ones: `_mesa_update_texture_state` (0.9, 5 games),
`nv30_state_validate` self (1.1, 9 games), `util_set_vertex_buffers_mask` (ClassiCube 2.9),
`pushbuf_kref`/`pushbuf_validate` (0.9, 0017's area). No single patch takes more than about 1% here.
The lever is fewer draws in the games, which ClassiCube's chunk merging showed (below), and immediate
mode out of QuakeSpasm's 2D path (~10%).

### Target 4: a buffer per fence (Mesa 0034)

Every fence makes a 4 KiB GART buffer for the kernel to block on, and nv30 makes a fence per
submission: `nouveau_fence_new` 0.7% and the frees under `_nouveau_fence_ref` 0.6% on average, all 9
games, mostly kernel time (GEM create/close, drm_mm placement). 0027's adaptive kick makes more
submissions, so more of these. The estimate was 1-1.5% per game (BananaBread 2.1, ClassiCube and
Neverball 1.6).

**0034 nouveau: recycle the wait buffers of deleted fences (`NV30_FENCE_BO_POOL=N`).** Up to N (at most
32) buffers of deleted fences go into a pool on the screen's fence list, and the next fences take them
instead of allocating. A fence is deleted only once signalled (the fence list holds a reference from
its emit until then) or if it was never emitted, so no batch the GPU still runs references a recycled
buffer; the fence emit only references the buffer in the batch, its contents are never written or
read. The pool has its own lock (nouveau_fence_new runs both with and without the fence list's lock
held) and is drained before the screen's device goes away. Off by default.

### Target 5: the query result wait (Mesa 0035)

`nv30_query_result` waits for a query's report by spinning, taking issue slots from the PPE's other
hardware thread. Only BananaBread showed it (10.3% of its CPU, one call site: the engine asks for its
water's occlusion query a few passes after issuing it, while the GPU is still behind on the frame;
[water-query-wait](../gamedb/engines/cube2/investigations/2026-10-18-water-query-wait.md), with the
game-side fix, cube2 0010 `oqwaterlag`, and the measurements).

**Mesa 0035 `NV30_QUERY_WAIT=US`** (unset: spin as before). A query's end holds the context's current
fence, which the query's kick emits after the report. A wait spins at most US microseconds at low SMT
priority (`or 1,1,1`), then sleeps in the kernel on that fence (`nouveau_fence_wait`, ps3rsx's
`GEM_CPU_PREP`, which its fence timer wakes within 250 us), then spins on what is left with
`sched_yield` (the kernel's fence follows the FIFO, which may be a little ahead of the 3D back end
writing the report). Same result, same pixels; the frame waits as long (or up to ~250 us longer per
wait), but the PPE's other thread gets the core meanwhile. `0` sleeps at once; `50` keeps short waits
spinning. Measured: no fps gain, off.

### The staging buffer per upload (Mesa 0036)

[ClassiCube](../gamedb/games/classicube/) calls `glTexSubImage2D` every frame (the animated water and lava tiles), and
`nv30_miptree_transfer_map` made a new staging buffer for each: `nouveau_bo_new` (GEM create, 1.6%),
`nouveau_bo_map` (an mmap, 3.0%: the ppc64 slice search and VMA setup in the kernel), and the fence
work after the copy freed it (munmap 2.4%, GEM close 0.7%). **8.6% of ClassiCube's CPU** in the
second-round profile (004514).

**Mesa 0036 `NV30_STAGING_POOL=N`** (N up to 32; 0, the default: off). Write-only uploads up to
256 KiB take a buffer of their power-of-two size class (4 KiB at least) from a screen-wide pool of
idle buffers that are still mapped; the fence work that freed the buffer gives it back instead. Only
buffers whose fence signalled are in the pool (none is in a batch the GPU still runs), so it is
written without a wait; uploads that read keep their own buffer (their map waits for the GPU's copy
into it). At most N buffers and 2 MiB of GART, oldest freed first, emptied with the screen. The copies
are the same, so are the pixels.

### Client vertex arrays

A draw from a client vertex array makes Mesa copy the array into GART at every draw (`u_upload_data`;
ps3rsx maps GART cacheable, ordinary RAM). X-Moto's batches were the biggest case in the first round
(5.5% of its CPU), fixed in the game: xmoto 0004 writes its batches straight into a mapped stream
buffer ([client-array-copies](../gamedb/games/xmoto/investigations/2026-10-18-client-array-copies.md)).
SuperTuxKart's are the case left (Irrlicht's legacy driver, [below](#inside-the-graphics-stack)).

## Gameplay profiles on the SSD root (batch ssdroot2 P1b)

Seven games, one run each, `PERF=1 MEM=1` (perf at 499 Hz over the measured window), 2026-10-19
02:01-02:30Z, kernel .472, Mesa 26.2.4-1+ps3.476, ps3rsx 0.443, root on the internal SSD image
(results row 73; run directories `ps3-bench-20261019T0201` to `T0230`). Photos at 60 s and 150 s were
checked: Luanti, BananaBread, SuperTuxKart, ClassiCube, OpenArena and dsda-doom show gameplay;
OpenTTD shows its **main menu** (a title screen redrawing with music, so its profile is the title
screen's). Profiled runs are for profiles, not for fps: perf's own CPU is 0.06-0.23 s, and OpenArena
ran 56 profiled against 60 unprofiled.

The PS3-side reports show Mesa's libgallium and most game binaries as `[unknown]`, so the table
below is by DSO class (`perf-leaf.txt` and the folded stacks). The runs' perf.data were pulled later
and symbolised on the PC ([Inside the graphics stack](#inside-the-graphics-stack)): the class
shares agree with this table within 0.2 points. `perf_*` percentages are of the game process's samples
(all its threads).

| Game | HUD fps (profiled) | game | Mesa | ps3rsx | kernel, rest | soft-irq replay | libc, libm | audio libs | other |
|---|---|---|---|---|---|---|---|---|---|
| [BananaBread](../gamedb/games/bananabread/) | 40 | 10.5 | **62.8** | 4.6 | 6.9 | 2.5 | 4.6 | 6.0 | 2.1 |
| [ClassiCube](../gamedb/games/classicube/) | 45 | 15.6 | **41.2** | 6.8 | 15.1 | 5.0 | 4.8 | 10.0 | 1.5 |
| [SuperTuxKart](../gamedb/games/supertuxkart/) lean | 20 | **36.5** | 35.5 | 2.5 | 3.4 | 1.7 | 15.1 | 3.3 | 2.0 |
| [OpenArena](../gamedb/games/openarena/) gl2min | 44 | 27.6 | 23.2 | 5.6 | 10.1 | 4.1 | 8.9 | 15.0 | 5.5 |
| [dsda-doom](../gamedb/games/dsda-doom/) GL | 57 | 20.5 | 22.1 | 3.3 | 8.6 | 4.1 | 7.4 | **30.9** | 3.1 |
| [Luanti](../gamedb/games/luanti/) | 46 | **47.2** | 14.6 | 2.0 | 7.9 | 5.7 | 13.7 | 1.3 | 7.6 |
| [OpenTTD](../gamedb/games/openttd/) (title screen) | 18 | 21.9 | 1.1 | 0.6 | 11.9 | **37.6** | 9.1 | 16.8 | 1.0 |

"ps3rsx" is its kernel functions (`ps3rsx_shadow_burst`, `validate`, the ioctls, ww_mutex); "kernel,
rest" is every other kernel sample; "soft-irq replay" is `__replay_soft_interrupts` plus
`arch_local_irq_restore`: time spent with interrupts soft-masked, booked where they came back on
([Soft-masked time](#soft-masked-time-and-the-staging-buffer-per-frame) splits it by that place);
"other" is the rest of the libraries, zstd and inflate (OpenArena 4.8, Luanti 1.2) and what no class
takes. Shares are of the game process's samples, so a row sums to 100.

Each game's own split of that profile (which threads, which libraries) is on its technical page
(gamedb's `technical.md`); the graphics stack's part, symbolised, follows.

### Inside the graphics stack

The seven runs symbolised on the PC with the method above (Mesa ps3.476's dbgsym debs, kernel .472's
System.map, ps3rsx 0.443's module, cube2 .466's dbgsym for BananaBread's own frames). Hot path = the
innermost Mesa frame that matches a rule of `categorize.py`; mean of the % of each game's CPU, games
at 1% or more, per game:

| Hot path | mean | games | Sb | CC | STK | OA | dsda | Lu | OTTD | what is under it |
|---|---|---|---|---|---|---|---|---|---|---|
| st/mesa per-draw state | 10.4 | 6 | 13.8 | 18.4 | 14.8 | 7.1 | 11.1 | 7.2 | 0.3 | own code 8.8; diffuse (top: `st_prepare_draw` 0.8, `util_set_vertex_buffers_mask` 0.8, CC 3.6) |
| GL API entry | 8.5 | 7 | 4.7 | 4.8 | 6.7 | 4.0 | 3.8 | 2.5 | **33.2** | OpenTTD: `_mesa_memcpy_texture`, the CPU copy of its PBO upload (0040's area) |
| buffer allocation and free | 5.4 | 5 | 3.4 | 6.2 | 1.0 | **8.0** | 1.4 | 1.0 | **17.0** | kernel 3.7: OpenTTD's 8 MB staging buffers (0040), OpenArena's fences for its submissions |
| submission (`pushbuf_flush`) | 5.2 | 6 | 6.9 | 9.3 | 3.4 | 7.9 | 4.8 | 3.2 | 0.6 | ps3rsx 3.6, kernel 1.3 |
| vertex/index upload | 3.3 | 4 | 1.2 | 0.2 | **17.6** | 2.2 | 1.2 | 0.4 | 0 | SuperTuxKart's client arrays: memcpy 10.1, the index min/max scan 6.5 |
| nv30 state validation | 3.0 | 6 | 3.8 | 6.5 | 3.1 | 2.4 | 3.0 | 2.1 | 0 | own code |
| winsys bookkeeping | 2.9 | 6 | 2.5 | 5.8 | 1.4 | 7.0 | 1.8 | 1.4 | 0.1 | own code |
| vertex format translation | 2.6 | 1 | **18.3** | 0 | 0 | 0.1 | 0.1 | 0 | 0 | `generic_run`, 0033's table loop |
| query result wait | 2.5 | 1 | **17.4** | 0 | 0 | 0 | 0 | 0 | 0 | a busy loop |
| swap and present | 1.2 | 2 | 0.8 | 2.3 | 0.4 | 0.9 | 2.6 | 0.9 | 0.3 | kernel |
| nv30 draw emit | 1.1 | 2 | 0.8 | 4.1 | 1.2 | 0.5 | 0.6 | 0.6 | 0 | own code |
| Gallium HUD (bench only) | 0.9 | 2 | 0.5 | 1.6 | 0.3 | 0.7 | 2.0 | 0.8 | 0.2 | number formatting |
| texture upload, buffer map, fence wait | 1.7 | 4 | 0.5 | 5.9 | 0.1 | 1.9 | 1.5 | 0.4 | 1.4 | |

Top single functions (self time, memcpy counted at its Mesa caller): `generic_run` 2.6 (BananaBread
18.1), `nv30_query_result` 2.5 (BananaBread 17.4), memcpy < `u_upload_data` 1.6 (SuperTuxKart 10.1),
`ps3rsx_shadow_burst` 1.2 (6 games), `nv30_state_validate` 1.0 (6), `vbo_get_minmax_index_mapped` 0.9
(SuperTuxKart 6.5), `nv30_draw_vbo` 0.9 (ClassiCube 3.6), memcpy < `_mesa_memcpy_texture` 0.9
(OpenTTD 6.2), `nv30_vbo_validate` 0.7, `util_set_vertex_buffers_mask` 0.7 (ClassiCube 3.1),
`pushbuf_kref` 0.6, `u_vbuf_draw_vbo` 0.6 (BananaBread 1.6). The kernel frames between `vfs_ioctl`
and ps3rsx's ioctl handlers come out under wrong names (`ps3rsx_probe`, `_drm_lease_revoke`,
`drm_setclientcap`: the module's symbol table); the handlers and everything below them are right.

**Who submits.** Every submission is a pushbuf ioctl through ps3rsx (validator, state shadow), a
fence with its 4 KiB wait buffer and the buffer list referenced again. Inclusive % under
`nouveau_pushbuf_kick` by what asked for it (`stacks.py chain`):

| Game | all | at a draw (0027) | frame end, glFlush | a query's end | deferred frees |
|---|---|---|---|---|---|
| [BananaBread](../gamedb/games/bananabread/) | 9.3 | 2.2 | 1.5 | **5.6** | 0 |
| [ClassiCube](../gamedb/games/classicube/) | 9.3 | 0.1 | 9.2 | 0 | 0 |
| [SuperTuxKart](../gamedb/games/supertuxkart/) | 3.8 | 2.9 | 0.9 | 0 | 0 |
| [OpenArena](../gamedb/games/openarena/) gl2min | 12.4 | 0.1 | 0.8 | 0 | **11.6** |
| [dsda-doom](../gamedb/games/dsda-doom/) | 5.5 | 0 | 5.4 | 0 | 0 |
| [Luanti](../gamedb/games/luanti/) | 3.6 | 0.4 | 3.2 | 0 | 0 |
| [OpenTTD](../gamedb/games/openttd/), title | 1.1 | 0 | 0.6 | 0 | 0 |

"Deferred frees" is `nouveau_fence_work`, which submits once more than 64 frees wait on the current
fence. OpenArena's opengl2 renderer streams its vertices with `glBufferSubData`, and every one of
those queues two (the staging buffer and its suballocation), so it submits every 32 updates: 12.5%
of its CPU with the fence bookkeeping. BananaBread's 5.6% is `nv30_query_end`, which submits at the
end of every occlusion query. ClassiCube's and dsda-doom's frame-end submission is one per frame,
the size of the frame: what is left there is ps3rsx's per-word cost (`shadow_fast` measured nothing).

### The outlier: BananaBread's 62.8%

BananaBread's Mesa share by hot path: vertex format translation 18.3, the query wait 17.4, per-draw
state 13.8, submission 6.9, nv30 validation 3.8, buffer allocation 3.4, the rest 13. The first two are
one engine's habits, each with its investigation: the world's normals and tangents sent as signed
bytes, which u_vbuf converts at every draw even with 0033's table and which make nouveau keep system
memory copies of the VRAM buffers it reads
([byte-normals](../gamedb/engines/cube2/investigations/2026-10-18-byte-normals.md); Mesa 0043 removes
the conversion for every game), and the water's occlusion query waited for mid-frame, each query also
ending with a submission of its own (5.6%, above: Mesa 0042;
[water-query-wait](../gamedb/engines/cube2/investigations/2026-10-18-water-query-wait.md)).

### System, driver and library targets that span several games

Ranked by the mean share of the seven profiles and the number of games where it is at least 4% (the
user prioritises these over game-level fixes). Mesa's inner split is not available (see above).

| # | Target | Mean share | Games at 4%+ | Per game (%) | Lever |
|---|---|---|---|---|---|
| 1 | **Mesa/nv30 in the GL calls** (libgallium: state, draw, translate, upload, the query wait) | 28.6 | 6 of 7 | Sb 62.8, CC 41.2, STK 35.5, OA 23.2, dsda 22.1, Lu 14.6, OTTD 1.1 | split [above](#inside-the-graphics-stack): per-draw state 10.4 (diffuse), submission 5.2, buffers 5.4; the single big ones are BananaBread's conversion and query wait, OpenArena's submissions for deferred frees and SuperTuxKart's client arrays: Mesa 0041-0044 ([third round](#third-round-submissions-snorm8-attributes-index-scans)) |
| 2 | **Audio mixing and synthesis** (OpenAL Soft's mixer, ALSA's float conversion, FluidSynth, libspeq, Vorbis, SDL_mixer) | 11.9 | 5 of 7 | dsda 30.9, OTTD 16.8, OA 15.0, CC 10.0, Sb 6.0, STK 3.3, Lu 1.3 | built, not run: OpenAL Soft's mixer and libvorbis's `ov_read` without load-hit-stores, OpenAL's output 16-bit so the ALSA plug stops converting (1.9% ClassiCube, 0.8 STK, 0.7 OA), fluidsynth's reverb without per-sample divides ([SPU-AUDIO.md](SPU-AUDIO.md#openal-soft-on-the-ppe-conversions-and-output-format)); the music synth is also where the underruns are (dsda 2793, OpenTTD 4172) |
| 3 | **libc and libm** (memcpy, malloc and free, mutexes, math) | 9.1 | 7 of 7 | STK 15.1, Lu 13.7, OTTD 9.1, OA 8.9, dsda 7.4, CC 4.8, Sb 4.6 | libc6-dbg symbols to say which (named so far: `pthread_mutex_lock` 0.5-1.2%, `__memcmpeq` 0.4-0.7, `malloc`, `cfree` 0.4-0.9) |
| 4 | **Soft-masked time** (kernel `__replay_soft_interrupts`, `arch_local_irq_restore`) | 8.7 (3.9 without OpenTTD) | 5 of 7 | OTTD 37.6, Lu 5.7, CC 5.0, OA 4.1, dsda 4.1, Sb 2.5, STK 1.7 | real time, split by where interrupts came back on ([below](#soft-masked-time-and-the-staging-buffer-per-frame)): OpenTTD's is its per-frame staging buffer (Mesa 0040); elsewhere hash faults 0.2-1.6, ALSA's stream lock 0.1-1.3, system call entry and exit 0.3-1.0, wakeups 0.1-1.1 (kernel local/0046 takes one lv1 call off every hash fault) |
| 5 | **ps3rsx submission and validation** (`ps3rsx_shadow_burst`, `_validate`, the push ioctls, ww_mutex) | 3.6 | 3 of 7 | CC 6.8, OA 5.6, Sb 4.6, dsda 3.3, STK 2.5, Lu 2.0 | `shadow_fast` measured no gain, so this is the structural cost of the software channel switch; fewer, larger submits are the game-side lever |
| 6 | **Buffer creation in ps3rsx and page-table flushes** (`ps3rsx_gart_clear` memset under `bo_new`, `ps3_hpte_invalidate` and `unmap_page_range`) | 4.3 (inclusive) | 2 of 7 | OTTD 5.4 + 11.7, CC 1.7 + 4.5, OA 2.0 + 1.0, Sb 1.1 + 0.1 | buffers reused in Mesa, still mapped: 0036 (small uploads, ClassiCube) and 0040 (big ones, OpenTTD); ps3rsx `clear_dcbz` for the zeroing that stays ([below](#soft-masked-time-and-the-staging-buffer-per-frame)) |
| 7 | Decompression at play time (libdeflate, zstd) | 0.9 | 1 of 7 | OA 4.8 (3.9 libdeflate), Lu 1.2 | only OpenArena's reloads; the load-time decoders are in [PROFILING.md](PROFILING.md#start-up-profiles-on-the-ssd-root-batch-ssdroot2) |

Items 1-3 touch most of the suite. Item 2 has a hardware lever and a measured symptom (underruns);
item 1 has the most CPU, and its switches are built; items 4 and 6 are one cause in OpenTTD, built
against and waiting for their A/B.
Game-level items (Luanti's MeshUpdate thread, STK's skinning, OpenTTD's blitter) are in the per-game
files.

## Third round: submissions, SNORM8 attributes, index scans

Picked from [Inside the graphics stack](#inside-the-graphics-stack): the biggest single hot paths a
driver or library change removes whole, in the games where they occur, rather than the per-draw
state (10.4%, spread over dozens of functions, as in the first round). Four switches, off by
default, in Mesa 26.2.4-1+ps3.545~dev (patches 0041-0044 on top of ps3.544's 0040; the debs are in
`build/debs/hotpaths3/mesa`, not deployed, not published):

| Mesa | Switch | Takes away | Share in the profiles | Expected |
|---|---|---|---|---|
| 0041 | `NV30_FENCE_WORK_KICK=N` (64, the default, as upstream; 0: never) | the submissions for deferred frees | OpenArena gl2min 12.5% | OpenArena's CPU about -10% with N=1024 or 0; fps up to +10% where its main thread is the limit |
| 0042 | `NV30_QUERY_KICK=0` or `2` (1, the default: as before) | the submission at the end of every query | BananaBread 5.6% | BananaBread's CPU -5%; fps 0 to +5%, or worse in mode 0 if the water query's wait gets longer |
| 0043 | `NV30_VTX_SNORM8=1` (only with ps3.570~dev or later: ps3.545's wedges the RSX) | u_vbuf's conversion of GL_BYTE normalized attributes | BananaBread 18-19%, QuakeSpasm 4-6% (10-18) | BananaBread +15 to 20% fps (cube2 0011's game-side version of the same gave +17%, row 69), no system memory copies of its VRAM buffers; QuakeSpasm's CPU -4% |
| 0044 | `NV30_MINMAX_VMX=1` | most of the min/max scan of client index arrays | SuperTuxKart 6.5%; OpenArena opengl1 1.3, Yamagi 1.0 (10-18) | SuperTuxKart's CPU about -5%, fps +3 to 5% |

Not picked: the per-draw state; the query wait itself (the GPU is behind there: cube2 0010 is the
lever); ClassiCube's and dsda-doom's one submission a frame (ps3rsx's per-word cost, which
`shadow_fast` didn't move); OpenTTD's PBO copy (0040 is its lever, measured +53% in ssdroot4);
SuperTuxKart's vertex copies (memcpy < `u_upload_data` 10.1%: Irrlicht's legacy driver draws from
client arrays; the lever is VBOs in the game).

### Fewer submissions (Mesa 0041, 0042)

**0041 nouveau: submit for deferred frees only past a set count (`NV30_FENCE_WORK_KICK=N`).**
`nouveau_fence_work()` queues a function to run once a fence signals (the staging memory of an
upload, a destroyed buffer's storage) and submitted the fence's batch as soon as more than 64 waited
on it, to bound the memory they hold. The threshold is now a field of the fence list (64 by default,
so nv50 and nvc0 are as before), from `NV30_FENCE_WORK_KICK` on nv30; 0 never submits for it. The
frees then wait for the next submission there is anyway (0027's at a draw once the GPU caught up and
enough is queued, a flush, a full push buffer) and hold their memory until then: at most a frame's staging copies and
orphaned buffers. Same commands in the same order, in fewer submissions.

**0042 nv30: submit a query's batch when its result is wanted, not at its end (`NV30_QUERY_KICK`).**
1 (the default) submits at the end of every query, as upstream. 0 submits nothing there: a result
check that finds the report not written submits the batch first, if that hasn't happened
(`nouveau_fence_flush` on the fence that was current at the query's end), whether the caller waits
or only polls, so `GL_QUERY_RESULT_AVAILABLE` loops get their answer; nvc0 does the same. 2 is 0
plus a submission at the end while the GPU has finished everything before (0027's test, one read of
the fence notifier), so a query never leaves the GPU idle that the old submission would have fed.
The 124 report slots are recycled by spinning on the oldest: each slot holds the fence of the batch
that writes it, and in modes 0 and 2 that batch is submitted before the spin. A context that goes
away waits for its last fence, so no slot outlives its batch. What changes for a frame that waits
for a result (BananaBread's water): the wait starts at the check instead of the query's end when
nothing submitted the batch in between, which mode 2 makes rare.

No host test for these two: they change when batches go to the GPU, not what is in them; the A/B
shows it.

### SNORM8 attributes in the vertex program (Mesa 0043)

**0043 nv30: signed 8-bit vertex attributes normalized in the vertex program (`NV30_VTX_SNORM8=1`).**
nv30 reports R8_SNORM to R8G8B8A8_SNORM as vertex formats, so u_vbuf leaves them alone. The GPU
fetches them as U8_USCALED (libgcm's UB256: each byte as a float 0..255) and four instructions at
the start of the vertex program give what util_format's unpack gives,
`MAX2(-1.0f, (float)(c * (1.0f/0x7f)))`:

    s = SGE(v, 128); t = MAD(s, -256, v); t = MUL(t, 1/127); t = MAX(t, -1)

The fetch is always four bytes, U8_USCALED x4: the RSX faults on U8_USCALED with 3 components
(see Status below), and only x4 is safe ([RSX.md](RSX.md#errors-and-what-wedges-the-gpu)). MUL and
MAX work on the attribute's components; for R8, R8G8 and R8G8B8 a fifth instruction moves
(0, 0, 0, 1) into the others, so the bytes after the attribute, which the fetch reads too, never
reach the program. That needs four bytes of room from the attribute to the end of its vertex
(`src_offset % stride + 4 <= stride`; BananaBread's normal sits at 12 in a 32-byte vertex). A 1 to 3
byte attribute packed at the very end of its vertex is converted to float by nv30 itself instead,
with the vertices pushed inline (slow, and no game we run has one). The program reads that temp
instead of the input. Which inputs get it is a key of 3 bits per input in the vertex
elements state, kept to the inputs the program declares; a program is translated again when its key
changes (as for clip planes) and the key is part of 0039's cache key. Inline vertices (vbo_fifo)
carry the bytes as they are and constant attributes are emitted as their raw bytes, so every path
hands the program the same values; a program that indexes its inputs goes to the draw module, which
fetches SNORM8 itself. The cost moves to the RSX: four vertex program instructions per such
attribute and vertex, five below four components (BananaBread's world: nine, the normal's five and
the tangent's four).

**Status (2026-10-19): fixed after a wedge, not yet re-run.** The first build (Mesa ps3.545~dev)
fetched each attribute with its own component count, so BananaBread's R8G8B8_SNORM normals went to
the RSX as U8_USCALED x3. Its first run with the switch (batch ssdroot6, `NV30_VTX_SNORM8=1`,
BananaBread base) raised graph exception 0x106 2.2 s into the demo. The GPU was dead until the
console rebooted, and the game spun in `nv30_query_result`. The run before it in the same boot,
without the switch, finished. The dump stops at the start of a draw right after a vertex format
redefinition (decoded in `docs/dumps/exception-0x106-ub256-snorm8.txt`). RPCS3 accepts UB256 with
4 components only, and nouveau's table has U8_USCALED only as R8G8B8A8_USCALED, so the 3-component
fetch is the cause ([RSX.md](RSX.md#errors-and-what-wedges-the-gpu)). 0041 and 0042 were off
(defaults) in that run, and 0040 and 0044 were inactive. The fix is the four-byte fetch above, in
Mesa 26.2.4-1+ps3.570~dev (`build/debs/hotpaths3b/mesa`, not deployed, not published). ps3rsx
0.545~dev (`build/debs/hotpaths3b/ps3rsx-472`, for the booted kernel .472) refuses UB256 with any
size but 4 or 0, so a stream like ps3.545's now fails its push instead of wedging the GPU. The
controlled re-test comes before any A/B (docs/TESTS.md Next, "0043 re-test").

Tests (`tools/run-tests.sh mesa-vtx-snorm8`, from `build/work/mesa`):

- `packages/patched/graphics/mesa/tests/vtx-snorm8-test.c`: the prologue's arithmetic in IEEE single precision against
  util_format's generated unpack functions (taken from `u_format_table.py`'s output) for the four
  formats, every byte in every component, whatever bytes follow the attribute (the four-byte
  fetch), constant attributes too, the key, when a four-byte fetch fits in the vertex, and the
  prologue's length: 67,370 checks, bit for bit;
- `packages/patched/graphics/mesa/tests/vtx-snorm8-vpsim.c`: Mesa's own `nvfx_vertprog.c` translates two programs (plain
  arithmetic; inputs read with swizzles, negated, as abs, twice in one instruction) without the
  prologue and with it for five keys (1 to 4 components, up to three inputs), and an interpreter of
  NV40 vertex program code (the vector slot's MOV MUL ADD MAD DP3 DP4 MIN MAX SLT SGE, refusing
  anything else) runs both on 3000 vertices each, the plain one given the unpacked floats and the
  other the four raw bytes a fetch gives (random past the attribute): the same outputs bit for bit
  (990,030 checks), four instructions more per converted input (five below four components), the
  same input mask. A wrong write mask, a missed input redirect or a missing fill of the absent
  components fails it (checked: without the fifth instruction it fails).
- `packages/walnut/drivers/ps3rsx/test/validate-test.c` (in `tools/run-tests.sh ps3rsx-shadow`): the validator's vertex
  format rule, every type and size in every slot, alone and in bursts: 35,333 checks (the
  validator before the fix fails 1,935 of them).

What the host can't check is the RSX's own rounding of the multiply: if it isn't IEEE's, a component
can be one ulp off, invisible in lighting. On the PS3 the check is the picture: BananaBread's world
lighting and QuakeSpasm's model shading against the switch off.

### AltiVec index scans (Mesa 0044)

**0044 vbo: index array min/max in AltiVec (`NV30_MINMAX_VMX=1`).** A draw from client vertex arrays
needs the range of vertices its indices use, so Mesa scans the index array of every such
glDrawElements; on the PPE the scalar loop is a load, two compares and two branches per index. With
the switch (PowerPC only, and only where the CPU has AltiVec), arrays of 16- and 32-bit indices of
64 or more go eight or four at a time: GCC's vector types, which it compiles to `lvx` and
`vminuh`/`vmaxuh` (`vminuw`/`vmaxuw`) in these functions (`target("altivec")`). The unaligned head
and the tail stay scalar, and so do arrays under 64 (taking the lanes apart at the end goes through
memory, a load-hit-store). Restart indices are masked out; an array of nothing but restart indices
gives ~0u and 0, as the scalar loop does.

`packages/patched/graphics/mesa/tests/minmax-vmx-test.c` (`tools/run-tests.sh mesa-minmax-vmx`) checks it against the scalar
loops (every start alignment, counts 0 to 300, restart off and six restart indices, random, narrow
and top-of-range values: 2.7 million cases) on the host and as a ppc64 `-mcpu=cell` build from the
deb builder's cross compiler under qemu-ppc64.

### Measured: 0041, 0042, 0044

Each switch against the unset default in the same boot, rotated, webcam picture right in every run,
interfered runs left out (kernel .472, SSD root, Mesa ps3.545~dev, 0043 not used). First x2-x3 on the
game whose profile promised the most (batches ssdroot6 and ssdroot7, 2026-10-19), then x5 where that
looked like a gain (ssdroot9) and on other games (ssdroot11, 2026-10-20).

0041 `NV30_FENCE_WORK_KICK` (1024 and 0 against unset), OpenArena gl2min:

<!-- gamedb:batch-table 2026-10-19-ssdroot6-fence-work-kick metrics=fps_steady,low1_play,stall_play_s,cpu -->
| Game | Variant | `fps_steady` (fps) | `low1_play` (fps) | `stall_play_s` (s) | `cpu` (s) |
|---|---|---|---|---|---|
| [OpenArena](../gamedb/games/openarena/) | gl2min@rec10 | 62.3 | 28.1 | 0 | 211.9 |
| [OpenArena](../gamedb/games/openarena/) | gl2min@rec10+fence_work_kick0 | 63.2 | 28.9 | 0 | 216.5 |
| [OpenArena](../gamedb/games/openarena/) | gl2min@rec10+fence_work_kick1024 | 65.3 | 29.5 | 0.1 | 222.2 |

Numbers: gamedb:batch/2026-10-19-ssdroot6-fence-work-kick (batch ssdroot6 Mesa 0041: NV30_FENCE_WORK_KICK=1024 and =0 against unset on OpenArena gl2min x3 (SSD root .472 Mesa ps3.545~dev), 2026-10-19; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

<!-- gamedb:batch-table 2026-10-20-ssdroot9-fence-work-kick metrics=fps_steady,low1_play,stall_play_s,cpu -->
| Game | Variant | `fps_steady` (fps) | `low1_play` (fps) | `stall_play_s` (s) | `cpu` (s) |
|---|---|---|---|---|---|
| [OpenArena](../gamedb/games/openarena/) | gl2min@rec10 | 62 | 27.8 | 0.2 | 191.7 |
| [OpenArena](../gamedb/games/openarena/) | gl2min@rec10+fence_work_kick1024 | 62.9 | 28.2 | 0.3 | 209.7 |

Numbers: gamedb:batch/2026-10-20-ssdroot9-fence-work-kick (batch ssdroot9 Mesa 0041: NV30_FENCE_WORK_KICK=1024 against unset on OpenArena gl2min x5 (SSD root .472 Mesa ps3.545~dev), 2026-10-20; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

0042 `NV30_QUERY_KICK` (2 and 0 against unset), BananaBread:

<!-- gamedb:batch-table 2026-10-19-ssdroot7-query-kick metrics=fps_steady,low1_play,stall_play_s,cpu -->
| Game | Variant | `fps_steady` (fps) | `low1_play` (fps) | `stall_play_s` (s) | `cpu` (s) |
|---|---|---|---|---|---|
| [BananaBread](../gamedb/games/bananabread/) | base@rec10 | 39.4 | 26.6 | 0.2 | 231.4 |
| [BananaBread](../gamedb/games/bananabread/) | base@rec10+query_kick0 | 39.6 | 27.1 | 0.1 | 232.4 |
| [BananaBread](../gamedb/games/bananabread/) | base@rec10+query_kick2 | 39.8 | 27.1 | 0.2 | 231.3 |

Numbers: gamedb:batch/2026-10-19-ssdroot7-query-kick (batch ssdroot7 Mesa 0042: NV30_QUERY_KICK=2 and =0 against unset on Sauerbraten base x3 (SSD root .472 Mesa ps3.545~dev), 2026-10-19; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

0044 `NV30_MINMAX_VMX=1` against unset, SuperTuxKart lean, then BananaBread and OpenArena gl2min:

<!-- gamedb:batch-table 2026-10-19-ssdroot7-minmax-vmx metrics=fps_steady,low1_play,stall_play_s,cpu -->
| Game | Variant | `fps_steady` (fps) | `low1_play` (fps) | `stall_play_s` (s) | `cpu` (s) |
|---|---|---|---|---|---|
| [SuperTuxKart](../gamedb/games/supertuxkart/) | lean@rec10 | 21 | 12 | 0.8 | 305.2 |
| [SuperTuxKart](../gamedb/games/supertuxkart/) | lean@rec10+minmax_vmx1 | 22 | 12.3 | 0.8 | 313.2 |

Numbers: gamedb:batch/2026-10-19-ssdroot7-minmax-vmx (batch ssdroot7 Mesa 0044: NV30_MINMAX_VMX=1 against unset on SuperTuxKart lean x3 (SSD root .472 Mesa ps3.545~dev), 2026-10-19; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

<!-- gamedb:batch-table 2026-10-20-ssdroot9-minmax-vmx metrics=fps_steady,low1_play,stall_play_s,cpu -->
| Game | Variant | `fps_steady` (fps) | `low1_play` (fps) | `stall_play_s` (s) | `cpu` (s) |
|---|---|---|---|---|---|
| [SuperTuxKart](../gamedb/games/supertuxkart/) | lean@rec10 | 20 | 11.6 | 1.2 | 319.6 |
| [SuperTuxKart](../gamedb/games/supertuxkart/) | lean@rec10+minmax_vmx1 | 22 | 12.3 | 0.6 | 307.4 |

Numbers: gamedb:batch/2026-10-20-ssdroot9-minmax-vmx (batch ssdroot9 Mesa 0044: NV30_MINMAX_VMX=1 against unset on SuperTuxKart lean x5 (SSD root .472 Mesa ps3.545~dev), 2026-10-20; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

<!-- gamedb:batch-table 2026-10-20-ssdroot11-minmax-vmx metrics=fps_steady,low1_play,stall_play_s,cpu -->
| Game | Variant | `fps_steady` (fps) | `low1_play` (fps) | `stall_play_s` (s) | `cpu` (s) |
|---|---|---|---|---|---|
| [BananaBread](../gamedb/games/bananabread/) | base@rec10 | 39.6 | 26.9 | 0 | 230.4 |
| [BananaBread](../gamedb/games/bananabread/) | base@rec10+minmax_vmx1 | 39.4 | 25.7 | 0.2 | 232.4 |
| [OpenArena](../gamedb/games/openarena/) | gl2min@rec10 | 62.7 | 28.2 | 0.3 | 184.1 |
| [OpenArena](../gamedb/games/openarena/) | gl2min@rec10+minmax_vmx1 | 61.7 | 26.9 | 0.1 | 185 |

Numbers: gamedb:batch/2026-10-20-ssdroot11-minmax-vmx (batch ssdroot11 Mesa 0044: NV30_MINMAX_VMX=1 against unset on Sauerbraten base and OpenArena gl2min x3 (SSD root .472 Mesa ps3.545~dev), 2026-10-20; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

- **0041: neutral, stays off.** +5% in ssdroot6 at n=2 (two more runs interfered by an SSH session),
  +1.5% at n=5 in ssdroot9, inside the spread. The CPU the profile promised (-10%) is not there: the
  whole run's CPU seconds stayed level or rose with the extra frames.
- **0042: neutral**, nothing to offer on BananaBread; drop it unless another game shows a gain.
- **0044: a gain where the min/max loops run.** SuperTuxKart +5% at n=3, then 22 against 20 fps
  (+10%) at n=5 with gameplay stalls 0.6 against 1.2 s and CPU 307 against 320 s: a candidate
  default. Neutral on BananaBread and OpenArena, whose draws don't scan client index arrays much.

No arm changed the load (6.7 s OpenArena, 7.2 s BananaBread, 28.2 s SuperTuxKart) or showed a picture
difference. The OpenAL mixer wedge hit 3 of the 6 SuperTuxKart runs of ssdroot7 with no sign of lower
fps; three of the 9 OpenArena runs of ssdroot6 ended with ioquake3's DOUBLE SIGNAL FAULT at the SIGTERM
of the window's end, as four earlier runs without 0041 did.

The 0043 run of 2026-10-19 19:24Z (BananaBread, `NV30_VTX_SNORM8=1`, graph exception 0x106) is
analysed above; it has no numbers.

## Soft-masked time and the staging buffer per frame

What the "soft-irq replay" of the profiles above is, what OpenTTD's 37.6% of it is made of, and the
three switches built against it. All three are off by default and wait for their A/B
([TESTS.md](TESTS.md), batch irqoff).

### What a replay sample is

perf on the PS3 samples with `cpu-clock` (perf_events has no driver for the Cell's PMU): an hrtimer,
so a sample is a decrementer interrupt. ppc64 masks interrupts lazily. `local_irq_disable()`,
`spin_lock_irqsave()` and every synchronous exception handler only set a soft mask; a decrementer
that fires then is noted and taken later, in `arch_local_irq_restore` -> `__replay_soft_interrupts`,
when interrupts come back on. perf books the sample there. The replay itself costs little: the
samples are the time of the masked section before it, and the call chain above
`arch_local_irq_restore` says which section that was. So the path was in the profiles all along, one
frame up. `bench/irqoff.py` (perf-post.sh writes its table to `perf-irqoff.txt`) sorts the
samples by that place:

| Game (P1b) | soft-masked | hash faults (user) | HPTE unmap (munmap) | ALSA stream lock | syscall entry/exit | wakeups | context switch | rest |
|---|---|---|---|---|---|---|---|---|
| [OpenTTD](../gamedb/games/openttd/), title | **37.6** | **27.1** | **8.4** | 0.6 | 0.5 | 0.3 | 0.2 | 0.5 |
| [Luanti](../gamedb/games/luanti/) | 5.7 | 1.6 | 0.0 | 0.1 | 0.6 | 1.1 | 0.5 | 1.8 |
| [ClassiCube](../gamedb/games/classicube/) | 5.0 | 1.1 | 0.2 | 0.8 | 1.0 | 0.3 | 0.3 | 1.3 |
| [OpenArena](../gamedb/games/openarena/) gl2min | 4.1 | 1.2 | 0.1 | 0.4 | 0.6 | 0.2 | 0.2 | 1.4 |
| [dsda-doom](../gamedb/games/dsda-doom/) GL | 4.1 | 0.3 | 0.0 | 1.3 | 1.0 | 0.6 | 0.3 | 0.6 |
| [BananaBread](../gamedb/games/bananabread/) | 2.5 | 0.2 | 0.0 | 1.0 | 0.6 | 0.1 | 0.1 | 0.5 |
| [SuperTuxKart](../gamedb/games/supertuxkart/) lean | 1.7 | 0.4 | 0.0 | 0.3 | 0.3 | 0.1 | 0.2 | 0.4 |

% of the game process's samples. "Hash faults (user)" is the return to user mode after an exception
taken in user code: a hash page fault inserts the page's hash table entry through lv1 with interrupts
soft-masked for the whole handler (a Linux page fault turns them on and shows its own functions).
Device interrupts end the same way, but they would spread over all user code; OpenTTD's sit in one
memcpy (26.7 of the 27.1 points under one libc frame, against 0.1 under its blitter, which runs 8.7%
of the time). "HPTE unmap" is `ps3_hpte_invalidate`'s unlock: one lv1 call per page at munmap. The
ALSA lock is `snd_pcm_delay` under the stream's irq lock, the audio libraries' polling (dsda-doom
1.3, BananaBread 1.0: a lead for [SPU-AUDIO.md](SPU-AUDIO.md)'s side).

Not the cause: ps3rsx's fence poll. Its hrtimer runs every 250 us only while someone waits on a
fence (`fence_enable_signaling`), and a waiter's path (`ps3rsx_ioctl_gem_cpu_prep` <
`dma_fence_enable_sw_signaling`) is 0.07% in ClassiCube and less elsewhere. Vblank comes from the
vsync interrupt, not a timer (`vblank_irq=1`). The tick is HZ_PERIODIC at 250 Hz.

### Big uploads: a staging buffer every frame

An upload over 4 MB (Mesa 0007's limit of staging in flight per context) gets a new GART staging buffer
each time: ps3rsx zeroes it at `GEM_NEW`, Mesa's copy into it takes a hash fault on every 4 KiB page
(two lv1 calls each, interrupts soft-masked), `munmap` invalidates every page through lv1 once the
copy's fence signalled, and `nv30_staging_bo_new()` first waits for the GPU, so CPU and GPU run in
lockstep. From OpenTTD's shares (two such uploads a frame, 2540 pages, 18 fps) a hash fault costs about
6.6 us and an invalidation about 2.1 us. OpenTTD's OpenGL driver uploads its whole 1080p screen from two
pixel buffers every frame that way: over 40% of its CPU
([staging-buffer-per-frame](../gamedb/games/openttd/investigations/2026-10-19-staging-buffer-per-frame.md)).

ClassiCube has the same pattern, small: the staging buffers of its animated textures (mmap 2.4,
munmap 2.0, GEM create 1.7, close 0.9; Mesa 0036 `NV30_STAGING_POOL` covers them and measured CPU -4%,
fps unchanged: ClassiCube is GPU-bound). OpenArena's 3.3% and BananaBread's 2.2% of GEM create and
close are small buffers (fences, small uploads), where the cost is the ioctls, drm_mm and an
occasional CMA chunk, not the zeroing (memset 0.1%).

### The switches

| Switch | What it does | Expected |
|---|---|---|
| Mesa 0040 `NV30_STAGING_POOL_MB=M` (M up to 64; 0, the default: off) | Uploads over 256 KiB take an idle staging buffer of their MiB class, or one up to twice it, from a pool of buffers that are still mapped; the fence work that freed the buffer gives it back. At most M MiB and 16 buffers, oldest freed first, every buffer unused for a second freed (checked at each flush), so a load's burst leaves no GART behind. A reused buffer takes no new GART: no in-flight wait. `NV30_STAGING_POOL_STATS=1` prints hits and misses at exit | a game with big uploads every frame: the new buffer, its zeroing, faults, unmapping and in-flight wait go (OpenTTD: about 40% of its samples; measured 34 -> 52 fps, +53%, with the 8bpp blitter: [staging-buffer-per-frame](../gamedb/games/openttd/investigations/2026-10-19-staging-buffer-per-frame.md)). GART: the buffers of the classes in use stay while it runs, as they do now from frame to frame. Nothing for games without big uploads (ClassiCube and OpenArena neutral, ssdroot5) |
| kernel local/0046 `ps3_htab.insert_readback=0` (1, the default: the read-back; 2: both, compared, mismatches counted) | One lv1 call per hash table insertion instead of two: whether the entry is in the secondary group follows from the index lv1 returns (outside the group we gave it: the secondary). Also `/sys/module/ps3_htab/parameters/stats`: inserts, evictions, read-backs, mismatches, invalidations and update-pp's with their microseconds in lv1 | every hash fault, every process: the read-back is up to half of an insertion's lv1 time. OpenTTD without 0040: up to ~13 of its 27 points; elsewhere up to half of 0.2-1.6. Its stats say what the htab costs, in microseconds |
| ps3rsx `clear_dcbz=1` (0, the default: memset) | New GART buffers zeroed with `clear_page` (dcbz: a cache line zeroed without being read first) instead of memset's stores, which on the PPE read every line before writing it | the zeroing about halves: OpenTTD 5.3 -> ~2.5% without 0040, nothing with it; other games under 0.1% |

0040's decisions are in `nv30_staging_policy.h` with no Mesa types, tested on the host
(`packages/patched/graphics/mesa/tests/staging-pool-test.c`, `tools/run-tests.sh` mesa-staging-pool: classes, picks, eviction,
OpenTTD's two uploads a frame for 1000 frames (at most 4 buffers made, at least 1996 of the 2000 uploads reused), a load
burst emptied a second later, 200 random traffic runs against the invariants). `ps3rsx_zero()`
(`src/zero.h`) runs in `test/zero-test.c` on x86 and, with the kernel's dcbz loop, under
qemu-ppc64. local/0046's derived slot is checked on the PS3 itself: `insert_readback=2` for a game
session, `mismatches` must stay 0, before 0 is measured.

Not built: a GPU copy straight from the PBO into the texture (no staging, no CPU copy; but the next
frame's `glMapBuffer(GL_READ_WRITE)` would then wait for that copy, so CPU and GPU would serialise
again); a ps3rsx-side cache of buffers per client (Mesa's pool skips the ioctls, mmap and faults
too); batched invalidations (`lv1_invalidate_htab_entries` is undocumented).

### Profiling soft-masked time

perf's options don't change the picture: `--call-graph=dwarf` would copy 8 KiB of stack per sample
for user frames that the back chain already gives, and the kernel part is right as it is; there are
no hardware events (`cycles:k` doesn't exist without a PMU driver); a higher `-F` books the same
way. Inside a masked section perf can't see, so the sections are counted instead:

- `perf-irqoff.txt` (irqoff.py): the split above, per run, with the faulting user frame for hash
  faults;
- `irq/start.*` and `irq/end.*` (client.sh, at perf's on and off): `/proc/interrupts` and the htab
  stats, as rates over the window in the same file;
- the htab stats by hand: `cat /sys/module/ps3_htab/parameters/stats` (local/0046 kernels).

An irq-off tracer (`CONFIG_IRQSOFF_TRACER`) would time every section, but it needs FTRACE, which
neither kernel flavour has; not done.

## Measured on the PS3

Same-boot benchmark batches. The batches of 2026-10-18 up to the query-wait repeat ran with the 10 ms
kicker (`@wkkick10`), batch 16 on kernel .433 with the idle fix and no kicker, so compare within a batch
only. Steady fps unless noted.

| Change | Result | Batch | Verdict |
|---|---|---|---|
| 0033 `NV30_TRANSLATE_LUT` | [BananaBread](../gamedb/games/bananabread/) 25.9 -> 39.6 (+53%), `low1` 18.2 -> 26.9, x2, on the spectator demo of that night; CPU per run unchanged (231.6 / 231.1 s) | gamedb:batch/2026-10-18-mesa-407-lut | **on by default** since Mesa ps3.408; not re-measured on the first-person demo used since |
| 0033, [QuakeSpasm](../gamedb/games/quakespasm/) | 91.0 / 90.8 against 90.9 / 91.0, CPU 107.0 -> 102.5 s (about -3%) | gamedb:batch/2026-10-18-mesa-407-switches | neutral in fps |
| 0034 `NV30_FENCE_BO_POOL=16` | [ClassiCube](../gamedb/games/classicube/) 48.4 both ways, CPU 83.3 / 82.6 s | gamedb:batch/2026-10-18-mesa-407-switches | neutral, stays off |
| 0035 `NV30_QUERY_WAIT=200` | BananaBread 45.6 against 25.0 in one run; the interleaved repeat 23.0 / 33.4 against 26.0 / 37.9, CPU lower with it (96-197 against 151-232 s) | gamedb:batch/2026-10-18-mesa-416-hot-spots, gamedb:batch/2026-10-18-query-wait-repeat | no fps gain, stays off; the single run was lucky |
| 0036 `NV30_STAGING_POOL=8` | ClassiCube `chunks1` 50.9 both ways, CPU 104.4 -> 100.7 s (-4%) | gamedb:batch/2026-10-18-mesa-416-hot-spots | neutral in fps, stays off |
| ps3rsx `shadow_fast` | ClassiCube 46.11 / 46.11, BananaBread 39.15 / 39.7, X-Moto 158.4 / 159.0 (x2 each, off / on) | gamedb:batch/2026-10-19-ssdroot2-shadow-fast | no gain resolved, stays off |

The game-side changes of the same rounds are measured in their investigations: cube2 0010 `oqwaterlag`
([water-query-wait](../gamedb/engines/cube2/investigations/2026-10-18-water-query-wait.md): promising in one run, the x3 repeat queued),
cube2 0011 `vanormalshort` ([byte-normals](../gamedb/engines/cube2/investigations/2026-10-18-byte-normals.md): +17%), ClassiCube
`CC_GL_CHUNKS=1` ([batching-gpu-bound](../gamedb/games/classicube/investigations/2026-10-18-batching-gpu-bound.md):
+4.7% over three rounds, not the default yet) and xmoto 0004 `XMOTO_GL_STREAM`
([client-array-copies](../gamedb/games/xmoto/investigations/2026-10-18-client-array-copies.md): +1%,
stays off). The single-run BananaBread numbers swing a lot on the first-person demo (the query-wait
repeat's two base runs: 26.0 and 37.9), so small A/Bs there need more rounds.

### Running the A/Bs

Install the Mesa, game and module debs the usual way (`make deploy`), then `make bench-push`. run.sh's
`ENVS` tokens `NV30_X=V` reach the game as `NV30_X=V` (client.sh writes `genv=` into bench.txt;
report.py shows `base@translate_lut0`, `base@query_wait0`, `base@staging_pool16`). Tokens
`PS3RSX_NAME=V` write the value to `/sys/module/ps3rsx/parameters/name` for that run and put the old
one back after it (`sudo -n`; bench.txt gets `kparam=`, report.py shows `base@rsx_shadow_fast1`).
selftest.py covers the tokens. Same boot, interleaved, desktop stopped, webcam photos checked (wrong
pixels from 0033 would show as lighting or shading differences on BananaBread's and QuakeSpasm's
models; look for missing or flickering minimap dots and sprites with X-Moto's stream):

    ENVS="- NV30_TRANSLATE_LUT=0" /var/tmp/bench/run.sh bench bananabread base 3
    ENVS="- NV30_FENCE_BO_POOL=16" /var/tmp/bench/run.sh bench classicube base 3
    ENVS="- NV30_QUERY_WAIT=0 NV30_QUERY_WAIT=50" /var/tmp/bench/run.sh bench bananabread base 3
    /var/tmp/bench/run.sh bench bananabread "base oqlag oqnowait" 3
    ENVS="- NV30_STAGING_POOL=16" /var/tmp/bench/run.sh bench classicube base 3
    /var/tmp/bench/run.sh bench classicube "base chunks1 gloff" 3
    /var/tmp/bench/run.sh bench xmoto "base stream" 3
    ENVS="- PS3RSX_SHADOW_FAST=1" /var/tmp/bench/run.sh bench classicube base 3
    ENVS="- PS3RSX_SHADOW_FAST=1" /var/tmp/bench/run.sh suite base 1
    # profiles of the new paths (for the CPU shares, not for fps)
    PERF=1 MEM=1 ENVS="NV30_QUERY_WAIT=0" /var/tmp/bench/run.sh bench bananabread base 1
    PERF=1 MEM=1 ENVS="NV30_STAGING_POOL=16+PS3RSX_SHADOW_FAST=1" /var/tmp/bench/run.sh bench classicube base 1

Batch irqoff (soft-masked time, the section above). Part A runs on the booted kernel .472 with Mesa
ps3.544 and `ps3rsx-modules-6.12.111+ps3.472` 0.544 installed. Check `perf-irqoff.txt` of the
profiled runs (the hash fault and unmap rows should be near 0 with 0040) and game.log's
`nv30: big staging pool: N hits, M misses` line:

    ENVS="- NV30_STAGING_POOL_MB=24" /var/tmp/bench/run.sh bench openttd "base gl" 3
    ENVS="- PS3RSX_CLEAR_DCBZ=1 NV30_STAGING_POOL=16+PS3RSX_CLEAR_DCBZ=1" /var/tmp/bench/run.sh bench classicube base 3
    ENVS="- PS3RSX_CLEAR_DCBZ=1 NV30_STAGING_POOL=16+NV30_FENCE_BO_POOL=16" /var/tmp/bench/run.sh bench openarena gl2min 3
    PERF=1 MEM=1 ENVS="- NV30_STAGING_POOL_MB=24+NV30_STAGING_POOL_STATS=1" /var/tmp/bench/run.sh bench openttd gl 1
    PERF=1 ENVS="- PS3RSX_CLEAR_DCBZ=1" /var/tmp/bench/run.sh bench openttd gl 1

Part B needs kernel 6.12.111+ps3.544 (local/0046) booted, with `ps3rsx-modules` 0.544 built for it.
That kernel also carries local/0045 (the ps3disk write guard), so the boot follows
[GAMEOS-HDD.md](GAMEOS-HDD.md) "The kernel guard" first. The derived slot is checked before it is
measured: `mismatches` must be 0 after `insert_readback=2` sessions, else stop:

    PERF=1 ENVS="PS3HTAB_INSERT_READBACK=2" /var/tmp/bench/run.sh bench openttd "gl base" 1
    PERF=1 ENVS="PS3HTAB_INSERT_READBACK=2" /var/tmp/bench/run.sh bench luanti base 1
    cat /sys/module/ps3_htab/parameters/stats      # mismatches 0; perf-irqoff.txt has the rates
    ENVS="- PS3HTAB_INSERT_READBACK=0" /var/tmp/bench/run.sh bench openttd gl 3
    ENVS="- PS3HTAB_INSERT_READBACK=0" /var/tmp/bench/run.sh bench luanti base 3
    ENVS="- PS3HTAB_INSERT_READBACK=0" /var/tmp/bench/run.sh bench classicube base 3
    python3 /var/tmp/bench/report.py /var/tmp/ps3-bench/<run dirs>...

Batch hotpaths3 (Mesa 0041-0044, the third round above). Install Mesa 26.2.4-1+ps3.570~dev from
`build/debs/hotpaths3b/mesa` (ps3.545 in `build/debs/hotpaths3/mesa` has the 0043 that wedges the
RSX: never run `NV30_VTX_SNORM8=1` on it) and ps3rsx 0.545~dev for .472 from
`build/debs/hotpaths3b/ps3rsx-472`, after the 0043 re-test in docs/TESTS.md passed (every switch
off by default, so the other rows are unaffected), then same boot, interleaved, x3. Pictures: BananaBread's world lighting and water,
QuakeSpasm's model shading, SuperTuxKart's track and karts all there; MemAvailable (bench.txt's
`min_memavailable_kb`) for 0041, which holds staging memory until the next submission:

    ENVS="- NV30_FENCE_WORK_KICK=1024 NV30_FENCE_WORK_KICK=0" /var/tmp/bench/run.sh bench openarena gl2min 3
    ENVS="- NV30_VTX_SNORM8=1 NV30_QUERY_KICK=2 NV30_VTX_SNORM8=1+NV30_QUERY_KICK=2" /var/tmp/bench/run.sh bench bananabread base 3
    ENVS="- NV30_QUERY_KICK=0" /var/tmp/bench/run.sh bench bananabread base 3
    ENVS="- NV30_MINMAX_VMX=1" /var/tmp/bench/run.sh bench supertuxkart lean 3
    ENVS="- NV30_VTX_SNORM8=1" /var/tmp/bench/run.sh bench quakespasm base 3
    # control: nothing to take away in ClassiCube (no SNORM8, no queries, few deferred frees)
    ENVS="- NV30_FENCE_WORK_KICK=1024+NV30_QUERY_KICK=2+NV30_VTX_SNORM8=1+NV30_MINMAX_VMX=1" /var/tmp/bench/run.sh bench classicube base 2
    # profiles with the switches (for the CPU shares, not for fps)
    PERF=1 MEM=1 ENVS="NV30_VTX_SNORM8=1+NV30_QUERY_KICK=2" /var/tmp/bench/run.sh bench bananabread base 1
    PERF=1 MEM=1 ENVS="NV30_FENCE_WORK_KICK=1024" /var/tmp/bench/run.sh bench openarena gl2min 1
    PERF=1 MEM=1 ENVS="NV30_MINMAX_VMX=1" /var/tmp/bench/run.sh bench supertuxkart lean 1

In the profiles, `generic_run` should be gone from BananaBread (and `nouveau_buffer_transfer_map`
reads of its world buffers), `nv30_query_end > nouveau_pushbuf_kick` near 0,
`nouveau_fence_work > nouveau_fence_kick` gone from OpenArena, and `vbo_get_minmax_index_mapped`
down to a fifth or less in SuperTuxKart (`stacks.py chain` reads all four).

## Fewer draws can cost the GPU more

ClassiCube's batching halved its CPU per frame (8.5 against 17.1 ms) and still ran slower than upstream's
drawing, because the game was GPU-bound and one batching mode (`CC_GL_CHUNKS=2`) sent 75% more
indices to the RSX's vertex units, which transform faces the CPU culling used to skip. Read a CPU
profile's shares as shares of different totals: per frame every class went down. The measurements and
the mode that keeps the savings without the extra indices are
[batching-gpu-bound](../gamedb/games/classicube/investigations/2026-10-18-batching-gpu-bound.md).

## Reproducing the analysis on the PC

    bench/hotpaths/symroot.sh                      # build/tmp/hotpaths-sym: debug files, System.map, modules
    for d in build/bench/<run dirs>; do
        python3 bench/hotpaths/analyze.py $d build/tmp/hotpaths/$(basename $d).pkl
    done
    python3 bench/hotpaths/categorize.py build/tmp/hotpaths/*.pkl   # the tables above
    python3 bench/hotpaths/stacks.py callers <pkl> '^nv30_query_result$' 8   # who calls it
    python3 bench/hotpaths/stacks.py chain '^nv30_query_end$ > ^nouveau_pushbuf_kick$' <pkl>...

Pull the runs with `make bench-pull PERFDATA=1`. `symroot.sh` takes the versions as arguments (Mesa,
kernel release, ps3rsx; the defaults are batch ssdroot2's: 26.2.4-1+ps3.476, 6.12.111+ps3.472,
0.443+k6.12.111+ps3.472) and `EXTRA_DEBS` (the game's deb and -dbgsym of the profiled build, for its
own frames; the SSD-root profiles used cube2 .466's); analyze.py takes the kernel release from
`HOTPATHS_KVER` (default .472) and the symbol root from `HOTPATHS_SYM` (it can live on the overflow
disk: `HOTPATHS_SYM=/mnt/data_linux/Temp/ps3/hotpaths-sym`, about 100 MB). Runs need MEM=1 for the Mesa maps, or a record started before
Mesa loaded (every record since ps3-gpu-tools 0.412). Delete `build/tmp/hotpaths*` afterwards. To get
the PS3's own call-chain handling instead, run its perf under qemu ([PROFILING.md, "Re-processing a
run on the PC"](PROFILING.md#re-processing-a-run-on-the-pc)).

## Not implemented

From the targets above, still open: caching u_vbuf's converted copy of a static buffer instead of
converting it each draw (needs write tracking on the buffer; 0043 makes it unneeded for SNORM8, the
only format the profiles show converted); in ps3rsx, the parts of the submission path
`shadow_fast` leaves untouched (validate, the push copy, the reservation locks) and a restore of only
what other channels changed since this one ran (a per-channel stale bitmap, which needs a wider
proof); immediate mode out of QuakeSpasm's 2D path; ETR's and SuperTuxKart's client vertex arrays
(VBOs in the games). They are in [backlog/graphics.md](backlog/graphics.md).

## History

- 2026-10-18 early morning: the profiling batch, its PS3-side text files found broken, the analysis
  moved to the PC; 0033 and 0034 written (Mesa ps3.407).
- 2026-10-18 06:47 (row 57, commit da8239c): 0033 +53% on BananaBread, made the default in ps3.408.
  07:43 (row 58, commit 350683b): QuakeSpasm LUT and the fence pool neutral.
- 2026-10-18 morning: the second round (0035, 0036, cube2 0010, xmoto 0004, `shadow_fast`; Mesa
  ps3.416~dev, ps3rsx 0.250~dev). 14:21 and 14:38 (rows 64-65, commits 31e356a, 0382e3e): the query
  wait's single-run gain not confirmed.
- 2026-10-18 evening, batch 16 (row 68, commit 296f99d): chunks1 and stream repeated.
- 2026-10-19 (batch ssdroot2, rows 73 and 75): gameplay profiles of seven games on the SSD root
  (unsymbolised Mesa) and the `shadow_fast` A/B (no gain).
- 2026-10-19 afternoon: those seven profiles symbolised on the PC; the third round, Mesa 0041-0044
  (ps3.545~dev), built with host tests, waiting for batch hotpaths3.

Older versions and the dated history: [archive/HOTPATHS.md](archive/HOTPATHS.md).
