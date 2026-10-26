# FreeCol: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "Ranked table": FreeCol 1.2.0 (Java, Swing, no GL); Debian main (arch all) + HotSpot. The game refuses to start below ~0.55 GB heap ("You need to assign more than 518979584 bytes", measured with -Xmx512M) and warns "recommended 2 GB" at 600M; a patch could drop the check but the maps and AI need the memory. Effort M. Verdict: not worth it (RAM by its own check).
