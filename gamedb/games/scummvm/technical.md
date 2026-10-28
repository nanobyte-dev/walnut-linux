# ScummVM: technical notes

## Status

2026-10-29 (games night; kernel .874, Mesa ps3.924, Debian's `scummvm` 2026.4.0+dfsg-1 installed fresh, the
VRAM swap on): the recipe's keys now reach the window (`xkeys.py` had missed sdl2-compat's UTF-8 window names;
fixed 2026-10-28). Two of the player's own games, copied from the PC to `~/Games/scummvm/<GAME>` (the folder
a player points "Add Game" at; never in the repository), recipe variants `dott` and `monkey2`:

- Day of the Tentacle, CD talkie, Spanish (`TENTACLE.000` sha256 `dd673f21...0d600`, `TENTACLE.001`
  `865ab1a6...97dbb`, `MONSTER.SOU` `1819b516...aee5b`): Escape twice skips the intro, then Bernard in the
  hotel lobby with the verbs and inventory, colours right, fullscreen with the 4:3 picture in bars
  (`ps3-bench-20261028T224303Z-smoke-scummvm-dott`). 90 MB peak RSS, sound running, no audible dropouts.
- Monkey Island 2, floppy, Spanish (`MONKEY2.000` sha256 `bdca6e56...88ac5`, `MONKEY2.001` `f5cb9fc5...8b3`):
  stops at the game's own copy-protection screen (the recipe for bad breath), drawn right; ScummVM doesn't
  skip it for this version.

The Gallium HUD numbers (6-9 fps, "stalls") mean nothing here: ScummVM presents a frame only when the picture
changes. Beneath a Steel Sky (the base recipe) was not re-run: its data package isn't installed and the disk
was at 1.9 GB free.

Earlier, 2026-10-19:

Debian's ScummVM 2026.1.0 with Beneath a Steel Sky. First run in batch ssdroot3
(gamedb:batch/2026-10-19-ssdroot3-smoke-new): it starts in the game's intro art at a 5 fps HUD median, the sound
running (20 underruns; the mic hears it 12 dB over the room's floor); 56 MB free, 85 MB peak RSS. Escape never
went out: `xkeys` found no window named `ScummVM*` 8 s in, so the recipe ([benchmark/recipe.sh](benchmark/recipe.sh))
now waits for the game's title (unverified); `--gfx-mode=normal` was "unrecognized" and is dropped. Its ALSA
sequencer errors (`/dev/snd/seq`) are the MIDI port probe, harmless with the AdLib emulator. After the intro
skip: queen, drascula and lure; not played yet.
