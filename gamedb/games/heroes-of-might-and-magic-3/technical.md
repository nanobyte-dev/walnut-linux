# Heroes of Might and Magic III (VCMI): technical notes

Also known as: VCMI (the engine).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Strategy": Heroes of Might and Magic III (1999): VCMI, playable, SDL2, own data, effort M-L (RAM, Boost); 6.0k stars, no other engine found.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "vcmi (Heroes 3 engine)": no hard no: SDL2 client, a software renderer possible; an SDL3 client too. Debian contrib `vcmi` 1.7.3+dfsg-1, no ppc64 binary; a heavy C++ build (boost, SDL2 + image/mixer/ttf, minizip, TBB; M). Big-endian real support (`CBinaryReader` converts LE data on BE hosts, `test_big_endian`); BE cost S-M. RAM estimate 600 MB-1.2 GB (battle creature DEFs unpacked to 32-bit surfaces). Data: HoMM III files (GOG or CD), no free set. Effort L. Verdict: NOT WORTH IT (RAM, build weight, no free data).
