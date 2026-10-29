# Flight of the Amazon Queen: technical notes

## Status

Untested: nobody has played a session of it on Walnut yet. Runs on ScummVM ([the engine](../../engines/scummvm/technical.md), Debian's package).

Smoke run 2026-10-20 (`ps3-bench-20261020T092844Z-smoke-scummvm-queen`, ScummVM 2026.1.0): "Running
Flight of the Amazon Queen (CD/DOS/English)", the sound running; no screenshots in that run. The package is
the CD version with speech (`queen.1c`).

## How it is set up

- **Data**: Debian's `flight-of-the-amazon-queen`, installed with ScummVM by Walnut Games, in `/usr/share/scummvm/flight-of-the-amazon-queen`.
- **Launcher** ([desktop/launchers](desktop/launchers/)): `scummvm -c ~/.config/scummvm/walnut/queen.ini
  -i /usr/share/ps3-games/scummvm/queen.ini -p /usr/share/scummvm/flight-of-the-amazon-queen queen` through ps3-game, shown once the data
  is installed (`X-Walnut-Requires=`); the package's own menu entry is diverted
  ([the engine's notes](../../engines/scummvm/technical.md)).
- **Settings**: the initial config [queen.ini](desktop/files/usr/share/ps3-games/scummvm/queen.ini); the
  player's settings then live in `~/.config/scummvm/walnut/queen.ini`, the saves in
  `~/.local/share/scummvm/saves/queen.s*` (set aside by a played session: [rating/test.toml](rating/test.toml)).
