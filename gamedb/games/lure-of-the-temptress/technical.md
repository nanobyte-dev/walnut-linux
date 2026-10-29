# Lure of the Temptress: technical notes

## Status

Untested: nobody has played a session of it on Walnut yet. Runs on ScummVM ([the engine](../../engines/scummvm/technical.md), Debian's package).

Smoke run 2026-10-20 (`ps3-bench-20261020T093324Z-smoke-scummvm-lure`, ScummVM 2026.1.0): "Running Lure
of the Temptress (VGA/DOS/English)", the sound running; no screenshots in that run. The package holds
the English, German, French, Italian and Spanish versions, each in its own folder; the launcher starts
the English one (Debian's `lure` wrapper picks by locale).

## How it is set up

- **Data**: Debian's `lure-of-the-temptress`, installed with ScummVM by Walnut Games, in `/usr/share/scummvm/lure-of-the-temptress/en`.
- **Launcher** ([desktop/launchers](desktop/launchers/)): `scummvm -c ~/.config/scummvm/walnut/lure.ini
  -i /usr/share/ps3-games/scummvm/lure.ini -p /usr/share/scummvm/lure-of-the-temptress/en lure` through ps3-game, shown once the data
  is installed (`X-Walnut-Requires=`); the package's own menu entry is diverted
  ([the engine's notes](../../engines/scummvm/technical.md)).
- **Settings**: the initial config [lure.ini](desktop/files/usr/share/ps3-games/scummvm/lure.ini); the
  player's settings then live in `~/.config/scummvm/walnut/lure.ini`, the saves in
  `~/.local/share/scummvm/saves/lure.*` (set aside by a played session: [rating/test.toml](rating/test.toml)).
