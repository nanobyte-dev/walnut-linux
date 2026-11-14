# Chocolate Doom: technical notes

## Status

Debian's Chocolate Doom 3.1.1 runs: MAP01, right colours, 35 fps (the vanilla cap). The software renderer is scaled by SDL2's renderer (sdl2-compat over our SDL3) to fullscreen 1920x1080; OPL music through SDL_mixer.

Smoke-tested (`SUITE=0`, one launch) on 2026-10-18 with Freedoom 2 (the `freedoom` package), level 1 started at once (`-warp 1 -skill 3`) for a player; installed from Debian ports with `--no-install-recommends`. The recipe ([benchmark/recipe.sh](benchmark/recipe.sh)) writes the config (restored after the run): fullscreen at the desktop size, the scaling buffer capped at 1 M pixels (the default 16 M means an intermediate texture of up to 3200x2000 uploaded every frame), no ENDOOM, no start-up delay, no disk icon. Variants: `demo` (the WAD's DEMO1, exits at its end), `attract` (the title and demo loop, unattended), `nomusic`, `sdlsw`, `gles`. Sound was not judged by ear; not played yet. dsda-doom, the Doom port we package and play, is [its own entry](../dsda-doom/technical.md).

Again on 2026-11-14 (kernel .1529, ps3rsx 0.1534, Mesa ps3.1508, gamedb:batch/2026-11-14-s1114-smoke): MAP01 drawn right, fullscreen 1920x1080, 35 fps steady (the cap), 1% low 28, one 669 ms frame; 3061 ALSA underruns in 120 s (the 10-18 run was not counted this way), the sound not heard.
