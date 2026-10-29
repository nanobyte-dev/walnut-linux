# Indiana Jones and the Fate of Atlantis: technical notes

## Status

Untested: published so that Walnut Games can set up its data; nobody has played a session of it on
Walnut yet. Runs on ScummVM ([the engine](../../engines/scummvm/technical.md), Debian's package).

Smoke run 2026-10-29 (`ps3-bench-20261029T051807Z-smoke-scummvm-atlantis`, variant `atlantis`): Indy in
the college's store room with his first line, colours right. The run before it stopped at once with
"Unrecognized game 'atlantis'": the bare id matches another engine's game too, so the recipe used
`--auto-detect`; the menu entries name the target in their own config instead.

The player's copy is the Spanish floppy release (`ATLANTIS.000` sha256 `ee30779f...a9b8d1`),
`fate-of-atlantis-floppy-es-data` in Debian's game-data-packager 89 already.

## How it is set up

- **Data**: game-data-packager's packages for this game (build.toml `[data_setup]`, one per edition and
  language), made from the player's own copy by Walnut Games' Add game files or `walnut-data import indiana-jones-and-the-fate-of-atlantis`;
  each goes to `/usr/share/games/<package without -data>/`.
- **Launchers** ([desktop/launchers](desktop/launchers/)): one per package, `NoDisplay=true` with
  `X-Walnut-Requires=` its main file, so the menu shows the editions that are installed. Each starts
  `scummvm -c ~/.config/scummvm/walnut/atlantis.ini -i /usr/share/ps3-games/scummvm/atlantis.ini -p <its folder> atlantis`
  through ps3-game: the game directly, never ScummVM's launcher ([the engine's notes](../../engines/scummvm/technical.md)).
- **Settings**: the initial config [atlantis.ini](desktop/files/usr/share/ps3-games/scummvm/atlantis.ini) (fullscreen,
  the picture's shape kept, ScummVM's own file browser, AdLib music, the `atlantis` target); the player's
  settings then live in `~/.config/scummvm/walnut/atlantis.ini`, the saves in
  `~/.local/share/scummvm/saves/atlantis.s*`, which a played session sets aside ([rating/test.toml](rating/test.toml)).
