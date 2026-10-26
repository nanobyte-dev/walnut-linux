# QuakeSpasm and Yamagi Quake II

Both Quake engines meet the 60 fps target with almost no stalling. QuakeSpasm plays the Quake
shareware timedemo at 91.0 steady fps (row 58); Yamagi Quake II plays the Quake II demo at 68.7
(row 57) since its random number table shrank from 32 MB to 64 KB. Nothing is open for either;
upstream drafts of our Yamagi fix are in `docs/upstream/` (PLAN 5.8).

## The data

Both games use non-free data, built once on the PC with game-data-packager (`make quake-data`, the
Debian games builder with `lhasa`, into `build/debs/quake-data`): `quake-shareware`
(`/usr/share/games/quake/id1/pak0.pak`, Quake 1.06 shareware, 18 MB, demo1-3) and `quake2-demo-data`
(`/usr/share/games/quake2-demo/baseq2/`). They are published once to our licensed suite
(`make publish`) and reused, since game-data-packager's output isn't reproducible, and
`ps3-games-licensed` pulls them in. A registered Quake needs the user's own `id1/pak0.pak` and
`pak1.pak`. Don't install the `quake` launcher package: it pulls GTK 4.

## QuakeSpasm

Debian's `quakespasm` 0.96.3, unpatched. 0.96.3 has a big-endian bug: packed-pixel lightmaps give
wrong colours and dlight wraparound (quakespasm issue #114), so the recipe passes `-nopackedpixels`
(upstream master disables the path on big-endian; nv30 has no 10-bit format anyway).

Recipe `bench/recipes/quakespasm.sh`: `quakespasm -nopackedpixels -width 1920 -height 1080
-fullscreen` with a `quake.rc` of ours in `~/.quakespasm/id1`. `+timedemo` on the command line
doesn't work: the shareware's own quake.rc runs `startdemos demo1 demo2 demo3` after the command
line and the attract loop replaces the timedemo, and the shareware refuses `-game` directories. Our
quake.rc loops the timedemo by frame count (`bench` starts it and waits 1100 host frames; demo1's
timedemo is 969 frames), since QuakeSpasm has no hook at a demo's end. About 5 warm-up passes of
13 s, then about 9 measured. Parsing: `(\d+) frames\s+([\d.]+) seconds\s+([\d.]+) fps`; a line
starting with `-1 frames` means a timedemo started before the last one ended. Base keeps GLSL on
(alias models) with gamma at 1.0, so the full-screen gamma pass doesn't run. Variants: `noglsl` (GL
1.x only), `glslgamma` (the gamma pass on).

Results (row 58, kicker, same boot): steady 91.0, 1% low 48.6, 2.5 s of gameplay stalls (10 frames
over 100 ms), longest 393 ms, 44 MB free at the lowest, 49 MB of VRAM, 5 underruns. Mesa's
`NV30_TRANSLATE_LUT=1` changed nothing here (90.9/91.0 against 91.0/90.8) but saved about 3% of CPU.
Memory: 58 MB Rss, its 256 MB heap mapping 24.8 MB touched; nothing to cut at that size
([MEMORY.md](../MEMORY.md)).

History: first runs 2026-10-17: 59.2 fps without GLSL, 64.8 with, 70.3 with the gamma pass, colours
right. Mesa ps3.262 70.5 to 78.6 (73 and 84 in two runs, +11%).

## Yamagi Quake II

Our build of Debian's `yquake2` 8.70 (`gamedb/games/yquake2/packages/yquake2/`, `COMPONENT=contrib`): debian-ports builds nothing
arch-specific in contrib, so Debian has no ppc64 binary. Packages `yamagi-quake2`,
`yamagi-quake2-core` (`/usr/lib/yamagi-quake2/quake2`, renderers `ref_gl1` and `ref_gl3`). The gl1
renderer (GL 1.4 fixed function) is the one that runs; gl3 and gles3 need GL 3.

Patches:
- **0001** swaps the 16-bit WAV samples to host byte order at load. Uploaded little-endian, they
  played as white noise through OpenAL (+ps3.240; the user confirmed the sound).
- **0002** `YQ2_KISS_QSIZE`: the KISS PRNG's lag table (`static uint64_t QARY[0x200000]` in
  `src/common/shared/rand.c`, 16 MiB, every entry written at seed, linked into both `quake2` and
  `game.so`) is allocated at first use with this many entries, a power of two from 64 to 2^21. The
  default is 4096 (32 KiB per module) since the PS3 measurement; `YQ2_KISS_QSIZE=2097152` (variant
  `kissbig`) gives the old size and the same sequence. In the package the .bss is 7.7 MB in `quake2`
  (was 23.7) and 81 KiB in `game.so` (was 16 MB). Client-side random effects differ from the old
  table; the demo itself doesn't.

Recipe `bench/recipes/yquake2.sh`: the engine run directly (Debian's `quake2` wrapper is
contrib, not ours) on the demo pak's only demo, `+timedemo 1 +demomap q2demo1.dm2`, vsync off,
config `~/.local/share/YamagiQ2/baseq2/config.cfg` (`~/.yq2` before the XDG build). The timedemo repeats in one session: autoexec.cfg's `bench` alias
sets `nextdemo` again, disconnects (which prints the pass's line; a chained demomap would reconnect
and clear the counters) and starts the demo cold. Each pass also prints "ERROR: Disconnected from
server". Parsing `%i frames, %3.1f seconds: %3.1f fps`. Variants: `sdlsound` (SDL audio instead of
OpenAL, `s_openal 0`), `kissbig`, `kiss4k`, the OpenAL Soft variants.

Results (row 57, same boot, x2): `kiss4k` against the big table: RSS 125 to 56 MB, swap used 80 to
9 MB, gameplay stalls 22.6 to 0.05 s, longest frame 4.5 s to 91 ms, steady 65.7 to 68.7, 1% low 20.1
to 31.1, 46 MB free at the lowest (12 before), 48 MB of VRAM, no underruns.

History: first runs 2026-10-17: 56.1 fps with OpenAL, 58.4 with SDL audio; Mesa ps3.246 51.4
(56 the day before), ps3.262 63.9 (+14-24%, row 38).

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md),
[archive/MEMORY.md](../archive/MEMORY.md).
