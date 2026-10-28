# FreeOrion: technical notes

## Status

Debian's FreeOrion 0.5.1.2 (`freeorion`, `freeorion-data`, about 205 MB). Smoke run on 2026-10-29 (kernel .874,
Mesa ps3.924, the VRAM swap on; `ps3-bench-20261028T235212Z-smoke-freeorion-base`): `freeorion -f -q --auto-advance-n-turns 3`
([benchmark/recipe.sh](benchmark/recipe.sh)) starts a quickstart game with six AI empires, fullscreen 1920x1080,
the client reporting OpenGL 2.1. The interface is drawn right (the turn bar, the situation report, the empires,
the planet panel), but the galaxy map has a white, rotated square about 400 px wide over the home system, on
the TV too (xwd at 120 s, webcam at 150 s): a sprite drawn without its texture. Turn 1 still "Waiting for Other
Players" after about 90 s (six Python AI processes on the PPE). No OOM. Handed to the fixes.


## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Alternatives and popularity": FreeOrion (inspired by Master of Orion, a different game): active, C++/Python, 1.1k stars.
- 2026-10-19, [gl3-census-f-engines.md](../../research/gl3-census-f-engines.md), "Scan: other games that need GL 3+ (one line each)": GG engine, GL 2.1-class (UNVERIFIED version); Debian `freeorion` 0.5.1.2-1+b1; 500 MB+. Verdict: probably GL2 already; heavy, Python AI.
