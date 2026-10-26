# Woof: technical notes

## Status

Debian's Woof 16.0.0 runs: MAP01, right picture, 34 fps. The software frame goes through SDL3's renderer, 400 lines scaled up; OpenAL sound; MIDI through FluidSynth (on the SPEs through our libfluidsynth3).

Smoke-tested (`SUITE=0`, one launch) on 2026-10-18 with Freedoom 2 (the `freedoom` package), level 1 started at once (`-warp 1 -skill 3`) for a player; installed from Debian ports with `--no-install-recommends`. The recipe ([benchmark/recipe.sh](benchmark/recipe.sh)) writes the config (restored after the run): fullscreen at the desktop size, the scaling buffer capped at 1 M pixels (the default 16 M means an intermediate texture of up to 3200x2000 uploaded every frame), no ENDOOM, no start-up delay, no disk icon. Variants: `demo` (the WAD's DEMO1, exits at its end), `attract` (the title and demo loop, unattended), `nomusic`, `sdlsw`, `gles`; Woof also `uncapped`. Sound was not judged by ear; not played yet. dsda-doom, the Doom port we package and play, is [its own entry](../dsda-doom/technical.md).
