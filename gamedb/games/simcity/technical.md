# SimCity (Micropolis): technical notes

Also known as: Micropolis (the GPL release of SimCity's code).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-26, [docs/backlog/games.md](../../../docs/backlog/games.md), "Games to smoke next, and Wesnoth's music": "Also Micropolis (packaging only)"; not started.
- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Adventure, sim, casual": SimCity (1989): Micropolis (official GPL), complete, effort S; alternatives: micropolis (1.1k stars, last push 2026-02-10, the pick), MicropolisCore (C++ rewrite, 211 stars).
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "Ranked table": Micropolis: no GL (Tk/X11 + SDL 1.2 sound). Debian dropped it (`micropolis-activity` 0.0.20071228-10 is gone from sid). Package from upstream (SimHacker/micropolis, GPL) or snapshot.debian.org source; Tcl/Tk 8.6 + X11 shm patches; big-endian: Sun/SGI-era code, swap macros not verified. RAM estimate 30-60 MB. Effort S-M. Verdict: worth a build (cheap, novel, low appeal).
