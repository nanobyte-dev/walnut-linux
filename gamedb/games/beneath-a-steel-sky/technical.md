# Beneath a Steel Sky: technical notes

## Status

Untested: nobody has played a session of it on Walnut yet. Runs on ScummVM ([the engine](../../engines/scummvm/technical.md), Debian's package).

First run 2026-10-19 (gamedb:batch/2026-10-19-ssdroot3-smoke-new, ScummVM 2026.1.0): the intro, the
sound running (20 underruns; the mic heard it 12 dB over the room), 85 MB peak RSS. Escape never reached
the window then (the recipe's keys missed SDL's window names until 2026-10-28), so the run stayed in the
intro. Not run since: its package wasn't installed in the 2026-10-29 runs. The package is the CD version
with speech, all its languages in `sky.dsk`; the launcher plays the English one.

## How it is set up

- **Data**: Debian's `beneath-a-steel-sky`, installed with ScummVM by Walnut Games, in `/usr/share/scummvm/beneath-a-steel-sky`.
- **Launcher** ([desktop/launchers](desktop/launchers/)): `scummvm -c ~/.config/scummvm/walnut/sky.ini
  -i /usr/share/ps3-games/scummvm/sky.ini -p /usr/share/scummvm/beneath-a-steel-sky sky` through ps3-game, shown once the data
  is installed (`X-Walnut-Requires=`); the package's own menu entry is diverted
  ([the engine's notes](../../engines/scummvm/technical.md)).
- **Settings**: the initial config [sky.ini](desktop/files/usr/share/ps3-games/scummvm/sky.ini); the
  player's settings then live in `~/.config/scummvm/walnut/sky.ini`, the saves in
  `~/.local/share/scummvm/saves/SKY-VM.*` (set aside by a played session: [rating/test.toml](rating/test.toml)).
