# MegaGlest: technical notes

## Status

Debian's MegaGlest 3.13.0 (`megaglest`, `megaglest-data` 400 MB, with wxWidgets and fonts: about 430 MB).
Smoke run on 2026-10-29 (kernel .874, Mesa ps3.924, the VRAM swap on; `ps3-bench-20261028T233747Z-smoke-megaglest-base`): the recipe
([benchmark/recipe.sh](benchmark/recipe.sh)) writes ~/.megaglest/glestuser.ini (fullscreen 1920x1080, set aside and
restored) and starts `megaglest --auto-test=150,,exit`, which plays the game's scenarios with the AI on every
side. A scenario on a forest map runs, drawn right (the units, buildings, trees, rocks, rain, the fog of the
edges, the HUD; xwd), fullscreen. 24 fps steady on the Gallium HUD, 1% low 3.3, 78 frames over 100 ms (27 s of
the 90 s window), the longest 5.2 s; 108 MB peak RSS. At the 150 s limit it does not exit: `terminate called
after throwing ... megaglest_runtime_error: In [thread.cpp::~Mutex Line: 534] about to destroy mutex refCount =
1`, and the harness killed it at 200 s. Runs, needs performance work (a profile first) and the exit fixed.


## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-f-engines.md](../../research/gl3-census-f-engines.md), "Scan: other games that need GL 3+ (one line each)": GL 2.x, shaders optional; Debian `megaglest` 3.13.0-10.1+b1; 400 MB+. Verdict: GL2 already; heavy CPU RTS.
