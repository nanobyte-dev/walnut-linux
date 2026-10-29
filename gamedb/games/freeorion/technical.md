# FreeOrion: technical notes

## Status

Debian's FreeOrion 0.5.1.2 (`freeorion`, `freeorion-data`, about 205 MB). Smoke run on 2026-10-29 (kernel .874,
Mesa ps3.924, the VRAM swap on; `ps3-bench-20261028T235212Z-smoke-freeorion-base`): `freeorion -f -q --auto-advance-n-turns 3`
([benchmark/recipe.sh](benchmark/recipe.sh)) starts a quickstart game with six AI empires, fullscreen 1920x1080,
the client reporting OpenGL 2.1. The interface is drawn right (the turn bar, the situation report, the empires,
the planet panel), but the galaxy map has a white, rotated square about 400 px wide over the home system, on
the TV too (xwd at 120 s, webcam at 150 s): a sprite drawn without its texture. Turn 1 still "Waiting for Other
Players" after about 90 s (six Python AI processes on the PPE; the universe takes 100-150 s to generate). No OOM.


## The white square: field textures too big for the VRAM

The square is a field (nebula, ion storm, molecular cloud) drawn rotating over the system: the PC reference
(Debian's amd64 build on llvmpipe at GL 2.1, the same command) draws a grey cloud there. The field textures
(`default/data/art/fields/*.png`) are 2048x2048 RGBA, about 22 MB each with their mipmaps; on the PS3 a
copy shrunk to 1024 draws (`ps3-bench-20261029T002334Z`), and so do the package's own 2048 ones stored as
DXT5 at upload by our Mesa (`NV30_FORCE_DXT=all`, `ps3-bench-20261029T044231Z-smoke-freeorion-dxt`): they
didn't fit the video memory, and a texture that can't be placed samples white. Our menu entry
(`desktop/launchers/freeorion-walnut.desktop`, Debian's diverted) starts `freeorion -f` with that
variable: fullscreen, and the fields drawn. State: the entry built (ps3-games-integration), pending its PS3
test from the menu.

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Alternatives and popularity": FreeOrion (inspired by Master of Orion, a different game): active, C++/Python, 1.1k stars.
- 2026-10-19, [gl3-census-f-engines.md](../../research/gl3-census-f-engines.md), "Scan: other games that need GL 3+ (one line each)": GG engine, GL 2.1-class (UNVERIFIED version); Debian `freeorion` 0.5.1.2-1+b1; 500 MB+. Verdict: probably GL2 already; heavy, Python AI.
