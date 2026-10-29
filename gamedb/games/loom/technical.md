# Loom: technical notes

## Status

Untested: published so that Walnut Games can set up its data; nobody has played a session of it on
Walnut yet. Runs on ScummVM ([the engine](../../engines/scummvm/technical.md), Debian's package).

Smoke run 2026-10-29 (`ps3-bench-20261029T051548Z-smoke-scummvm-loom`, variant `loom`): the difficulty
choice (Estándar, Práctica, Experto), drawn right.

The player's copy is the Spanish EGA release (ScummVM: "loom", EGA, ES; `00.LFL` md5
`2a208ffb...a6e1`). Debian's game-data-packager doesn't know it; ours does (`loom-ega-es-data`, patch
0017: 74 room files, 28 of them the other EGA releases' own). The VGA talkie (`loom-en-data`) can't be
imported yet: game-data-packager rips its music from the CD's one audio track, which the import's
manifest doesn't describe ([the engine's todo](../../engines/scummvm/todo.toml)).

## How it is set up

- **Data**: game-data-packager's packages for this game (build.toml `[data_setup]`, one per edition and
  language), made from the player's own copy by Walnut Games' Add game files or `walnut-data import loom`;
  each goes to `/usr/share/games/<package without -data>/`.
- **Launchers** ([desktop/launchers](desktop/launchers/)): one per package, `NoDisplay=true` with
  `X-Walnut-Requires=` its main file, so the menu shows the editions that are installed. Each starts
  `scummvm -c ~/.config/scummvm/walnut/loom.ini -i /usr/share/ps3-games/scummvm/loom.ini -p <its folder> loom`
  through ps3-game: the game directly, never ScummVM's launcher ([the engine's notes](../../engines/scummvm/technical.md)).
- **Settings**: the initial config [loom.ini](desktop/files/usr/share/ps3-games/scummvm/loom.ini) (fullscreen,
  the picture's shape kept, ScummVM's own file browser, AdLib music, the `loom` target); the player's
  settings then live in `~/.config/scummvm/walnut/loom.ini`, the saves in
  `~/.local/share/scummvm/saves/loom.s*`, which a played session sets aside ([rating/test.toml](rating/test.toml)).
