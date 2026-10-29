+++
id = "quakespasm"
title = "QuakeSpasm"
summary = "A modern source port of id Software's Quake, with the original campaign and mods."
tags = ["fps", "horror", "single-player", "3d", "first-person", "mods"]
license = "GPL-2.0-or-later"
homepage = "https://quakespasm.sourceforge.net/"
data_guide = "https://quakespasm.sourceforge.net/"
data_steps = [
  "In Walnut Games, open QuakeSpasm and choose Add game files...: pick the disc in the drive, an ISO or BIN/CUE image, an installer or a folder that holds your copy.",
  "Or from a terminal: `walnut-data import quakespasm /path/to/your/copy`. The free shareware needs no copy: choose Download the free shareware (needs the network) in the chooser, or run `walnut-data import -p quake-shareware quakespasm`.",
  "What it needs: Quake's CD or The Offering (GOG), a Steam `common/Quake` folder. The 2021 re-release works as its own edition from GOG's Quake Enhanced installer (`setup_quake_enhanced_1.0_(60828).exe`), with its own menu entry, Quake (2021 re-release). The free shareware (the first episode) is downloaded for you (`quake106.zip`).",
  "The data is installed as quake-registered (or quake-shareware), in `/usr/share/games/quake/id1`, where the QuakeSpasm menu entries look. The full game replaces the shareware.",
  "The CD's music (optional, quake-music): from a BIN/CUE image its audio tracks are encoded to Ogg Vorbis; from the disc in the drive game-data-packager rips them. `walnut-data import -p quake-music quakespasm /path/to/QUAKE.cue` adds it alone. QuakeSpasm plays `id1/music/track02.ogg`..`track11.ogg` in place of the CD.",
  "If you have Walnut's licensed suite configured as an apt source, choose Install from Walnut licensed (or `walnut-data install-repo quakespasm`) instead.",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
+++

QuakeSpasm 0.96.3 is Debian's package, unpatched, and plays the shareware demo1 timedemo at about 91
frames a second. It is started with `-nopackedpixels`, because 0.96.3's packed lightmaps show wrong
colours on a big-endian CPU. It needs Quake's `id1/pak0.pak` (the shareware one is enough for the
first episode); the data is not part of the package. A few frames take up to 400 ms, 2.5 s in
total over a two minute window.
