# OpenRA: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Strategy / Alternatives and popularity": C&C Red Alert, Dune 2000, Tiberian Sun through OpenRA (17.6k stars, the most popular project of the survey): playable, GL 3.2 (an old GL 2.1 path in git history), .NET, effort L (the runtime). Vanilla Conquer is the pick for the original C&C games (gamedb:command-and-conquer).
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "Ranked table": OpenRA builds GL profiles ANGLE / Modern (3.2 core) / Embedded (GLES 3.0), no legacy path; .NET runtime: Microsoft's has no ppc64 port, Debian ships Mono 6.14.1 for ppc64, OpenRA targets .NET 6/8, not Mono. Verdict: drop.
- 2026-10-19, [gl3-census-c-rts.md](../../research/gl3-census-c-rts.md), "OpenRA (own C# engine on .NET, SDL2 + OpenAL, master of 2026-10-11, net10.0)": out: GL 3.2 core or GLES 3.0 (the GL 2.1 "Legacy" profile was removed after release-20231010), and current OpenRA needs .NET 10, which has no big-endian PowerPC port; only an old Mono-era build (2021-2022, GL 2.1 Legacy profile) would run in theory. Scores: popularity 4, novelty 5, playability 1, effort XL; data free (downloaded by the game).
