# Setting up the game data

Walnut Games makes the data for you. Open Yamagi Quake II in the app and choose Add game files...; the chooser lists the disc in the drive, mounted discs and sticks, GOG and Steam folders, and the ISOs, installers and archives in Downloads and your home folder. For the free demo choose Download the free demo (needs the network): nothing to find. In a terminal it is `walnut-data import yquake2 /path/to/your/copy` for the full game and `walnut-data import -p quake2-demo-data yquake2` for the demo. It runs Walnut's patched `game-data-packager` and installs an ordinary package that Remove data takes away again.

## Which editions work

- **The demo** (free): version 3.14, downloaded by game-data-packager itself (no copy or installer needed). Package `quake2-demo-data`; the Quake II (demo) menu entry starts it and shows only while the demo is installed.
- **The full game**: package `quake2-full-data`. It needs `baseq2/pak0.pak` and the `baseq2/video` folder of your copy: a CD, a Steam `common/Quake 2` folder, or GOG's Quake II Quad Damage (the installed folder or `setup_quake2_quad_damage_*.exe`). The import downloads the 3.20 patch (the official patch files) unless the copy already has it.
- **The CD's music**: package `quake2-music`, optional, in `/usr/share/games/quake2/baseq2/music/02.ogg`..`11.ogg`, which Yamagi Quake II plays in place of the CD. A BIN/CUE image of the CD (the `.cue`, or the `.bin` next to it) gives it: walnut-data writes the data track to a temporary ISO and encodes audio tracks 2 to 11 to Ogg Vorbis (oggenc, from vorbis-tools); from the disc in the drive game-data-packager rips the tracks with cdparanoia. The 3.20 patch download is allowed only when every package asked for may download, so the full game and its music are two imports: `walnut-data import -p quake2-full-data yquake2 /path/to/Quake2.cue`, then `walnut-data import -p quake2-music yquake2 /path/to/Quake2.cue`. Ripped audio has no checksum: the music is checked more loosely (exact file names, a size bound, Ogg Vorbis files only).
- The expansions (`quake2-reckoning-data`, `quake2-groundzero-data`, `quake2-zaero-data`) are packages of the same tool; they have not been tried on the PS3.

## What you get

The demo is installed in `/usr/share/games/quake2-demo/baseq2` and the full game in `/usr/share/games/quake2/baseq2`; the two use different folders. The Quake II (demo) entry passes `-datadir /usr/share/games/quake2-demo`, and the Quake II entry (shown once `baseq2/pak0.pak` is there) passes `-datadir /usr/share/games/quake2`. With Walnut's licensed suite as an apt source the page offers Install from Walnut licensed instead.
