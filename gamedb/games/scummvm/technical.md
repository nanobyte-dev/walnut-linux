# ScummVM: technical notes

## Status

Debian's ScummVM 2026.1.0 with Beneath a Steel Sky. First run in batch ssdroot3
(gamedb:batch/2026-10-19-ssdroot3-smoke-new): it starts in the game's intro art at a 5 fps HUD median, the sound
running (20 underruns; the mic hears it 12 dB over the room's floor); 56 MB free, 85 MB peak RSS. Escape never
went out: `xkeys` found no window named `ScummVM*` 8 s in, so the recipe ([benchmark/recipe.sh](benchmark/recipe.sh))
now waits for the game's title (unverified); `--gfx-mode=normal` was "unrecognized" and is dropped. Its ALSA
sequencer errors (`/dev/snd/seq`) are the MIDI port probe, harmless with the AdLib emulator. After the intro
skip: queen, drascula and lure; not played yet.
