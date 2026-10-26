# VVVVVV: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-26, [docs/backlog/games.md](../../../docs/backlog/games.md), "Games to smoke next, and Wesnoth's music": on the "try now" list (private build only, licence); not started.
- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Platformers and 2D action": VVVVVV (2010): the official source, complete, SDL2, a Wii port (?), free Make and Play data, effort S; 8k stars.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "VVVVVV (open-source edition)": no hard no: SDL renderer, software-capable; the current tree uses the SDL3 API (older tags SDL2). Not in Debian: own build (CMake, bundled FAudio, physfs, lodepng, tinyxml2). Big-endian: `GraphicsResources.cpp` picks `SDL_PIXELFORMAT_RGBA8888` on BE; BE cost S. RAM estimate 50-100 MB. Data: `data.zip` from the game, or the free Make and Play Edition. Licence: non-commercial source-available (not DFSG): redistribute no binary; private builds allowed. Effort S. Verdict: TRY NOW, not packageable in a public repository.
