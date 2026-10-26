# X-COM: UFO Defense (OpenXcom): technical notes

Also known as: UFO: Enemy Unknown; OpenXcom (the engine); OpenXcom Extended (OXCE, its superset fork, merged here); X-COM: Terror from the Deep (also played by OpenXcom).

## Engine options

- OpenXcom (the pick: the vanilla engine, 2.2k stars)
- OpenXcom Extended (OXCE, MeridianOXC/OpenXcom: a superset fork, most mods; drop-in later)

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-26, [docs/backlog/games.md](../../../docs/backlog/games.md), "Games to smoke next, and Wesnoth's music": "with the user's own data: ... OpenXcom/OXCE (vanilla first)"; not started.
- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Strategy": X-COM UFO Defense / TFTD (PS1 versions existed): OpenXcom, playable, SDL, own data, effort M.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "openxcom and OpenXcom Extended (OXCE)": no hard no: SDL 1.2 + SDL_gfx, 8-bit software blitter; OpenGL optional (`-D__NO_OPENGL` supported). Not in Debian: own build (CMake, bundled yaml-cpp, sdl12-compat, SDL_gfx). Big-endian: 7 files use `SDL_Swap*`/`BIG_ENDIAN`; a forum user ran 1.0 on a PowerPC G4 with a local FLI/FLC fix; BE cost M. RAM estimate 120-250 MB vanilla, OXCE with big mods 400 MB-1 GB. Data: the original UFO / X-COM files (Steam/GOG), no free set. Effort M. Verdict: WORTH A BUILD only with the user's own data; vanilla first, skip OXCE until vanilla works.
