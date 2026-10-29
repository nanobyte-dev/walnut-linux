# Heroes of Might and Magic II (fheroes2): technical notes

## Status

Our `fheroes2` 1.1.17+dfsg-1+ps3.N ([packages/fheroes2](packages/fheroes2/)): Debian's contrib source built for
ppc64 unchanged (debian-ports builds nothing from contrib). Built on 2026-10-29. The
engine reads the player's Heroes II data from `~/.local/share/fheroes2` or `/usr/share/games/fheroes2` (its
`DATA/` and `MAPS/` folders: HEROES2.AGG and the maps from the original game, GOG's install or the demo).

Smoke runs on 2026-10-29 (kernel .874, Mesa ps3.924, the VRAM swap on; the dev build installed by `dpkg -i`) with
the player's own Heroes of Might and Magic II Gold: `DATA/` and `MAPS/` (76 MB; `HEROES2.AGG` sha256
`7a11c86d...04315d`, `HEROES2X.AGG` `1f3edad1...f4d41`) in `~/.local/share/fheroes2`. The recipe
([benchmark/recipe.sh](benchmark/recipe.sh)) writes a configuration with fullscreen 1920x1080, the first-run
dialogs and the intro off, then clicks by XTEST: New Game, Standard Game, Okay on the first map ("4
Dimensions"). The main menu (the 4:3 picture in the middle, the sides in the game's own brown), the
scenario dialog and the adventure map at the full 1920x1080 (the map, the radar, the heroes and castles
panel, the day, the scenario's story) are drawn right, colours included (`ps3-bench-20261029T022739Z-smoke-heroes-of-might-and-magic-2-base`, xwd; webcam). 88 MB peak RSS,
sound running with 9 audible dropouts. The Gallium HUD's few fps mean little: the game presents only when
something changes. Not checked: a turn, a battle, the pad.

On an empty configuration (variant `fresh`) it opens a 640x480 window with the language dialog first: a
console default for our package (fullscreen at the screen's size, the first-run questions answered).

Also known as: fheroes2 (the engine).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-26, [docs/backlog/games.md](../../../docs/backlog/games.md), "Games to smoke next, and Wesnoth's music": on the "try now" list (HoMM2 demo data); not started.
- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Shortlist / Strategy": shortlist #8 (never on a console): complete, very active, SDL2, free demo data, effort M; 3.5k stars, the only active one.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "fheroes2 (Heroes 2 engine)": no hard no: SDL2 + software compositing of an 8-bit palette surface (`src/engine/screen.cpp` has `SDL_BIG_ENDIAN` branches). Debian contrib `fheroes2` 1.1.17+dfsg-1, no ppc64 binary; tiny Build-Depends (sdl2, sdl2-mixer, libsmacker, zlib): S. Big-endian: explicit `getLE16`/`getBE16` helpers; no BE issue found; BE cost S. RAM estimate 100-250 MB (the world-map sprite cache and 32-bit 1080p output are the consumers; run 640x480 or scale). Data: the HoMM II demo is supported (README, `extract_homm2_resources`); full data from GOG. Effort S. Verdict: TRY NOW, the second best of part A.
