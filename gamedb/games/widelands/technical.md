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
