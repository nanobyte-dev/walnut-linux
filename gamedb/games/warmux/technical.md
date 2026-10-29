# Warmux: technical notes

## Status

Debian's Warmux 11.04.1. First run in batch ssdroot3 (gamedb:batch/2026-10-19-ssdroot3-smoke-new): it starts,
the main menu in a small window, the sound running with 26 underruns; 29 MB free, 90 MB peak RSS. Fullscreen:
the config is `~/.config/wormux/config.xml` (the recipe's BACKUP path was wrong); `setup()` now sets
`full_screen` to true when the file exists, which it does after a first run. Menu only, it waits for the user
([benchmark/recipe.sh](benchmark/recipe.sh)).

## Our package

`warmux` 1:11.04.1+repack2-6+ps3.N ([packages/warmux](packages/warmux/)):

- [0001](packages/warmux/patches/0001-Video-fullscreen-at-the-screen-s-size-by-default.patch): fullscreen at the
  screen's size by default (a fresh config started the main menu in a small window). Checked on the PS3
  (2026-10-28, recipe variant `fresh`: fullscreen 1920x1080, the main menu right).
- [0002](packages/warmux/patches/0002-Tile-read-the-map-s-PNG-as-ARGB-on-big-endian-machin.patch): the ground on
  big-endian. In the first local match on the PS3 (variant `play`) every character was "probably misplaced"
  and fell into the water at once ("Winner: Bugzilla"): the ground's tiles test each pixel as a Uint32
  0xAARRGGBB, but tile.cpp read the map's PNG as B, G, R, A bytes (`png_set_bgr`), which is that value only
  on little-endian; the PS3 tested the blue byte as alpha and most of the ground was empty. Big-endian now
  reads A, R, G, B (`png_set_swap_alpha`). Checked on the PS3 (2026-10-29, variant `play`,
  `ps3-bench-20261029T023911Z-smoke-warmux-play`): the characters stand on the Abyss map, the turns pass, the
  picture right. Worth offering upstream.

The match runs at 7 fps (the game's counter; 9 on the Gallium HUD, 51 s of 90 in frames over 100 ms, 110 MB
RSS): SDL 1.2 software drawing of a 1920x1080 surface through sdl12-compat. A lower resolution scaled by
sdl12-compat is the next default to try.

