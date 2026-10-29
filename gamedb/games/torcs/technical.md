# TORCS: technical notes

## Status

Debian's TORCS 1.3.7 (`torcs`, `torcs-data`). Smoke run on 2026-10-29 (kernel .874, Mesa ps3.924, the VRAM swap
on; `ps3-bench-20261028T230810Z-smoke-torcs-base`): the window is named after the binary (`/usr/lib/powerpc64-linux-gnu/torcs/torcs-bin`), and Return
three times by XTEST (Race, Quick Race, New Race) starts Quick Race on Forza with four robots. The loading
screen and the race are drawn right (the track, stands, cars, gauges, the HUD; xwd), fullscreen 1920x1080
(`geom fs=1`), with the recipe's texture compression and a 1024 texture limit. But it crawls: about 7 fps on
the Gallium HUD (the game's own counter 6.6), 1% low 0.3, frames up to 6.1 s, 84 MB peak RSS, sound running.
Runs, far too slow to play: a profile first (plib's immediate-mode drawing, five cars' simulation on the PPE).
The recipe: [benchmark/recipe.sh](benchmark/recipe.sh) (variants `notc`, `window`, `single`, `menu`).

## Threads in the stall frames

Checked on 2026-10-29 after gamedb:luanti's stalls turned out to be its render thread waiting for the CPU its own worker threads had (luanti 0010 puts them at nice 10). From the stall record (stalls.py's `stall_main_pct` and `stall_rest_pct`: the CPU of the main thread and of the game's other threads in the stall frames, in % of their time, 100 being one PPE thread; Luanti before its fix: 22 and 83) and the profile (`bench/analysis/stallperf.py`). Run `ps3-bench-20261028T232756Z-smoke-torcs-base` (STALLS=1 PERF=1): main thread 84%, other threads 9% (OpenAL's mixer, SCHED_RR), kswapd0 2%. One thread does the drawing (Mesa 41% of the samples) and the robots and physics (simuv2, lliaw, tita, berniw, olethros): the stalls are its own work, nothing waits on a worker, so renicing has nothing to act on. The fix is in that thread's frame (todo.toml).
