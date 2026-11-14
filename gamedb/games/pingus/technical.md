# Pingus: technical notes

## Status

Debian's Pingus 0.7.6 (`pingus`, `-data`, 11 MB) runs: the first tutorial level's start screen (it waits for
the Play click), 39 fps (2026-10-18). `-r opengl` (textured quads in fixed function), `-f -R 1920x1080`; the
mouse does everything, but the penguins walk in and animate without input. Not played yet. Again on 2026-11-14 (kernel .1529, ps3rsx 0.1534, Mesa ps3.1508, gamedb:batch/2026-11-14-s1114-smoke): the same start screen
right at 30 and 90 s, fullscreen 1920x1080, 40 fps steady, longest frame 265 ms.

Smoke-tested: one launch to see whether it starts and plays (`SUITE=0`, no benchmark numbers kept; the smoke runs are described in [SUITE.md](../../../docs/games/SUITE.md#smoke-runs)). Variants `menu`, `sdl`, `delta`, `swcursor`, `nosound` ([benchmark/recipe.sh](benchmark/recipe.sh)).
