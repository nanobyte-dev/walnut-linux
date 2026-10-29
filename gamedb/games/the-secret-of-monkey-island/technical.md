# The Secret of Monkey Island: technical notes

## Status

Untested: published so that Walnut Games can set up its data; nobody has played a session of it on
Walnut yet. Runs on ScummVM ([the engine](../../engines/scummvm/technical.md), Debian's package).

Smoke run 2026-10-29 (`ps3-bench-20261029T023324Z-smoke-scummvm-monkey`, variant `monkey`): the "Parte
Uno" title, then Guybrush on the dock at night with the verbs, colours right.

The player's copy is the Spanish VGA floppy release (eight disks; ScummVM: "monkey", VGA, ES; `000.LFL`
sha256 `4b24ef06...dfd9fc`). Debian's game-data-packager doesn't know it; ours does
(`secret-of-monkey-island-floppy-es-data`, patch 0016, from the copy's sizes and checksums; its
901-904.LFL are the German floppy's).

## How it is set up

- **Data**: game-data-packager's packages for this game (build.toml `[data_setup]`, one per edition and
  language), made from the player's own copy by Walnut Games' Add game files or `walnut-data import the-secret-of-monkey-island`;
  each goes to `/usr/share/games/<package without -data>/`.
- **Launchers** ([desktop/launchers](desktop/launchers/)): one per package, `NoDisplay=true` with
  `X-Walnut-Requires=` its main file, so the menu shows the editions that are installed. Each starts
  `scummvm -c ~/.config/scummvm/walnut/monkey.ini -i /usr/share/ps3-games/scummvm/monkey.ini -p <its folder> monkey`
  through ps3-game: the game directly, never ScummVM's launcher ([the engine's notes](../../engines/scummvm/technical.md)).
- **Settings**: the initial config [monkey.ini](desktop/files/usr/share/ps3-games/scummvm/monkey.ini) (fullscreen,
  the picture's shape kept, ScummVM's own file browser, AdLib music, the `monkey` target); the player's
  settings then live in `~/.config/scummvm/walnut/monkey.ini`, the saves in
  `~/.local/share/scummvm/saves/monkey.s*`, which a played session sets aside ([rating/test.toml](rating/test.toml)).
