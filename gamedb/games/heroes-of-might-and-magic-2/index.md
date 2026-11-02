+++
id = "heroes-of-might-and-magic-2"
title = "Heroes of Might and Magic II (fheroes2)"
summary = "The 1996 turn-based fantasy strategy game: lead heroes across the map, build castles and fight tactical battles, played with fheroes2 from your own copy."
tags = ["strategy", "fantasy", "single-player", "2d", "top-down"]
license = "GPL-2.0-or-later engine (fheroes2), commercial game data"
license_source = "debian/copyright of Debian's fheroes2 1.1.17+dfsg-1 (GPL-2.0+); the game data is the player's own copy"
homepage = "https://github.com/ihhub/fheroes2"
data_guide = "https://wiki.debian.org/Games/GameDataPackager"
data_steps = [
  "Copy the DATA and MAPS folders of your Heroes of Might and Magic II (the original CD, GOG's install or the demo; HEROES2.AGG and the maps) into `~/.local/share/fheroes2` on the console.",
  "Walnut Games can't import this data yet (game-data-packager has a heroes2 recipe, but Walnut's import doesn't know it); the folder copy is the way for now.",
  "Heroes II then starts from Play with the fheroes2 menu entry.",
]
controller = "partial"
designed_for = "keyboard-mouse"
known_issues = [
  "The game hung in the only session so far: about five minutes in, on the adventure map, the window stopped answering while the main thread was still in its loop and the kernel log showed no GPU error. It was killed for a core dump. It is not known yet whether it repeats; todo.toml has the investigation.",
  "Nobody has tried a battle, saving and loading or the gamepad in a played session yet.",
]
+++

fheroes2 is the open engine for Heroes II; Walnut builds it for the PS3 from Debian's source, with the game starting
fullscreen at 1080p. In the first played session the adventure map was drawn right at 1920x1080 with music and
effects, but the game hung about five minutes in and had to be killed, so it rates as a crash. It only redraws
when something changes, so the few frames a second in the record say little. The game's data is the player's own.
