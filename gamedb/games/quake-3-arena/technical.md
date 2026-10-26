# Quake III Arena: technical notes

## Engine options

- ioquake3 (the pick: gamedb:openarena runs on our ioquake3 engine, gamedb/engines/ioquake3)
- Quake3e (a performance fork: x86/ARM JIT, Vulkan; no PPC JIT)
- spearmint (not fetchable)
- id's Quake-III-Arena (frozen dump, reference)

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Action and shooters": "RTCW, Wolfenstein: ET, Quake 3, Elite Force" (mostly with console versions): iortcw, ET: Legacy, ioquake3; playable; GL 1.x/2.x; Q3's QVM had a big-endian PPC JIT; own data; effort M.
- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Alternatives and popularity": ioquake3 (2850 stars, 2026-10-12): the baseline, PPC JIT history.
