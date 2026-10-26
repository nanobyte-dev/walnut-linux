# Diablo (DevilutionX): technical notes

Also known as: DevilutionX (the engine); Hellfire (the expansion, also played by DevilutionX).

## Engine options

- DevilutionX (the pick: complete, very active, 9.9k stars, BE prior art)
- devilution (the matching decompilation, Windows exe: DevilutionX's source)
- freeablo (stalled since 2023)

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-26, [docs/backlog/games.md](../../../docs/backlog/games.md), "Games to smoke next, and Wesnoth's music": on the "try now" list (shareware `spawn.mpq`); not started.
- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "RPG": Diablo + Hellfire (1996, PS1 too): DevilutionX, complete, SDL software; a Mac OS X Tiger PPC build (45 fps on an iBook G4), Amiga 68k, a BE network fix; shareware free; effort S.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "devilutionx (Diablo 1)": no hard no: the game renders an 8-bit palette surface itself, `SDL_CreateRenderer` only presents it; SDL1.2, SDL2 or SDL3. Not in Debian: own build (CMake, bundled libs). Big-endian good: explicit LE loads/stores (`Source/utils/endian_*.hpp`); changelog "Added support for 32bit PowerPc", "macOS 10.4 PowerPC", "Correct networking support on big-endian systems"; Amiga M68K port. RAM estimate 60-120 MB (runs on the 3DS in under 100 MB). Data: `spawn.mpq` from the Blizzard shareware (first dungeon levels; tolerated, not an open licence); full game `DIABDAT.MPQ` (GOG). Effort S. Verdict: TRY NOW, the best 2D candidate of part A.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "Addendum: GameOS port exists?": no known PS3 port (its README's platform list has no PS3).
