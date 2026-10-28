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

## Setting up the game data

Walnut Games makes the data for you. Open QuakeSpasm in the app and choose Add game files...; the chooser lists the disc in the drive, mounted discs and sticks, GOG and Steam folders, and the ISOs, installers and archives in Downloads and your home folder. For the free shareware choose Download the free shareware (needs the network): nothing to find. In a terminal it is `walnut-data import quakespasm /path/to/your/copy` for the full game and `walnut-data import -p quake-shareware quakespasm` for the shareware. It runs Walnut's patched `game-data-packager` and installs an ordinary package that Remove data takes away again.

### Which editions work

- **Shareware** (free): version 1.06, the first episode, downloaded as `quake106.zip` by game-data-packager (package `quake-shareware`); a `quake106.zip` you already have works as a source too.
- **The full game**: package `quake-registered`. It needs `id1/pak0.pak` and `id1/pak1.pak` of your copy: a CD, The Offering from GOG (the installed folder or `setup_quake_the_offering_*.exe`), or a Steam `common/Quake` folder. The 2021 re-release can't stand in for it: it has no original paks (it is an edition of its own, below). A CD's 1.01 `pak0.pak` is brought to 1.06 with the patch files game-data-packager downloads.
- **The CD's music**: package `quake-music`, optional. A BIN/CUE image of the CD (give the `.cue`, or the `.bin` next to it) brings the data and the music in one import: walnut-data writes the data track to a temporary ISO and encodes audio tracks 2 to 11 to Ogg Vorbis (oggenc, from vorbis-tools); from the disc in the drive game-data-packager rips the tracks with cdparanoia. Ripped audio has no checksum, so the music is checked more loosely than the paks (exact file names, a size bound, Ogg Vorbis files only).
- **The 2021 re-release** (Nightdive's Quake Enhanced): package `quake-rerelease`, edition Remaster. From GOG's installer `setup_quake_enhanced_1.0_(60828).exe` (607 MB) or that build installed; other builds of the re-release (Steam's, older GOG updates) have not been checked. It takes the re-release's own `id1/pak0.pak` (the whole game in one 172 MB pak, with coloured lighting and the re-release's QuakeC), its English text from `QuakeEX.kpf` and its CD music, 257 MB installed. The import on the PS3 takes about four minutes.
- **The re-release's add-ons**, optional, from the same installer: Scourge of Armagon (`quake-rerelease-armagon`, 103 MB with its music), Dissolution of Eternity (`quake-rerelease-dissolution`, 70 MB with its music), Dimension of the Past (`quake-rerelease-dopa`, 39 MB) and Dimension of the Machine (`quake-rerelease-dimension-of-the-machine`, 474 MB). These are the re-release's own builds, with QuakeC written for it, so they need `quake-rerelease` and don't play with the original game's paks. An import of the installer makes all of them along with the re-release; `-p` picks single ones. Not yet tried on the PS3. The installer's CTF pak (Threewave Capture the Flag) is left out: it is for multiplayer, and QuakeSpasm has no bots.
- The mission packs (`quake-armagon`, `quake-dissolution`) and their music are packages of the same tool; they have not been tried on the PS3.

### What you get

The data ends up in `/usr/share/games/quake/id1`, the music in `/usr/share/games/quake/id1/music` (QuakeSpasm plays `trackNN.ogg` there for the CD track a level asks for). The menu entries QuakeSpasm (GLSL) and QuakeSpasm (no GLSL) read `/usr/share/games/quake`, so they play whichever edition is installed. `quake-registered` replaces `quake-shareware`, so the full game takes over the same folder. The 2021 re-release has a folder of its own, `/usr/share/games/quake-rerelease`, next to the original game, and its own menu entry, Quake (2021 re-release), which shows once it is installed; each add-on goes into a folder there (`hipnotic`, `rogue`, `dopa`, `mg1`) and has its own entry, such as Quake (2021): Dimension of the Machine. The original game keeps its saves and settings in `~/.quakespasm/id1`, the re-release in `~/.quakespasm/rerelease` and each add-on in `~/.quakespasm/<its folder>`. With Walnut's licensed suite as an apt source the page offers Install from Walnut licensed instead.
