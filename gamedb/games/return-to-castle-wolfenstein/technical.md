# Return to Castle Wolfenstein (iortcw): technical notes

Also known as: iortcw (the engine).

## Engine options

- iortcw (the pick: ioq3 line, GL 1.x/2.x, stable; 763 stars)
- RealRTCW (an active gameplay overhaul on iortcw, heavier)
- rtcwPro (a competitive MP mod)
- id's RTCW-SP (the frozen 2012 dump, reference)

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-26, [docs/backlog/games.md](../../../docs/backlog/games.md), "Games to smoke next, and Wesnoth's music": "with the user's own data: ... iortcw"; not started.
- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Action and shooters": RTCW: iortcw, playable, GL 1.x/2.x; Q3's QVM had a big-endian PPC JIT; own data; effort M.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "iortcw (Return to Castle Wolfenstein) and ET:Legacy": no hard no: an OpenGL 1 renderer (default) and an optional "rend2" GL2/GLSL one. Debian contrib `iortcw` 1.51.c+dfsg1-10, no ppc64 binary; a trivial Makefile build. Big-endian: ioquake3 lineage (`idppc`, `Q3_BIG_ENDIAN`, `LittleLong`); PPC OS X builds of ioq3/iortcw shipped for years; BE cost S. RAM estimate 150-350 MB with low textures (`com_hunkMegs` default 128). Data: RTCW retail pak0.pk3 (a single-player demo existed, not verified with iortcw). Verdict: WORTH A BUILD (needs the user's RTCW data).
