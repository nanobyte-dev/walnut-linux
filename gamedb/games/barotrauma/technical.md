# Barotrauma: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "Ranked table": MonoGame GL 3.x and proprietary. Verdict: drop.
- 2026-10-19, [gl3-census-f-engines.md](../../research/gl3-census-f-engines.md), "Barotrauma (MonoGame fork, .NET 8, repo pushed 2026-09)": not a GL problem (MonoGame DesktopGL needs OpenGL 2.0 plus ARB_framebuffer_object or GL 3.0, UNVERIFIED for its fork) but a runtime and licence one: `LinuxClient.csproj` targets net8.0 with x64-only native webm and Steamworks libraries; no .NET 8 for ppc64 BE; the source is under an EULA and the Content folder isn't in the repo (the paid Steam game is needed). Scores: popularity 4, novelty 5, playability 1, effort XL. Not worth it.
