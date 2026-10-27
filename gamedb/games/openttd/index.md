+++
id = "openttd"
title = "OpenTTD"
summary = "A transport simulation: build railways, roads, ships and airports and run a profitable company."
categories = ["simulation", "strategy"]
tags = ["transport", "management", "tycoon", "multiplayer"]
license = "GPL-2.0-only"
homepage = "https://www.openttd.org/"
controller = "keyboard"
designed_for = "keyboard-mouse"
known_issues = [
  "With the OpenGL driver, water pulses to white or very bright (its palette animation); the software driver doesn't.",
  "With the 32bpp-anim blitter the same game runs at 11 frames a second; NewGRFs that ask for 32bpp bring it back.",
]
+++

OpenTTD 15.1 runs with its OpenGL video driver, which Walnut's patches make start on the RSX (stock
refuses on the missing single-channel texture formats). Walnut sets the 8bpp-optimized blitter as the
default, which takes the benchmark savegame from 11 to about 38 to 45 frames a second.

## Original graphics and sound

OpenTTD plays with its free OpenGFX, OpenSFX and OpenMSX. If you own Transport Tycoon Deluxe for Windows, Walnut Games can add its original graphics, sounds and music: open OpenTTD and choose Add game files..., then pick the disc in the drive, an ISO, an installer or a folder; in a terminal it is `walnut-data import openttd /path/to/TTD`. It needs the `trg*.grf` files and `sample.cat` of the Windows version, and takes its music in either arrangement: the General MIDI `gm_tt*.gm` files or the FM synthesizer ones, `fm/9_tt*.fm`, which some copies have instead. The data is installed as `transport-tycoon-deluxe-data`, with `-music` (General MIDI, OpenTTD's `original_windows` music set) or `-music-fm` (the FM files and their own set, `original_windows_fm`, which our game-data-packager writes); both are Standard MIDI files that OpenTTD plays through FluidSynth. Once `trg1r.grf` exists the game offers a launcher with the original sets (`-I`/`-S original_windows`, and `-M` the General MIDI music when it is there, else the FM music); it picks them at every start, so a music choice made in its options lasts until the next start. Remove data takes it away.
