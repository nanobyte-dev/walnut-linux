# BananaBread: technical notes

BananaBread is Debian's `cube2`, which is Sauerbraten's engine, playing Debian's `cube2-data` (the
BananaBread content, 20 maps) completed by our licensed `sauerbraten-nonfree-data`. It runs on our
[Cube 2 build](../../engines/cube2/technical.md) with all its patches. The user's recorded demo plays at
39.1 steady fps on the SSD root with 0.05 s of gameplay stalls, and at 45.1 with the world normals sent
as shorts (cube2 0011, +17%, not the default yet). It is the suite's heaviest GLSL game: 117 MB of VRAM
and about 20-25 MB of RAM free at the lowest. Its played session (the "Cube2:Demo" launcher,
2026-10-24) had a 20 fps median and 22 s of stutter a minute.

## Packages and data

- `cube2`, `cube2-server`: our engine build (its patches on the [engine's page](../../engines/cube2/technical.md)).
- `cube2-data` 1.3: Debian's BananaBread data.
- `sauerbraten-nonfree-data` (`licensed/sauerbraten-nonfree-data`):
  the models and sounds Debian leaves out (CC BY-NC), the licensed suite only, pulled in by
  `ps3-games-licensed`. It fills in `/usr/share/games/cube2` (912 files). BananaBread itself is in no
  suite: it deploys by name.

## How it performs

| Configuration | Steady fps | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Batch |
|---|---|---|---|---|
| base, SSD root (n=4) | 39.1 | 0.05 s / 25.6 / 103 ms | 25 / 117 MB | gamedb:batch/2026-10-19-ssdroot1-baseline, gamedb:batch/2026-10-19-ssdroot2-shadow-fast |
| base, USB root (x3) | 38.5 | 1.6 s / 24.1 / 459 ms | 20 / 117 MB | gamedb:batch/2026-10-18-ssd1 |
| base, game data on the SSD (USB root) | 39.3 | 1.0 s / 26.4 / 407 ms | 21 / 117 MB | gamedb:batch/2026-10-18-ssd1 |
| `shortnormals` (cube2 0011) | 45.1 | 1.6 s / 30.4 / 512 ms | 20 / 121 MB | gamedb:batch/2026-10-18-batch17 |
| `NV30_FORCE_DXT=opaque`, SSD root (x2, off x2 beside it) | 39.0 (off 40.0) | 0.55 s / 23.9 / 177 ms | 27 / 73-78 MB (off 117) | gamedb:batch/2026-10-19-ssdroot2-force-dxt |

Later SSD-root batches agree: 39.4-39.8 (gamedb:batch/2026-10-19-ssdroot7-query-kick), 39.5
(gamedb:batch/2026-10-19-ssdroot6-libpng), 39.6 (gamedb:batch/2026-10-20-ssdroot11-minmax-vmx).

- **Picture right**, "surprisingly smooth" between stalls (the user). The sound card runs with 60-181
  ALSA underruns a run, yet the webcam microphone heard nothing above the room's hum in any of 20 clips:
  judged by ear, not by the mic.
- **The load** takes 7.1 s on the SSD root against 15.4-19.3 s on the USB root, its disk wait 1.7-1.9 s
  against 4.7-5.6 s, and the gameplay stalls fell from 1.6 s to 0.05 s: the USB stalls were disk
  faults. After the load the main thread is 56% on the CPU (3.9 s of CPU, 1.9 s of disk wait); map
  inflate (zlib) is 1.6 s of it ([PROFILING.md](../../../docs/PROFILING.md#start-up-profiles-on-the-ssd-root-batch-ssdroot2)).
  Mesa's GLSL disk cache saves no load time here (17.4 s cold, 18.4 warm, on USB).
- **Gameplay CPU**: Mesa 62.8% of the samples, the game 10.5%, ps3rsx 4.8%, audio about 5%
  ([HOTPATHS.md](../../../docs/HOTPATHS.md#gameplay-profiles-on-the-ssd-root-batch-ssdroot2-p1b)); most of
  Mesa's share was the world normals' conversion ([the engine's investigation](../../engines/cube2/investigations/2026-10-18-byte-normals.md)).
- **Memory** ([MEMORY.md](../../../docs/MEMORY.md)): 78 MB Rss plus 12.3 MB swapped, malloc 63.4 MB used
  and 17.6 MB free in its arenas (glibc's raised mmap threshold keeps 16-17 MB free in the main heap),
  117 MB of VRAM of which textures are about 87 MB. Without the VRAM swap it was OOM-killed; with it, the
  VRAM swap is full ([the investigation](investigations/2026-10-17-oom-and-the-memory-patches.md)).

The benchmark ([recipe](benchmark/recipe.sh)) plays the user's recorded demo
(`assets/sauerbraten-bench.dmo`, 2026-10-17: an FFA round on cave with two bots, 93 s of game time,
first person with the nonfree data since gamedb:batch/2026-10-18-mesa-416-hot-spots) with `mode -1; map
bench`. A CubeScript (`-x`) counts frames with a sleep re-armed every frame and wall time from
`getmillis 1`, printing `ps3bench: loop <k>: ...` per playback; the window opens 60 s after the first
`mapstart`, `demoend` plays the demo again (the reload is left out: `WIN_ON_RE`, `WIN_OFF_RE`) until the
window holds about 120 s, then `ps3bench: steady ...`. The bots are played back, not simulated, so
every run is the same; still, single runs of the first-person demo swung 23-38 fps in
gamedb:batch/2026-10-18-mesa-416-hot-spots and gamedb:batch/2026-10-18-query-wait-repeat. Variant `spawn`
is the older method (`-l` loads cave, the player stands at its start); the missing HUD gun there is
the data: `-l` starts coop edit with the pistol, and the BananaBread data has no pistol or fist HUD model.
The recipe was called `sauerbraten` before 2026-10-26; old runs keep that name.

## Known problems

- **A crash when GART runs full.** Five runs on 2026-10-18 (07:29-11:42 UTC) ended in `ps3rsx: out of
  GART for a 4096 KiB BO` followed by a segfault writing to address 0 in libc (rc 139); none of the 40
  runs after that stretch crashed. Real Sauerbraten showed the same signature and resolved its call
  site: Mesa's `st_setup_current()` copying into a NULL upload buffer, fixed by Mesa 0047 and 0048
  (Sauerbraten's [technical notes](../sauerbraten/technical.md)). BananaBread's own call sites were not
  resolved; the fix is not yet confirmed on the PS3 for it.
- **Missing content in the logs**, 65-74 of 74 runs: `could not read "data/blendbrush.cfg"`,
  `"data/heightmap.cfg"` (editor files), `could not load texture
  packages/gk/fantasy/skyfantasy/skyfantasy` (73 runs) and `could not load envmap skyboxes/morning`
  (65 runs, 910 lines). If the benchmark map uses them, its sky and reflections use a fallback and the
  picture differs from a PC's; the package is still to be checked for the files. The `could not load
  model` and `failed to load sample` lines of nine early runs (2026-10-17) were missing data files, fixed.
- On 2026-10-17 and 10-18, seven runs logged `speq: s3tc dxt1/dxt5 ... Operation not supported` (the SPE
  compressor refused those job sizes and the CPU path took over) and one `N batches in a row failed on
  the SPEs, the game skins for N ms`; none since.

## What was tried

| Change | Result | Verdict |
|---|---|---|
| Mesa 0033 `NV30_TRANSLATE_LUT` (8-bit vertex attributes through a table) | 25.9 to 39.6 steady (+53%), 1% low 18.3 to 26.9, on the earlier spectator demo (gamedb:batch/2026-10-18-mesa-407-lut) | Mesa's default (`=0` turns it off); not A/B'd again on the first-person demo |
| CPU skinning through libspeq | `ppeskin` best, the SPU path adds hitches ([the investigation](../../engines/cube2/investigations/2026-10-17-spu-skinning.md)) | plain CPU skinning stays the default |
| RGB DXT1 on the SPEs (libspeq 0.394) | `rgbspu` 39.1 against `rgbppe` 39.4: the demo uploads no RGB DXT1, only a few DXT5 (gamedb:batch/2026-10-18-ioquake3-396) | not exercised here |
| `MALLOC=mmap`, `MALLOC=trim` | the rounds disagreed with each other (gamedb:batch/2026-10-18-batch8) | noise, not shipped |
| Mesa 0035 `NV30_QUERY_WAIT=200`, cube2 0010 `oqwaterlag` | [the water query wait](../../engines/cube2/investigations/2026-10-18-water-query-wait.md) | no fps gain from 0035; 0010 promising, to repeat |
| cube2 0011 `vanormalshort` | 38.8 to 45.1 (+17%), stalls equal, VRAM +4 MB (gamedb:batch/2026-10-18-batch17) | default still 0 (pixels), decision open |
| Game prefetch (`GAMEPREFETCH=fetch`) | load 18.1-18.6 s against 15.2-18.8 (gamedb:batch/2026-10-18-batch16) | no gain, default off |
| SSD game data, SSD swap spill | 39.3 / 38.5 against 38.5; load -3 to -6%, inside the spread (gamedb:batch/2026-10-18-ssd1) | no proven gain: the binary, libraries and shader cache stayed on USB |
| The whole root on the SSD | load 16.9 to 7.1 s, gameplay stalls 1.6 to 0.05 s, steady 38.5 to 39.1 (gamedb:batch/2026-10-19-ssdroot1-baseline) | helps loads and stalls, not frame cost |
| Mesa 0038 `NV30_FORCE_DXT=opaque` | 73 textures to DXT1 (88 MB to 11 MB), peak VRAM 119.8 to 73-78 MB, steady 39.9 to 39.0 (inside the spread), the picture slightly softer (gamedb:batch/2026-10-19-ssdroot2-force-dxt) | a VRAM win, lossy, off by default |
| ps3rsx `shadow_fast` | 39.7 against 39.15 (x2), stalls and VRAM equal (gamedb:batch/2026-10-19-ssdroot2-shadow-fast) | no gain at n=2, off |
| Mesa 0042 `NV30_QUERY_KICK` 2 and 0 | 39.8 and 39.6 against 39.4, CPU equal (gamedb:batch/2026-10-19-ssdroot7-query-kick) | neutral |
| Mesa 0044 `NV30_MINMAX_VMX=1` | 39.37 against 39.64 (gamedb:batch/2026-10-20-ssdroot11-minmax-vmx) | neutral here |
| libpng16 +ps3.560, libvorbis +ps3.549, zlib-ng | loads unchanged within the spread (gamedb:batch/2026-10-19-ssdroot6-libpng, gamedb:batch/2026-10-19-ssdroot5-load-libraries) | too small a part of this load |
| Mesa 0043 `NV30_VTX_SNORM8=1` | an RSX graph exception (cause 0x106) one run after a clean run without it (2026-10-19) | stopped; [the engine's investigation](../../engines/cube2/investigations/2026-10-18-byte-normals.md) |

Built, not measured yet: cube2 0007-0009; `texreduce 1` (variant `tex1`, an estimated 45-60 MB of the
117 MB VRAM); Mesa 0037's sysmem statistics with `shortnormals`.

## Investigations

- [2026-10-17 the OOM and the memory patches](investigations/2026-10-17-oom-and-the-memory-patches.md):
  OOM-killed without the VRAM swap, 15 fps with it, then cube2 0001-0005.
- The engine's: [SPU skinning](../../engines/cube2/investigations/2026-10-17-spu-skinning.md),
  [the water query wait](../../engines/cube2/investigations/2026-10-18-water-query-wait.md),
  [the byte normals](../../engines/cube2/investigations/2026-10-18-byte-normals.md).
