# Setting up the game data

## Which editions work

- **A Quake III Arena CD** (or its ISO, or an MDF/MDS image of it): `baseq3/pak0.pk3`. The CD's release is older than 1.32, so the import downloads id's 1.32 point release (`linuxq3apoint-1.32b-3.x86.run`, 30 MB) for pak1 to pak8, the files ioquake3 needs.
- **GOG's Quake III Gold** (`setup_quake3_2.0.0.2.exe`, or the folder it installs): Quake III Arena and Team Arena, both at 1.32; nothing is downloaded. The installer is unpacked whole, so it needs more free space than the folder.
- **Steam's** `common/Quake 3 Arena` folder: the same files as GOG's.
- The demo (`quake3-demo-data`) is a package of the same tool; it has not been tried.

## What you get

Two menu entries, shown once their data is there: Quake III Arena (`fs_basepath /usr/lib/quake3/base`) and Quake III: Team Arena (`/usr/lib/quake3/ta`, `fs_game missionpack`). Both start fullscreen at the TV's resolution with the OpenGL 1 renderer and a pad mapping (triggers fire and zoom, shoulders change weapons, A jumps, B crouches, Start opens the menu), from Walnut's `autoexec.cfg` in each folder.
