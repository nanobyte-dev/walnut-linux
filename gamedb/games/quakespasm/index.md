+++
id = "quakespasm"
title = "QuakeSpasm"
summary = "A modern source port of id Software's Quake, with the original campaign and mods."
categories = ["shooter"]
tags = ["fps", "classic", "3d", "single player", "mods"]
license = "GPL-2.0-or-later"
homepage = "https://quakespasm.sourceforge.net/"
data_guide = "https://quakespasm.sourceforge.net/"
data_steps = [
  "In Walnut Games, open QuakeSpasm and choose Set up game data...: pick the disc in the drive, an ISO, an installer or a folder that holds your copy.",
  "Or from a terminal: `walnut-data import quakespasm /path/to/your/copy`. The free shareware needs no copy: choose Download the free shareware (needs the network) in the chooser, or run `walnut-data import -p quake-shareware quakespasm`.",
  "What it needs: Quake's CD or The Offering (GOG), a Steam `common/Quake` folder. The 2021 re-release is not supported. The free shareware (the first episode) is downloaded for you (`quake106.zip`).",
  "The data is installed as quake-registered (or quake-shareware), in `/usr/share/games/quake/id1`, where the QuakeSpasm menu entries look. The full game replaces the shareware.",
  "If you have Walnut's licensed suite configured as an apt source, choose Install from your repository (or `walnut-data install-repo quakespasm`) instead.",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
+++

QuakeSpasm 0.96.3 is Debian's package, unpatched, and plays the shareware demo1 timedemo at about 91
frames a second. It is started with `-nopackedpixels`, because 0.96.3's packed lightmaps show wrong
colours on a big-endian CPU. It needs Quake's `id1/pak0.pak` (the shareware one is enough for the
first episode); the data is not part of the package. A few frames take up to 400 ms, 2.5 s in
total over a two minute window.

## Setting up the game data

Walnut Games makes the data for you. Open QuakeSpasm in the app and choose Set up game data...; the chooser lists the disc in the drive, mounted discs and sticks, GOG and Steam folders, and the ISOs, installers and archives in Downloads and your home folder. For the free shareware choose Download the free shareware (needs the network): nothing to find. In a terminal it is `walnut-data import quakespasm /path/to/your/copy` for the full game and `walnut-data import -p quake-shareware quakespasm` for the shareware. It runs Walnut's patched `game-data-packager` and installs an ordinary package that Remove data takes away again.

### Which editions work

- **Shareware** (free): version 1.06, the first episode, downloaded as `quake106.zip` by game-data-packager (package `quake-shareware`); a `quake106.zip` you already have works as a source too.
- **The full game**: package `quake-registered`. It needs `id1/pak0.pak` and `id1/pak1.pak` of your copy: a CD, The Offering from GOG (the installed folder or `setup_quake_the_offering_*.exe`), or a Steam `common/Quake` folder. The 2021 re-release (Quake enhanced) is not supported: it has no original paks. A CD's 1.01 `pak0.pak` is brought to 1.06 with the patch files game-data-packager downloads.
- The mission packs (`quake-armagon`, `quake-dissolution`) are packages of the same tool; they have not been tried on the PS3.

### What you get

The data ends up in `/usr/share/games/quake/id1`. The menu entries QuakeSpasm (GLSL) and QuakeSpasm (no GLSL) read `/usr/share/games/quake`, so they play whichever edition is installed. `quake-registered` replaces `quake-shareware`, so the full game takes over the same folder. With Walnut's licensed suite as an apt source the page offers Install from your repository instead.
