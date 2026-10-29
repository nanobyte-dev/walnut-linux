# Monkey Island 2: LeChuck's Revenge: technical notes

## Status

Untested: published so that Walnut Games can set up its data; nobody has played a session of it on
Walnut yet. Runs on ScummVM ([the engine](../../engines/scummvm/technical.md), Debian's package).

Smoke run 2026-10-28 (`ps3-bench-20261028T224550Z-smoke-scummvm-monkey2`, variant `monkey2`): the game's
copy-protection screen (the voodoo recipe wheel), drawn right; ScummVM doesn't skip it for this release
(known issue), so the run went no further.

The player's copy is the Spanish floppy release (`MONKEY2.000` sha256 `bdca6e56...88ac5`, `MONKEY2.001`
`f5cb9fc5...8b3`), `monkey-island2-es-data` in Debian's game-data-packager 89 already.

## How it is set up

- **Data**: game-data-packager's packages for this game (build.toml `[data_setup]`, one per edition and
  language), made from the player's own copy by Walnut Games' Add game files or `walnut-data import monkey-island-2`;
  each goes to `/usr/share/games/<package without -data>/`.
- **Launchers** ([desktop/launchers](desktop/launchers/)): one per package, `NoDisplay=true` with
  `X-Walnut-Requires=` its main file, so the menu shows the editions that are installed. Each starts
  `scummvm -c ~/.config/scummvm/walnut/monkey2.ini -i /usr/share/ps3-games/scummvm/monkey2.ini -p <its folder> monkey2`
  through ps3-game: the game directly, never ScummVM's launcher ([the engine's notes](../../engines/scummvm/technical.md)).
- **Settings**: the initial config [monkey2.ini](desktop/files/usr/share/ps3-games/scummvm/monkey2.ini) (fullscreen,
  the picture's shape kept, ScummVM's own file browser, AdLib music, the `monkey2` target); the player's
  settings then live in `~/.config/scummvm/walnut/monkey2.ini`, the saves in
  `~/.local/share/scummvm/saves/monkey2.s*`, which a played session sets aside ([rating/test.toml](rating/test.toml)).
