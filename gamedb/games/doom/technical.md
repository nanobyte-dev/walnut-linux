# Doom: technical notes

## Status

Untested on the PS3: published so that Walnut Games can set up its data; nobody has played it on Walnut
yet. Its launchers start DSDA-Doom 0.30.0 (Debian's package with Walnut's patches, gamedb:dsda-doom), which plays
Freedoom 2 at about 56 fps on its OpenGL renderer.

- **Data**: game-data-packager's `doom-wad` (build.toml `[data_setup]`), made from the
  player's own copy; the WADs go to `/usr/share/games/doom/`, where Freedoom's are.
- **Launchers** ([desktop/launchers](desktop/launchers/)), one per IWAD, `NoDisplay=true` with
  `X-Walnut-Requires=` the WAD (shown once the menu rule for it exists, PLAN 5.10):
  - `dsda-doom -iwad /usr/share/games/doom/doom.wad -vidmode gl -width 1920 -height 1080 -fullscreen`
