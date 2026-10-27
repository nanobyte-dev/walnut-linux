# Hexen: Beyond Heretic: technical notes

## Status

Data import tested, play not: GOG's installer imported on the PS3 (2026-10-27, 85 s) and DSDA-Doom reached
the title and played demo1 with the launcher's arguments; nobody has played it on Walnut yet. Its launcher starts DSDA-Doom 0.30.0 (Debian's package with Walnut's patches, gamedb:dsda-doom),
which plays Heretic and Hexen too (started as `dsda-hexen`, Debian's link to it); it plays Freedoom 2 at about 56 fps
on its OpenGL renderer.

- **Data**: game-data-packager's `hexen-wad` (build.toml `[data_setup]`), made from the player's own copy, in
  `/usr/share/games/doom/hexen.wad`.
- **Launcher** ([desktop/launchers](desktop/launchers/)), `NoDisplay=true` with `X-Walnut-Requires=` the
  WAD (shown once the menu rule for it exists, PLAN 5.10):
  `dsda-hexen -iwad /usr/share/games/doom/hexen.wad -vidmode gl -width 1920 -height 1080 -fullscreen`.
- **Deathkings of the Dark Citadel**: gdp's `hexen-deathkings-wad` (HEXDD.WAD 1.0 or 1.1, in
  `/usr/share/games/doom/hexdd.wad`) is optional data (edition `expansion`); its own launcher adds
  `-file /usr/share/games/doom/hexdd.wad` and needs that file. Not tried: none of the user's copies has
  HEXDD.WAD.

## Which copies work

game-data-packager wants HEXEN.WAD 1.1 (20083672 bytes, md5 abb033ca...453f); DSDA-Doom 0.30 plays nothing
else.

- **GOG**, `setup_hexen_beyond_heretic_1.1_(42801).exe` (148062304 bytes): HEXEN.WAD 1.1, the CD's data track
  as `game.gog`, the CD music as Ogg Vorbis, no Deathkings. gdp 89 didn't know the installer; our patch
  0010 unpacks it with innoextract, as gdp does for GOG's Heretic. An installed GOG folder works without
  the patch.
- **CD, Steam** (`common/Hexen`) and any other 1.1 copy: as gdp knows them.
- **1.0** (20128392 bytes, the floppy and early CD release): gdp refuses it and DSDA-Doom says "The Hexen
  v1.0 IWAD is not supported". Raven's 1.0-to-1.1 update is an RTPatch delta for DOS; we don't apply it
  yet.
- **Macintosh CD** (1996, `Hexen.WAD` 21078584 bytes, md5 b68140a7...93be, sha256 06baa5ee...5913):
  accepted since gdp patch 0011 as `hexen.wad?mac` (a 1.1 copy). It is the 1.1 game (the same maps,
  sounds, music lumps and graphics; 11 lumps differ, CREDIT, HELP1-2 and LIFEGMM1-8, and it adds 291
  lumps of its own: fonts, status bar pieces, menu art), but it lacks DMXGUS and GENMIDI, the DOS sound
  driver's instrument banks. DSDA-Doom loaded GENMIDI for its OPL player at start whatever
  `snd_midiplayer` said and stopped with `W_GetNumForName: GENMIDI not found` (exit 255). Our
  DSDA-Doom patch 0007 makes a missing GENMIDI mean "no OPL player" (`LoadInstrumentTable()` checks
  `W_LumpNameExists`; `I_OPL_InitMusic()` already handles the failure): with this WAD the OPL music
  player is unavailable, the other players are not. Our default is fluidsynth, which takes the same
  D_ music lumps through its soundfont, so the Mac copy plays with music there (the music itself not
  judged by ear). Title and demo1 on the PS3 (2026-10-27): picture right.
  Both changes are upstream candidates (DSDA-Doom, Debian's gdp).

## Engine options

DSDA-Doom is the one Walnut ships and patches; the research's pick for vanilla exactness is Chocolate
Doom (a draft here). The options:

- Chocolate Doom (the pick: vanilla-exact; gamedb:chocolate-doom)
- Crispy Doom, DSDA-Doom, Doomsday, Russian Doom

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Alternatives and popularity": Heretic/Hexen (1994/95): Chocolate Doom, vanilla-exact: the pick.
