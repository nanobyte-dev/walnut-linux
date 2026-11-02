# Doom: technical notes

## Status

Played once on the PS3 (2026-11-02, 936 s of play from The Ultimate Doom's DOOM.WAD: median 59 fps, 1% low 40,
no crash, no audible dropouts, nothing wrong in the picture; Platinum 95, provisional under an hour of play). Its launchers start DSDA-Doom 0.30.0 (Debian's package with Walnut's patches, gamedb:dsda-doom), which also plays
Freedoom 2 at about 56 fps on its OpenGL renderer.

- **Data**: game-data-packager's `doom-wad` (build.toml `[data_setup]`), made from the
  player's own copy; the WADs go to `/usr/share/games/doom/`, where Freedoom's are.
- **Launchers** ([desktop/launchers](desktop/launchers/)), one per IWAD, `NoDisplay=true` with
  `X-Walnut-Requires=` the WAD (shown once the menu rule for it exists, PLAN 5.10):
  - `dsda-doom -iwad /usr/share/games/doom/doom.wad -vidmode gl -width 1920 -height 1080 -fullscreen`
