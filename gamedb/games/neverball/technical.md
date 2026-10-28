# Neverball: technical notes

Neverball (Debian's 2018 snapshot of 1.6, with Neverputt) runs with the right picture at about 66 fps in
the benchmark's replay, and its played session (2026-10-24) had a 60 fps median ("played flawlessly", the
player). It found three of the driver bugs on its way.

## Packages and patches

`neverball`, `neverputt` and their data (`1.6.0+git20180603-3+ps3.N`, [packages/neverball](packages/neverball/)),
replacing Debian's.

| Patch | What | Switch |
|---|---|---|
| [0001](packages/neverball/patches/0001-Benchmark-mode-NEVERBALL_BENCH-prints-the-frames-and.patch) | bench mode: a replay prints its frames, seconds, fps and longest frame at its end and the game quits | `NEVERBALL_BENCH=1` |
| [0002](packages/neverball/patches/0002-Benchmark-mode-NEVERBALL_BENCH_LOOPS-N-plays-the-rep.patch) | the replay played N times, one line per pass | `NEVERBALL_BENCH_LOOPS=N` |
| [0003](packages/neverball/patches/0003-Fewer-GL-calls-and-draws-a-frame-shared-mesh-buffers.patch) | fewer GL calls and draws: one vertex and element buffer per solid file and one for the five item files, items drawn in a row, the background billboards transformed on the CPU and drawn per material run (76 to 13 draws), no depth toggles around empty passes, one texture unit where one is used | `NEVERBALL_GL_MESH`, `_ITEMS`, `_BACK`, `_STATE`; `NEVERBALL_GL=0` all off |
| [0004](packages/neverball/patches/0004-config-fullscreen-by-default-Walnut-is-a-console-on-.patch) | fullscreen by default (a fresh `neverballrc` started Neverball and Neverputt in an 800x600 window); fullscreen already takes the desktop's mode (1920x1080 on the PS3); the Options menu and a saved `neverballrc` still decide. Built 2026-10-28, pending its PS3 test | the Options menu |

On llvmpipe at GL 2.1 with a fixed 33 ms clock, 0003 took `gui/demo1.nbr` from 2154 to 844 calls and 137
to 74 draws a frame (demo2 2916 to 1285; the level intros greed 6289 to 1915 and mover 8205 to 2463;
Neverputt 565 to 431). Pixels are identical except BACK's rounding at cloud edges (demo1: 163 pixels over 7
frames, 1 level, one pixel 11); all switches off is the old build's exact call stream. VBOs and point
parameters are required with no fallback.

## How it performs

- Bench (gamedb:batch/2026-10-17-luanti-neverball-batching, batching on against `gloff`, same boot):
  steady 66.5 against 62.4, smooth fps 78.7 against 79.1, CPU 65 against 80 s, stalls similar (51 frames
  over 100 ms against 39.5, 1% low 41.3 against 48.1, longest 3.9 and 3.7 s); 58 MB free, 34 MB of VRAM,
  118 underruns. Neverball's stalls are mostly mid-replay, not the restart
  ([STALLS.md, "Gameplay-only columns"](../../../docs/STALLS.md#gameplay-only-columns)).
- Before the steady state: 70-76 fps by its own count with 320-380 ms stalls at the start; Mesa ps3.246
  78.2, ps3.262 75.9 (74, 78: noise).
- Played: 60 fps median, 1% low 37, 1.4 s of stutter a minute.

The benchmark ([recipe](benchmark/recipe.sh)): the shipped replay `gui/demo1.nbr`, real-time paced, about 20
s, played 20 times (passes take 12-16 s on the PS3); `~/.neverball/neverballrc` fullscreen 1920x1080, vsync
off, the fps counter on. A 20 s replay is too short a window to trust; a longer recording of the user's
play is in the game's todo.

## Known problems

None open. No wedge since ps3rsx 0.249 (gamedb:batch/2026-10-17-ps3rsx-249-first-boot).

## Investigations

- [2026-10-16 the driver bugs Neverball found](investigations/2026-10-16-driver-bugs.md): L8A8 swapped,
  1-pixel mipmap levels, graph exception 0x105 (Mesa 0012-0014).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": "Neverball / Neverputt" a strong candidate: light, uses a gamepad well, packaged in Debian (Neverputt: gamedb:neverputt).
