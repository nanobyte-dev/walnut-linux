# CPU hot paths of the graphics stack across the games

Where the CPU of a frame goes in Mesa nv30, its winsys and ps3rsx, ranked across the suite, from
the same-boot profiling batch of 2026-10-18 (PERF=1 MEM=1, ps3.299 Mesa, kernel 6.12.111+ps3.178,
ps3rsx 0.249; `build/tmp/prof-runs.txt` listed the runs). The two biggest targets that are clear
and safe are implemented as Mesa patches 0033 and 0034, both off by default until a PS3 A/B says
otherwise.

## Method

### The PS3-side text files were not usable

Two problems turned up before any ranking could be made:

- **`perf.folded` and `perf-leaf.txt` hold only a few samples** (1 to 621 per run, from the first
  0.1-2 s), so report.py's `perf_*` columns are wrong for this whole batch (OpenArena 011212 shows
  `perf_unk=100` from a single sample). `perf script` stopped early on the PS3 every time, right
  after `cmd_addr2line_configure ... addr2line configuration failed` in `perf-post.err`.
  `perf-report.txt` and `perf-threads.txt` (perf report) are complete.
- **perf misses every mmap made while the record is disabled.** ps3-bench.sh starts perf with
  `-D -1` during the game's load and enables it at the window; libraries the game dlopens in that
  time (libGLX_mesa and libgallium come in at context creation) get no MMAP2 record, so their
  samples are `[unknown]` in the PS3's own report. In OpenArena 011212 that was 25.7% of the
  game's samples; Luanti, Sauerbraten, X-Moto and Armagetron lost libgallium the same way.

Both are fixed on the PS3 side since (docs/PROFILING.md, "The 2026-10-18 batch"): perf script
`--no-inline` (it died of SIGPIPE on the missing addr2line command), the load's mmaps tracked by
ps3-bench.sh (ps3-gpu-tools 0.412), and for records without them perfprof.py fills the frames from
the MEM=1 smaps as below; on this batch's perf.data the fixed perf-post.sh gives this tool's class
shares to the tenth.

### Symbolised again on the PC

`games/bench/hotpaths/` reads `perf.data` directly (big-endian PERFILE2, call chains, MMAP2
records with build ids) and symbolises it on the PC:

- the game's maps are perf's MMAP2 records, with the holes filled from the run's `mem/mid.smaps`
  (MEM=1), which has libgallium where perf has nothing;
- user frames: the debug files of the exact versions (build ids match): Mesa's dbgsym debs from
  `build/debs/mesa`, Debian's `libc6-dbg` 2.43-7, the runtime `.so`s for the ELFv1 function
  descriptors (`.opd`); the kernel from the 6.12.111+ps3.178 image's System.map, ps3rsx and drm
  from their `.ko`, placed at the module addresses perf recorded;
- ppc64's call chain stores the link register as the second entry, which duplicates the caller when
  the leaf has saved it already; perf on the PS3 checks that against the CFI, this tool drops the
  entry when it equals the next one or falls in the leaf's own function;
- checked against the PS3's `perf-report.txt`: the same top symbols with the same self shares
  (ps3rsx_shadow_burst 1.73% here against 1.78% there in OpenArena, __memcpy_cell 2.22/2.22%,
  RB_StageIteratorGeneric, Resample_Cubic_C), apart from the samples the PS3 had as `[unknown]`.

Classes are those of report.py (`perf_game`, `perf_mesa`, ...), by the DSO of the sampled
instruction, with libc's memcpy/memset family split out (`libcmem`). nv30's winsys
(nouveau_pushbuf, nouveau_bo, fences) is built into libgallium, so `drm` (libdrm itself) is
nearly empty: the winsys counts as `mesa`.

Hot paths: each sample gets the category of its **innermost Mesa frame that matches a rule**
(scanning from the leaf outwards), so memcpy, kernel and ps3rsx time under a Mesa function counts
for that function's work: `memcpy < u_upload_data` is vertex upload, the kernel under
`pushbuf_flush` is submission, `nouveau_bo_new` under `nouveau_fence_new` is a fence's buffer. The
rules are in `categorize.py`. Cross-game numbers are the **mean over the games of the % of each
game's CPU** (every game weighs the same), with the number of games where the path takes at least
1% (0.5% for single functions).

### Runs used and left out

Webcam photos (60 s and 150 s into each run) checked for every run:

| Run | Picture | Used |
|---|---|---|
| openarena 011212 (PERF+MEM) | loading at 60 s (warm-up), gameplay at 150 s | yes, the OpenArena profile |
| openarena 001600, 003017 (PERF) | gameplay | cross-check only: no MEM smaps, but perf had Mesa's maps there; class shares within 1% of 011212 |
| openarena 001126, 002131, 002559, 003614 | gameplay | overhead runs, no profile |
| armagetronad 004040 | gameplay at 60 s, **black screen with a cursor at 150 s** | **no**: samples only in the first 20 s of the 120 s window (3973 + 502, then none) |
| openttd 011738 | **main menu with the survey dialog**, not gameplay | **no**, and perf recorded no samples |
| dsda-doom 005026 | gameplay | **no**: perf recorded no samples (`perf_win` empty) |
| classicube, etr, luanti, neverball, quakespasm, sauerbraten, xmoto, yquake2 | gameplay (QuakeSpasm dark but in the level) | yes |

So 9 games: OpenArena, ClassiCube, ETR, Luanti, Neverball, QuakeSpasm, Sauerbraten, X-Moto, Yamagi
Quake II. perf's overhead on fps is within the noise here: OpenArena 50.7, 50.5, 52.7 fps profiled
against 52.7 and 50.2 without (and 52.9, 54.2 with the stall recorder).

## Per game: CPU by class

% of the game process's samples (all its threads) in the window. `cpu` = threads busy on average
(samples / 499 Hz / window); `gfx` = samples with a Mesa frame on the stack or a ps3rsx/drm leaf
(the whole graphics stack, game-side GL calls included); `xorg` = Xorg's samples as % of the game's.

| Game | fps | cpu | game | mesa | drm | kgpu | kernel | libc mem* | libc | other | gfx | xorg |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| OpenArena | 52.7 | 0.73 | 23.2 | 27.3 | 0.0 | 4.7 | 10.8 | 3.7 | 3.2 | 27.0 | 39.7 | 3.8 |
| ClassiCube | 43.4 | 0.38 | 16.1 | 41.3 | 0.1 | 8.0 | 18.0 | 1.1 | 3.7 | 11.6 | 65.0 | 7.5 |
| ETR | 56 | 0.81 | 13.1 | 15.5 | 0.0 | 3.0 | 8.1 | 4.0 | 10.6 | 45.6 | 23.6 | 3.6 |
| Luanti | 45 | 1.00 | 44.7 | 14.8 | 0.0 | 2.5 | 13.6 | 6.2 | 7.8 | 10.4 | 21.0 | 3.0 |
| Neverball | 80.5 | 0.40 | 10.1 | 40.9 | 0.1 | 8.6 | 13.1 | 2.8 | 10.1 | 14.3 | 60.8 | 12.2 |
| QuakeSpasm | 74.4 | 0.51 | 24.3 | 42.9 | 0.1 | 5.5 | 14.0 | 0.8 | 6.3 | 6.2 | 60.5 | 9.5 |
| Sauerbraten | 22.1 | 1.10 | 7.3 | 72.0 | 0.1 | 3.6 | 6.2 | 0.8 | 2.6 | 7.5 | 80.3 | 1.6 |
| X-Moto | 154 | 0.91 | 39.9 | 22.0 | 0.1 | 3.6 | 9.1 | 6.4 | 7.6 | 11.2 | 39.0 | 11.0 |
| Yamagi Quake II | 55.3 | 0.69 | 30.5 | 24.6 | 0.1 | 3.8 | 11.4 | 2.7 | 5.5 | 21.4 | 38.1 | 6.0 |

What `other` is: OpenAL and libdeflate in OpenArena (17% and 5%), freetype + harfbuzz (29%) and
SFML audio in ETR, LuaJIT in Luanti. The game process's `kernel` time is mostly system calls:
DRM ioctls 4.7% on average, read/poll/futex 3.8%, mmap/munmap 1%. Xorg's own CPU (6.5% of the
games' on average) is a third kernel and a sixth its own code; its glamor/ps3rsx part is small
(shadow_restore 0.18%, the channel switch back to the game).

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
| util_format_r8g8b8a8_snorm_unpack_rgba_float | 2.5 | 2 | Sauerbraten 18.2, QuakeSpasm 4.5 |
| util_format_r8g8b8_snorm_unpack_rgba_float | 1.8 | 1 | Sauerbraten 16.4 |
| ps3rsx_shadow_burst (kernel) | 1.3 | 9 | the software context switch's per-word parse of every push |
| nv30_query_result | 1.1 | 1 | Sauerbraten 10.3 |
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

Inclusive, for scale: `glXSwapBuffers` 9.0% on average (Neverball 18, ClassiCube 16), mostly the
flush it does; `pushbuf_flush` 6.4% (9 games); `nv30_draw_vbo` 6.9%; `st_prepare_draw` 5.7%;
`nv30_context_kick_notify` 0.9%, of which `nouveau_fence_new` 0.7.

## Targets

Estimated savings are % of each game's CPU, from the profile shares and what the change removes.
Where the time sits on the game's render thread and the game is CPU-bound, a frame gets that much
shorter; X-Moto (154 fps) and Neverball (80) have headroom already, Sauerbraten (22 fps), Luanti
(45) and ClassiCube (43) don't.

1. **u_vbuf's SNORM8 vertex conversion** (implemented: 0033, `NV30_TRANSLATE_LUT`, measured and on by default, `=0` turns it off; Sauerbraten 25.9 -> 39.6 fps, +53%, same-boot PS3 A/B, 2026-10-18). nv30 has no
   signed 8-bit vertex type (RSX: S1, F, SF, UB, S32K, CMP, UB256), so u_vbuf turns
   GL_BYTE-normalized normals and tangents into 32-bit floats on every draw, for the whole index
   range, static VBOs included. translate_generic does that with the format's unpack function and
   an emit function per vertex and attribute, and every byte passes an integer-to-float
   conversion, which the PPE does through memory (store, load into an FPR, a load-hit-store stall
   of tens of cycles per component). Sauerbraten: 44.9% of its CPU, QuakeSpasm 6.1%. A table per
   channel removes the conversions and the calls: estimate 35-40% of Sauerbraten's CPU, 4-5% of
   QuakeSpasm's. A later step would cache the converted copy of a static buffer instead of
   converting it again each draw (needs write tracking on the buffer).
2. **The submission ioctl in ps3rsx** (not done: kernel module). 5.4% on average in all 9 games:
   `ps3rsx_shadow_burst` parses every pushed word into the channel's state shadow (1.3), the
   restore at channel switches (0.6), `ps3rsx_validate` (0.4), the copy of the push (0.3) and
   the reservation lock (0.3). Candidates: a table-driven method classifier in the burst, skipping
   the restore when the GPU's channel didn't change, fewer and larger submissions (0027's adaptive
   kick decides how many). Estimate 2-3% per game. Needs ps3rsx work and its own A/B.
3. **Per-draw state work in st/mesa and nv30** (diffuse; earlier rounds 0016-0025 took the nv30
   part down). st-validate 13.1 + nv30-validate 3.6 + winsys bookkeeping 2.2 + draw emit 1.2,
   about 20% together, spread over dozens of functions of 0.2-1% each. The biggest single ones:
   `_mesa_update_texture_state` (0.9, 5 games), `nv30_state_validate` self (1.1, 9 games),
   `util_set_vertex_buffers_mask` (ClassiCube 2.9), `pushbuf_kref`/`pushbuf_validate` (0.9, 0017's
   area). No single patch takes more than about 1% here; the lever is fewer draws in the games
   (ClassiCube's chunk merging showed that) and immediate mode out of QuakeSpasm's 2D path (~10%).
4. **A buffer allocation and free per fence** (implemented: 0034, `NV30_FENCE_BO_POOL=N`). Every
   fence makes a 4 KiB GART buffer for the kernel to block on, and nv30 makes a fence per
   submission: `nouveau_fence_new` 0.7% and the frees under `_nouveau_fence_ref` 0.6% on average,
   all 9 games, mostly kernel time (GEM create/close, drm_mm placement). 0027's adaptive kick makes
   more submissions, so more of these. Recycling the buffers of signalled fences: estimate 1-1.5%
   per game (Sauerbraten 2.1, ClassiCube and Neverball 1.6 today).
5. **Sauerbraten's occlusion query wait** (not done). `nv30_query_result` spins on the report
   word with nothing in the loop: 10.3% of Sauerbraten's CPU. The wait itself is the GPU's, so
   the frame doesn't get shorter, but the spinning thread takes issue slots from the PPE's other
   hardware thread. A short spin, then `HMT_low` (`or 1,1,1`) or a yield, would give them back.
   Estimate 5-10% of Sauerbraten's CPU, uncertain fps effect.

Smaller ones, for later: the staging buffer per texture upload (`nv30_miptree_transfer_map`:
ClassiCube 5.4, QuakeSpasm 2.2, Yamagi 1.7; a buffer cache like 0034's would cover it, 0007's
area); client vertex arrays copied per draw (`u_upload_data` memcpy: X-Moto 5.5, ETR 2.3) and the
min/max scans of client index arrays (`vbo_get_minmax_index_mapped`: OpenArena 1.3, Yamagi 1.0; the
index side is 0030's); the Gallium HUD costs 1.3% in bench runs only (number formatting), not in
play. 0028 (index fencing) shows as `nouveau_fence_ref < nv30_state_validate`, 0.3% in 2 games;
0031's fragment program constants are down to 0.2%.

## Implemented

Both in `build/work/mesa`, exported as `mesa/patches/0033-*` and `0034-*`, built into
`build/debs/mesa` (26.2.4-1+ps3.407), not published, not on the PS3.

**0033 translate: 8-bit vertex attributes to floats through a table (`NV30_TRANSLATE_LUT`, on by default since the PS3 measurement: Sauerbraten +53% steady fps, 25.9 -> 39.6; `NV30_TRANSLATE_LUT=0` is the off switch).**
In translate_generic, an element whose input is a plain 8-bit array format with identity swizzle
(R8 to R8G8B8A8; UNORM, SNORM, USCALED, SSCALED) and whose output is R32..._FLOAT with as many
channels gets a table per channel, 256 entries holding the bits that the format's own unpack
function returns for each byte value. The per-vertex loop then does one table load and one store
per channel, no float conversion, no calls. Bit-identical by construction; checked on the PC by
building translate_generic with the generated format tables and running all 16 formats through
`run`, `run_elts`, `run_elts16` and `run_elts8` (index clamping, two buffers, mixed with copied
attributes) with the switch off and on (then `=1`, now the default): identical output, and the table path taken for all 16
formats when on, never when off. llvmpipe can't show it (it fetches SNORM8 itself, no u_vbuf).

**0034 nouveau: recycle the wait buffers of deleted fences (`NV30_FENCE_BO_POOL=N`).** Up to N
(at most 32) buffers of deleted fences go into a pool on the screen's fence list and the next
fences take them instead of allocating. A fence is deleted only once signalled (the fence list
holds a reference from its emit until then) or if it was never emitted, so no batch the GPU still
runs references a recycled buffer; the fence emit only references the buffer in the batch, its
contents are never written or read. The pool has its own lock (nouveau_fence_new runs both with
and without the fence list's lock held) and is drained before the screen's device goes away.
Compiles; only the PS3 can test it.

## A/B on the PS3

Install `build/debs/mesa` (26.2.4-1+ps3.407) the usual way (`make deploy` of the Mesa debs),
`make bench-push` (client.sh passes the switches; selftest.py covers it). run.sh's `ENVS` tokens
`NV30_X=V` reach the game as `NV30_X=V` (client.sh writes `genv=` into bench.txt, report.py shows
`base@translate_lut0`). Same boot, interleaved, desktop stopped, as always:

    # the translate table (on by default; =0 is its off switch, shown as base@translate_lut0):
    # Sauerbraten and QuakeSpasm are the games that use SNORM8 attributes
    ENVS="- NV30_TRANSLATE_LUT=0" /var/tmp/bench/run.sh bench sauerbraten base 3
    ENVS="- NV30_TRANSLATE_LUT=0" /var/tmp/bench/run.sh bench quakespasm base 3

    # the fence buffer pool: every game submits, so a spread of them
    ENVS="- NV30_FENCE_BO_POOL=16" /var/tmp/bench/run.sh bench classicube base 3
    ENVS="- NV30_FENCE_BO_POOL=16" /var/tmp/bench/run.sh bench neverball base 3
    ENVS="- NV30_FENCE_BO_POOL=16" /var/tmp/bench/run.sh suite base 1

    # both, with a profile to see the paths go (profiles are for profiles, not fps)
    PERF=1 MEM=1 ENVS="- NV30_FENCE_BO_POOL=16" /var/tmp/bench/run.sh bench sauerbraten base 1

    python3 /var/tmp/bench/report.py /var/tmp/ps3-bench/<run dirs>...

Look at the webcam photos of every run (wrong pixels from 0033 would show as lighting or shading
differences on Sauerbraten's and QuakeSpasm's models; there shouldn't be any). For the profiles,
pull with `make bench-pull PERFDATA=1` and run the PC tools below: the PS3's perf.folded is still
the broken one.

## Reproducing the analysis on the PC

    games/bench/hotpaths/symroot.sh                      # build/tmp/hotpaths-sym: debug files, System.map, modules
    for d in build/bench/<run dirs>; do
        python3 games/bench/hotpaths/analyze.py $d build/tmp/hotpaths/$(basename $d).pkl
    done
    python3 games/bench/hotpaths/categorize.py build/tmp/hotpaths/*.pkl   # the tables above

`symroot.sh` takes the versions as arguments (Mesa, kernel release, ps3rsx; the defaults are this
batch's); runs need MEM=1 for the Mesa maps (or a record started before Mesa loaded). Delete
`build/tmp/hotpaths*` afterwards.
