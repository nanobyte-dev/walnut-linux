# Frozen Bubble: technical notes

## Status

Debian's Frozen Bubble 2.212 (`frozen-bubble`, `-data`, 21 MB) runs: level 1, right picture, 45 fps
(2026-10-18). `--solo` starts a one-player game; the game is 640x480 on SDL 1.2 (sdl12-compat over our SDL3),
so fullscreen is that picture scaled. Perl starts it. Not played yet.

Smoke-tested: one launch to see whether it starts and plays (`SUITE=0`, no benchmark numbers kept; the smoke runs are described in [SUITE.md](../../../docs/games/SUITE.md#smoke-runs)). Variants `menu`, `nosound`, `slow`, `window` ([benchmark/recipe.sh](benchmark/recipe.sh)).
