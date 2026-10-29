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
RSS): SDL 1.2 software drawing of a 1920x1080 surface through sdl12-compat. The profile
(`ps3-bench-20261029T030029Z-smoke-warmux-play`) has 55% of the frame in one loop of libSDL3, which the
disassembly names as `Blit8888to8888PixelAlphaSwizzle`: the scalar path (no SIMD on PowerPC) for
per-pixel-alpha ARGB8888 sprites onto sdl12-compat's XRGB8888 screen. Our libsdl3's
[0003](../../../packages/patched/graphics/libsdl3/patches/0003-blit-a-faster-scalar-8888-alpha-blit-between-differe.patch)
makes that loop cheaper, bit-identical (shifts in registers, one mask for same-place RGB, alpha 0 and 255
without the blend). On the PS3 (2026-10-29, same boot): the game's counter 7.0 -> 9.8 fps, the picture
unchanged (`ps3-bench-20261029T033532Z-smoke-warmux-play`); under perf libSDL3 55% -> 40%. Frozen Bubble,
which spent 27% in the same loop, stays at its 45 fps cap with the picture right. Next in the profile
(`ps3-bench-20261029T034148Z`): `ConvertAltivec32to32_prefetch` 11% (a whole-frame 32-bit format conversion,
already AltiVec: sdl12-compat's screen surface into its texture, probably a format mismatch to remove) and
the blit itself 8%. That conversion was sdl12-compat's: its screen texture was always in the renderer's
first format (ARGB8888) against the 32-bit screen's XRGB8888;
[sdl12-compat 0001](../../../packages/patched/graphics/sdl12-compat/patches/0001-video-the-window-texture-in-the-application-s-format.patch)
makes it in the application's format. With both (2026-10-29, same boot,
`ps3-bench-20261029T040111Z-smoke-warmux-play`): 17.5 fps on the game's counter (7.0 with neither, 9.8 with
libsdl3 0003 alone), 38.6 s of stalls in 90 (51-57 before), the picture and colours right; Frozen Bubble,
BomberClone and Pingus look right too. A lower resolution scaled by sdl12-compat is the lever left.

