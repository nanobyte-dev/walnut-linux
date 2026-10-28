# Diablo (DevilutionX): technical notes

Also known as: DevilutionX (the engine); Hellfire (the expansion, also played by DevilutionX).

## Status

DevilutionX 1.5.5 built by us (`devilutionx` `1.5.5-1+ps3.N`, a dev build from
`gamedb/games/diablo/packages/devilutionx/`, made on 2026-10-28 and not committed: DevilutionX is under the
Sustainable Use License 1.0, non-commercial and not DFSG-free, so whether it goes into the repository and the
public suites is the user's decision). One patch: 0001 opens the audio device in the host byte order
(`AUDIO_S16SYS`; `AUDIO_S16` is little-endian, so the PPE swapped every sample twice). Upstream's defaults are
already console-like: fullscreen at the screen's mode (`SDL_WINDOW_FULLSCREEN_DESKTOP`, the picture widened to
16:9 and scaled up), SDL game controllers on.

The data is the player's: Diablo 1.09's `DIABDAT.MPQ` (517,501,242 bytes, sha256
`6bc120a603fdb383ff91f374e7db599765d6230d81e5a4c1b33f8254911774e7`, md5 `68f049866b44688a7af65ba766bef75a`),
in `~/.local/share/diasurgical/devilution/` (SDL's pref path for "diasurgical", "devilution": not
`devilutionx`; diablo.ini and the saves go there too; `--data-dir` names another folder).

Smoke runs on 2026-10-28 (kernel .874, Mesa ps3.924, the VRAM swap on; the last one
`ps3-bench-20261028T223638Z`): the recipe ([benchmark/recipe.sh](benchmark/recipe.sh)) starts `devilutionx
--diablo -n` with a save folder of the run's own, then Return through the menus and a typed name (DevilutionX
fills one in only for a pad) into Tristram. The title menu, the hero creation, the difficulty and the town are
drawn right (fire, pentagrams, the Warrior, the panel's orbs; xwd and webcam), fullscreen 1920x1080
(`geom fs=1`). In town: 58 fps steady on the Gallium HUD, 1% low 25, 10 frames over 100 ms in 90 s (2.3 s,
the longest 425 ms), 48 MB peak RSS; the sound running with 8 audible dropouts (to be judged by ear). The hero
was saved (`single_0.sv`, an MPQ the game writes). DevilutionX found the DualShock 3 ("PS3 Controller"
mapping); the pad is not tried. Its `--demo`/`--timedemo` options would make a benchmark once a demo is
recorded.

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
