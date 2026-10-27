# Heretic: technical notes

## Status

Untested on the PS3: published so that Walnut Games can set up its data; nobody has played it on Walnut
yet. Its launcher starts DSDA-Doom 0.30.0 (Debian's package with Walnut's patches, gamedb:dsda-doom),
which plays Heretic and Hexen too (started as `dsda-heretic`, Debian's link to it); it plays Freedoom 2 at about 56 fps
on its OpenGL renderer.

- **Data**: game-data-packager's `heretic-wad` (build.toml `[data_setup]`), made from the player's own copy, in
  `/usr/share/games/doom/heretic.wad`.
- **Launcher** ([desktop/launchers](desktop/launchers/)), `NoDisplay=true` with `X-Walnut-Requires=` the
  WAD (shown once the menu rule for it exists, PLAN 5.10):
  `dsda-heretic -iwad /usr/share/games/doom/heretic.wad -vidmode gl -width 1920 -height 1080 -fullscreen`.

## Engine options

DSDA-Doom is the one Walnut ships and patches; the research's pick for vanilla exactness is Chocolate
Doom (a draft here). The options:

- Chocolate Doom (the pick: vanilla-exact; gamedb:chocolate-doom)
- Crispy Doom (gamedb:crispy-doom)
- DSDA-Doom (gamedb:dsda-doom)
- Doomsday (gamedb:doomsday)
- Russian Doom

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Alternatives and popularity": Heretic/Hexen (1994/95): Chocolate Doom (2437 stars), vanilla-exact: the pick; alternatives Crispy Doom, DSDA-Doom, Doomsday, Russian Doom.
