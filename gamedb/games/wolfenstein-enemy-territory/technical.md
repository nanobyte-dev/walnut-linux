# Wolfenstein: Enemy Territory (ET: Legacy): technical notes

Also known as: ET: Legacy (the engine).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-26, [docs/backlog/games.md](../../../docs/backlog/games.md), "Games to smoke next, and Wesnoth's music": on the "try now" list (measure fps first); not started.
- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Action and shooters": Wolfenstein: ET (no console version): ET: Legacy, playable, GL 1.x/2.x, ET is free; effort M; the pick (731 stars, active).
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "iortcw (Return to Castle Wolfenstein) and ET:Legacy": no hard no: CMake `FEATURE_RENDERER1` (OpenGL 1/2) ON by default, `FEATURE_RENDERER2` (GL 3) OFF; GLES and Vulkan renderers exist (OFF). Not in Debian: own build (CMake, bundled GLEW and libs, renderer1 only). BE cost S-M (ioq3 `LittleLong` discipline). RAM estimate 150-350 MB. Data: Enemy Territory is freeware (pak0-2 from the ET 2.60b download, about 270 MB), and ET: Legacy adds its own "legacy" mod: a free, legal multiplayer/bots data set. Effort S-M. Verdict: TRY NOW if the freeware data is acceptable; 1080p multiplayer maps on one core need a measured frame rate.
- 2026-10-19, [gl3-census-d-fps.md](../../research/gl3-census-d-fps.md), "OpenMoHAA (Debian contrib) and ET: Legacy": no Debian package; `renderer_opengl1` (fixed function) would run on nv30 as our ioquake3 opengl1 does; engine GPLv3, game data from the free Wolfenstein: ET release; BE UNVERIFIED (ioq3 lineage). Scores: popularity 4, novelty 4, playability 3, effort S-M. Not opened in source.
