# Chromium B.S.U.: technical notes

## Status

Debian's Chromium B.S.U. 0.9.16.1 (`chromium-bsu`, `-data`, 1 MB) runs. Smoke runs on 2026-10-28 (kernel .874, Mesa
ps3.924; the last one `ps3-bench-20261028T201110Z`): Return on "new game" and a few shots and moves through XTEST
(the recipe's `during()`; the first runs never got their keys: xkeys.py looked windows up with XFetchName, which
misses sdl2-compat's UTF-8 window names, fixed the same night), then the level plays: 55 fps steady on the Gallium
HUD (the game's own counter says 53), 1% low 42, one frame over 100 ms (the capture), 37% CPU, 48 MB RSS, 31 MB of
VRAM; sound running, no dropouts logged. The webcam shows the same picture as the capture.

Two problems with Debian's build, both fixed in our package:

- The recipe wrote `screenHigh` (the game reads `screenHeight`), so the game kept 600 lines and drew into the
  bottom 1920x600 of the screen, the rest grey (the TV the same). With the right key the whole screen is used,
  but the projection's aspect stays 4:3 whatever size is read, so everything is a third wider than it should be.
- Fullscreen is off by default (an 800x600 window), and Debian builds no joystick support (`WITH_JOYSTICK` is
  defined nowhere).

Our package (dev build `0.9.16.1-5+ps3.931~dev`, installed with `dpkg -i`), `run.sh smoke chromium-bsu fresh` with no
configuration at all (`ps3-bench-20261028T210239Z`): fullscreen 1920x1080, the field in its proportions with the
clear colour at the sides, 54 fps steady, 1% low 41, sound running, no audible dropouts. The pad scheme is not
tried yet (no pad at the console tonight).

Played session `ps3-bench-20261029T203517Z` (ps3.931~dev, 2026-10-29): 4.4 min, quit with exit code 0, 246 s of play,
fullscreen 1920x1080 (geom= `1920x1080+0+0 fs=1`, the four shots show the field in proportion, the clear colour at the
sides red after a hit and grey otherwise). Median 56.1 fps, 1% low 41.1, longest frame 236.5 ms, 0.15 s a minute of
stutter, load 17 s, 50 MB RSS, 0 sound dropouts. The form: sound and music heard, controls full (the DualShock opened,
27022 pad events in play, no keyboard presses), no glitches. No PC reference sheet. Rated Platinum 97, provisional:
stability is scored from 4 minutes of play (90, the start of a crash-free game; the hours-based rule needs three).


- `chromium-bsu`, `chromium-bsu-data` (`0.9.16.1-5+ps3.N`, [packages/chromium-bsu](packages/chromium-bsu/)):
  Debian's source with two patches. In no suite; CI builds it for testing.
- [0001](packages/chromium-bsu/patches/0001-Fullscreen-by-default-at-the-screen-s-size-the-pictu.patch): fullscreen
  by default, as `SDL_WINDOW_FULLSCREEN_DESKTOP` (the PS3's mode can't change); the viewport, the mouse and the
  projection follow the drawable's real size, so 16:9 shows more of the field instead of stretching it; the
  aspect follows any size read from the configuration too.
- [0002](packages/chromium-bsu/patches/0002-Game-controllers-a-fixed-built-in-scheme-next-to-the.patch): a built-in
  pad scheme through SDL's game controller API (left stick and D-pad move, Cross or R1 fire, Square or L1 the
  item, Triangle the next item, Start the menu and back, Circle back from the menu), turned into the keys the
  game reads, so the menus work too.

Smoke-tested: one launch to see whether it starts and plays (`SUITE=0`, no benchmark numbers kept; the smoke
runs are described in [SUITE.md](../../../docs/games/SUITE.md#smoke-runs)). The recipe
([benchmark/recipe.sh](benchmark/recipe.sh)): medium graphics, OpenAL, its own fps counter on, then into play
through XTEST. Variants `full`, `min`, `noblend`, `nosound`, `menu`, `fresh` (no configuration: the package's
defaults).
