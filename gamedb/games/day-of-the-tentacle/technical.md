# Day of the Tentacle: technical notes

## Status

Untested: published so that Walnut Games can set up its data; nobody has played a session of it on
Walnut yet. Runs on ScummVM ([the engine](../../engines/scummvm/technical.md), Debian's package).

Smoke run 2026-10-28 (`ps3-bench-20261028T224303Z-smoke-scummvm-dott`, ScummVM 2026.4.0, recipe
[scummvm](../scummvm/benchmark/recipe.sh) variant `dott`, from `~/Games/scummvm/DOTT`): Escape twice skipped
the intro, then Bernard in the hotel lobby with the verbs and the inventory, colours right, fullscreen with
the 4:3 picture between bars; 90 MB peak RSS, the sound running with no audible dropouts.

The player's copy is the Spanish **floppy** release (ScummVM: "tentacle", Floppy, ES; game-data-packager:
`dayofthetentacle-floppy-es-data`), not the CD talkie: its `MONSTER.SOU` (3.8 MB) holds only the intro
song. `TENTACLE.000` sha256 `dd673f21...0d600`, `TENTACLE.001` `865ab1a6...97dbb`, `MONSTER.SOU`
`1819b516...aee5b`. Debian's game-data-packager 89 already knows it.

## How it is set up

- **Data**: game-data-packager's packages for this game (build.toml `[data_setup]`, one per edition and
  language), made from the player's own copy by Walnut Games' Add game files or `walnut-data import day-of-the-tentacle`;
  each goes to `/usr/share/games/<package without -data>/`.
- **Launchers** ([desktop/launchers](desktop/launchers/)): one per package, `NoDisplay=true` with
  `X-Walnut-Requires=` its main file, so the menu shows the editions that are installed. Each starts
  `scummvm -c ~/.config/scummvm/walnut/tentacle.ini -i /usr/share/ps3-games/scummvm/tentacle.ini -p <its folder> tentacle`
  through ps3-game: the game directly, never ScummVM's launcher ([the engine's notes](../../engines/scummvm/technical.md)).
- **Settings**: the initial config [tentacle.ini](desktop/files/usr/share/ps3-games/scummvm/tentacle.ini) (fullscreen,
  the picture's shape kept, ScummVM's own file browser, AdLib music, the `tentacle` target); the player's
  settings then live in `~/.config/scummvm/walnut/tentacle.ini`, the saves in
  `~/.local/share/scummvm/saves/tentacle.s*`, which a played session sets aside ([rating/test.toml](rating/test.toml)).
