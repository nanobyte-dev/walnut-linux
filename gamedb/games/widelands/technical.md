# Widelands: technical notes

## Status

Debian's Widelands 1.3.1. Its first run (batch ssdroot3) exited after 2 s: `--fullscreen=true` takes no value
(fixed in e40e282); the next (gamedb:batch/2026-10-19-ssdroot4-smoke) refused `--fullscreen` together with
`--xres` (fixed in 57339ca); the third (gamedb:batch/2026-10-19-ssdroot5-smoke) was nudged by the user and is
out of the cells.

Clean re-runs on 2026-10-28 (kernel .874, Mesa ps3.924, the VRAM swap on, `widelands` 1:1.3.1-4 from
debian-ports, +600 MB): the tutorial scenario loads in about 40 s (the longest frame of the window, 13.7 s, is
its end) and plays fullscreen 1920x1080, the map, water, trees, rocks, the toolbar and the tutorial's windows
drawn right (xwd; webcam). Its first message pauses the game, and Widelands shows a paused game as a dotted
map, one pixel in four lit: the PC reference (llvmpipe, `make bench-ref GAME=widelands`) is the same, so
it is no bug; the recipe ([benchmark/recipe.sh](benchmark/recipe.sh)) now clicks OK (and presses Return) 60 s
after the window. In play: 23 fps steady (27 while paused), 1% low 7, 29 frames over 100 ms in the 90 s window
(30 s, the load's end included), 110 MB peak RSS, sound running with 16-24 audible dropouts. Runs, needs
performance work: a profile of the play first (runs `ps3-bench-20261028T215509Z`, the paused one
`T211855Z`).

## Threads in the stall frames

Checked on 2026-10-29 after gamedb:luanti's stalls turned out to be its render thread waiting for the CPU its own worker threads had (luanti 0010 puts them at nice 10). From the stall record (stalls.py's `stall_main_pct` and `stall_rest_pct`: the CPU of the main thread and of the game's other threads in the stall frames, in % of their time, 100 being one PPE thread; Luanti before its fix: 22 and 83) and the profile (`bench/analysis/stallperf.py`). Run `ps3-bench-20261029T004009Z-smoke-widelands-base` (STALLS=1 PERF=1): main thread 74%, other threads 11%, kswapd0 10%. Widelands 1.3 runs its logic and its drawing in the main thread; the only busy other thread is SDL's audio (12% of the samples, Vorbis decoding), which stays as it is. The stalls are the main thread's own work and some swap: renicing has nothing to act on.
