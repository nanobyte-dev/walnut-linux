# UFO: Alien Invasion: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-26, [docs/backlog/games.md](../../../docs/backlog/games.md), "Games to smoke next, and Wesnoth's music": on the "try now" list (ppc64 binary in main, est. 400-900 MB, a candidate band); not started.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "ufoai (UFO: Alien Invasion)": no hard no: a Quake2-style GL 1.x fixed-function renderer (also GLES1) with optional GLSL via `r_programs`; FBO optional; nv30 should run it with `r_programs 0`. Debian main `ufoai` 2.5-11, built for ppc64 (3.6 MB); data packages arch all: `ufoai-data` 306 MB, `-maps` 379, `-textures` 329, `-music` 205, `-sound` 18, `-misc` 12.6 (about 1.25 GB). Big-endian: Quake-heritage swap macros (`LittleLong`, 22 files), runtime correctness on BE unverified; BE cost S-M. RAM estimate 400-900 MB for a battle map. Data free (GPL). Effort S. Verdict: TRY NOW, measure RAM first.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "Merged ranking of everything (top is best)": #8: ppc64 binary in main, free data, est. 400-900 MB (textures/maps likely the largest): candidate band, not a quick win.
