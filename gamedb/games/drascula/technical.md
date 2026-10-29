# Drascula: The Vampire Strikes Back: technical notes

## Status

Untested: nobody has played a session of it on Walnut yet. Runs on ScummVM ([the engine](../../engines/scummvm/technical.md), Debian's package).

Smoke run 2026-10-20 (`ps3-bench-20261020T093104Z-smoke-scummvm-drascula`, ScummVM 2026.1.0, the data
package of then): "Couldn't identify game 'drascula' for the engine 'drascula'", so the game didn't start.
On the PC (2026-10-30, a Debian container, ScummVM 2026.1.0 with `drascula` 1.0+ds4-4) the menu entry's
command says "Running Drascula: The Vampire Strikes Back (DOS/English)": to be seen again on the PS3.
The music is
`drascula-music`'s Ogg Vorbis tracks in `/usr/share/scummvm/drascula/audio/`. The other languages are
packages of their own (`drascula-spanish`, ...).

## How it is set up

- **Data**: Debian's `drascula`, `drascula-music`, installed with ScummVM by Walnut Games, in `/usr/share/scummvm/drascula/en`.
- **Launcher** ([desktop/launchers](desktop/launchers/)): `scummvm -c ~/.config/scummvm/walnut/drascula.ini
  -i /usr/share/ps3-games/scummvm/drascula.ini -p /usr/share/scummvm/drascula/en drascula` through ps3-game, shown once the data
  is installed (`X-Walnut-Requires=`); the package's own menu entry is diverted
  ([the engine's notes](../../engines/scummvm/technical.md)).
- **Settings**: the initial config [drascula.ini](desktop/files/usr/share/ps3-games/scummvm/drascula.ini); the
  player's settings then live in `~/.config/scummvm/walnut/drascula.ini`, the saves in
  `~/.local/share/scummvm/saves/drascula.*` (set aside by a played session: [rating/test.toml](rating/test.toml)).
