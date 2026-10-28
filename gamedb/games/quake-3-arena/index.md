+++
id = "quake-3-arena"
title = "Quake III Arena"
summary = "id Software's 1999 arena shooter against bots, played with Walnut's ioquake3 from your own copy, with Team Arena if you have it."
categories = ["shooter"]
tags = ["fps", "classic", "3d", "bots", "multiplayer"]
license = "GPL-2.0-or-later engine, commercial game data"
homepage = "https://ioquake3.org/"
data_guide = "https://wiki.debian.org/Games/GameDataPackager"
data_steps = [
  "In Walnut Games, open Quake III Arena and choose Add game files...: pick the disc in the drive, an ISO (or an Alcohol 120% MDF/MDS image) of the CD, an installer or a folder that holds your copy.",
  "Or from a terminal: `walnut-data import quake-3-arena /path/to/your/copy`.",
  "What it needs: `baseq3/pak0.pk3` from the CD (any release), an installed copy (Steam's `common/Quake 3 Arena`) or GOG's Quake III Gold installer (`setup_quake3_2.0.0.2.exe`). The 1.32 point release (pak1 to pak8) is downloaded from id's mirrors when your copy is older; GOG's installer has it already.",
  "Team Arena (optional, quake3-team-arena-data): `missionpack/pak0.pk3`, from GOG's Quake III Gold installer or the Team Arena CD. The Quake III: Team Arena menu entry appears once it is installed.",
  "The data is installed as quake3-data in `/usr/share/games/quake3-data/baseq3` (Team Arena in `/usr/share/games/quake3-team-arena-data/missionpack`), linked from `/usr/lib/quake3` where the engine looks; Quake III Arena then appears in Play. Remove data on the game's page takes it away again.",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
known_issues = [
  "GOG's installer is unpacked whole: importing from it needs about 2 GB free for Quake III Arena (its folder or the CD need less, about 1.6 GB).",
  "Quit from the game's menu: stopping it from outside can leave it hung on its last frame.",
]
+++

Quake III Arena runs on Walnut's build of ioquake3, the engine OpenArena plays on at about 70 frames a
second with its OpenGL 1 renderer. The engine is free; the game's data is not, so Walnut doesn't offer
it: Walnut Games makes a package of it from your own copy (the Overview's Installation, and the Guides). The game code is ioquake3's native
build of id's GPL source, not the bytecode in the paks: ioquake3 has no PowerPC compiler for that
bytecode any more, and the interpreter is slow on the PS3's processor. Quake III loads its first arena,
q3dm1, with the right picture; nobody has played it on Walnut yet.
