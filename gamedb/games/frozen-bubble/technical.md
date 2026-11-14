# Frozen Bubble: technical notes

## Status

Debian's Frozen Bubble 2.212 (`frozen-bubble`, `-data`, 21 MB) runs: level 1, right picture, 45 fps
(2026-10-18). `--solo` starts a one-player game; the game is 640x480 on SDL 1.2 (sdl12-compat over our SDL3),
so fullscreen is that picture scaled. Perl starts it. Not played yet.

Smoke-tested: one launch to see whether it starts and plays (`SUITE=0`, no benchmark numbers kept; the smoke runs are described in [SUITE.md](../../../docs/games/SUITE.md#smoke-runs)). Variants `menu`, `nosound`, `slow`, `window` ([benchmark/recipe.sh](benchmark/recipe.sh)).

Again on 2026-11-14 (kernel .1529, ps3rsx 0.1534, Mesa ps3.1508, gamedb:batch/2026-11-14-s1114-smoke): level 1 drawn right at 90 s, fullscreen 1920x1080, 36 fps steady; the 30 s shot is still the black loading screen, and the load took four frames of 10.6-12.6 s (54 s of stalls in the 90 s window, so the window is mostly load). 18 MB free at the lowest.
