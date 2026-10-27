# Quake III Arena: technical notes

## Status

Untested as a played game: published so that Walnut Games can set up its data. On 2026-10-27 the data
came from the CD's ISO on the PS3 and ioquake3 loaded q3dm1 with the right picture (lightmaps, models,
HUD; xwd fullscreen, looked at); nobody has played a session or measured it yet.

## Packages, data and settings

- **Engine**: our `ioquake3` (gamedb:ioquake3, OpenArena's engine), `/usr/lib/ioquake3/ioquake3`. Its
  package already has Debian's native game code links: `/usr/lib/quake3/base/baseq3/{cgame,qagame,ui}.so`
  for Quake III and `/usr/lib/quake3/ta/{baseq3,missionpack}/` for Team Arena. The launchers set
  `vm_cgame 0`, `vm_game 0`, `vm_ui 0` and `sv_pure 0`, so ioquake3 loads those native libraries
  (ioquake3's build of id's GPL game code, the same game as 1.32's bytecode): ioquake3 has no PowerPC
  compiler for the QVM bytecode any more (only x86, x86-64 and ARM), and the interpreter is slow on the
  PPE. With `sv_pure 1` the client would insist on the paks' bytecode.
- **Data**: game-data-packager's `quake3-data` (`/usr/share/games/quake3-data/baseq3/pak0-8.pk3`) and
  `quake3-team-arena-data` (`.../quake3-team-arena-data/missionpack/pak0-3.pk3`), each with gdp's links
  into `/usr/lib/quake3/{base,ta}`. Those links are why the data manifest and walnut-games-helper take
  symlinks now (docs/PACKAGING.md "Links"). Team Arena's Conflicts/Replaces `quake3-data (<< 77~)` is
  about old gdp builds and is left out of the manifest: unversioned it would remove quake3-data.
- **Launchers** ([desktop/launchers](desktop/launchers/)), both `NoDisplay=true` with
  `X-Walnut-Requires=` the pak0 of their data: "Quake III Arena" (`fs_basepath /usr/lib/quake3/base`) and
  "Quake III: Team Arena" (`fs_basepath /usr/lib/quake3/ta`, `fs_game missionpack`).
- **Defaults**: `autoexec.cfg` in `/usr/lib/quake3/base/baseq3/` and `/usr/lib/quake3/ta/baseq3/`
  ([desktop/files](desktop/files/)), OpenArena's: `opengl1`, `r_mode -2` fullscreen at the desktop's
  resolution, vsync, `com_maxfps 60`, DXT1 textures, the pad bindings; and `cl_cdkeyCheck 0`
  (ioquake3 0030): without it both menus open on a CD KEY screen at every start. `ui_cdkeychecked` can't
  skip it: the UIs register it `CVAR_ROM`, which forces a value set on the command line or in a config
  back to 0. No key is shipped or needed (it mattered only to id's authorize servers).
- **Settings** live in ioquake3's XDG folders: `~/.config/Quake3`, `~/.local/state/Quake3` (`~/.q3a` when it exists).

## Data sources tried

- **CD ISO** (`QUAKE3.iso`, 1.11-era: `Quake3/baseq3/pak0.pk3` and pak1): loop-mounted by walnut-data;
  gdp needs 1.32's pak1-8 too, from id's point release `linuxq3apoint-1.32b-3.x86.run`. gdp downloaded it
  from `ftp.fu-berlin.de` (idstuff) in the games builder on the PC. On the PS3 the import ran with
  `--no-download` and GOG's pak1-8 beside the ISO, for lack of room for the download's 128 MB.
- **GOG's Quake III Gold** (`setup_quake3_2.0.0.2.exe`, 921 MB, known to gdp): gdp unpacks
  `baseq3/pak0.pk3` as well when only Team Arena is asked for. On the PS3 that ran out of space at 4x the
  package, and walnut-data's space check now counts an installer's unpack as every package of the game.
  The installer needs about 2 GB free for quake3-data alone. Built from it in the games builder on the PC:
  `quake3-team-arena-data`.
- **MDF/MDS**: the CD's ISO written as a 2448-byte-sector MDF (2352 + 96 bytes of subchannel) with its
  `.mds` and extracted again by walnut-data's code gave the ISO 9660 volume byte for byte (on the PC).

## Known problems

- Sound drops out for 80-170 ms about once a minute in a game with bots (OpenAL's `mmap commit error:
  Broken pipe` in the log, one per dropout): OpenAL's mixer thread starves at normal priority. With a
  real-time limit for the session there were none in 3 runs; that change is system-wide and not made
  yet ([the investigation](../../engines/ioquake3/investigations/2026-10-28-sigterm-hang-and-openal-xruns.md)).

## Engine options

- ioquake3 (the pick: gamedb:openarena runs on our ioquake3 engine, gamedb/engines/ioquake3)
- Quake3e (a performance fork: x86/ARM JIT, Vulkan; no PPC JIT)
- spearmint (not fetchable)
- id's Quake-III-Arena (frozen dump, reference)

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Action and shooters": "RTCW, Wolfenstein: ET, Quake 3, Elite Force" (mostly with console versions): iortcw, ET: Legacy, ioquake3; playable; GL 1.x/2.x; Q3's QVM had a big-endian PPC JIT; own data; effort M.
- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Alternatives and popularity": ioquake3 (2850 stars, 2026-10-12): the baseline, PPC JIT history.
