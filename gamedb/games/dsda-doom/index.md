+++
id = "dsda-doom"
title = "dsda-doom"
summary = "A Doom source port with an OpenGL renderer, accurate demo playback and many mod features."
tags = ["fps", "single-player", "first-person", "mods"]
license = "GPL-2.0-or-later"
license_source = "debian/copyright of Debian's dsda-doom 0.30.1+dfsg-1 and of freedoom 0.13.0-3 (BSD-3-clause)"
homepage = "https://github.com/kraflab/dsda-doom"
controller = "keyboard"
designed_for = "keyboard-mouse"
known_issues = [
  "The MIDI music chops: about 465 sound underruns a minute with the synthesizer running on the SPEs.",
]
+++

dsda-doom 0.30.0 runs on its OpenGL renderer at about 56 frames a second with Freedoom's data, with
no stalls. Walnut's patches fix the little-endian sound lumps, upload the indexed textures in a format
the RSX has (stock GL mode left walls and sprites missing or black), and batch the GL draws.
