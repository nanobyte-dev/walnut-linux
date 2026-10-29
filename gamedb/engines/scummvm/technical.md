# ScummVM: technical notes

Debian's `scummvm`, unpatched (2026.4.0+dfsg-1 on the PS3 in the smoke runs of 2026-10-29; 2026.1.0 before).
The licence is GPL-3.0-or-later: the sources' headers since ScummVM 2.7 (Debian's copyright file still
says GPL-2+ for most files). Each game it runs is a GameDB entry of its own (the user, 2026-10-30: "we
don't rate engines, we rate games"); the old `scummvm` bundle is a draft that keeps ScummVM's own
launcher and the smoke-test recipe.

## How a game starts

Every game has its own menu entries in its bundle's `desktop/launchers/`, and nothing goes through
ScummVM's launcher or its Add Game:

    ps3-game sh -c "mkdir -p ~/.config/scummvm/walnut && exec scummvm -c ~/.config/scummvm/walnut/<target>.ini
        -i /usr/share/ps3-games/scummvm/<target>.ini -p <the data folder> <target>"

- **Through ps3-game**: game mode from the first frame. Before this, the player started ScummVM's
  launcher and pointed Add Game at the files; game mode froze the desktop, and Add Game's file dialog
  (the desktop's own, `gui_browser_native`) opened inside the frozen desktop and hung (2026-10-29).
- **A config file per game** (`-c`), so a played session sets aside only that game's settings and saves
  (its `rating/test.toml`), never the player's other games. `-i` is the game's initial config, shipped
  by ps3-games-integration from the bundle's `desktop/files/usr/share/ps3-games/scummvm/<target>.ini`:
  ScummVM reads it only while the `-c` file doesn't exist, then writes the player's settings there.
- **The console defaults** in it: `fullscreen=true`, `stretch_mode=fit` (the 4:3 picture between bars at
  1920x1080), `gui_browser_native=false` (ScummVM's own file browser wherever one opens: the global
  menu's save and load don't need one, its options' paths do), `confirm_exit=false`,
  `gui_return_to_launcher_at_exit=false` (quitting the game quits ScummVM), subtitles on, `music_driver=adlib`
  and `output_rate=22050` (the AdLib emulation at half rate: cheap on the PPE, as in the smoke runs; there
  is no MIDI device). The pad is the mouse by ScummVM's own default (`joystick_num` 0: the stick moves the
  pointer, the face buttons click); the smoke runs' logs say "Using game controller: PS3 Controller".
- **A target per game** in that file (`[tentacle]` with `gameid` and `engineid`): ScummVM keeps the
  settings of a target it finds in its config, but not of one it makes from a bare game id, and the bare
  id `atlantis` is ambiguous ("Unrecognized game 'atlantis'": another engine has a game of that id;
  the smoke run needed `--auto-detect`). `-p` gives the folder for this run only.
- **One entry per edition and language** for the player's own games: game-data-packager makes one
  package per edition (`dayofthetentacle-floppy-es-data`), each in its own folder
  (`/usr/share/games/dayofthetentacle-floppy-es/`). Each entry has `NoDisplay=true` and
  `X-Walnut-Requires=` its folder's main file, so ps3-game-menu shows the ones whose data is installed,
  all named after the game. The free games have one entry each, with their Debian package's folder
  (`/usr/share/scummvm/<game>/`); their own menu entries are diverted.

ScummVM's own launcher stays in the menu for games Walnut has no entry for: the `scummvm` draft bundle's
`scummvm.desktop` replaces Debian's `org.scummvm.scummvm.desktop` (diverted), runs with
`PS3_GAME_MODE=off` (ps3-game-detect leaves it alone) and `X-Walnut-GameMode=false` (ps3-game-menu
doesn't wrap it in ps3-game), and its initial config sets `gui_browser_native=false` too. A game
started from that launcher gets no game mode.

## The data

- The player's own games: game-data-packager's ScummVM packages ([data_setup] in each bundle), made by
  Walnut Games' Add game files or `walnut-data import`. Our game-data-packager adds what Debian's lacks
  for the player's copies: the Spanish VGA floppy release of The Secret of Monkey Island (patch 0016),
  the Spanish EGA release of Loom (0017), and `--no-launchers` for ScummVM games (0018: no `.desktop` link
  into game-data-packager-runtime, no icon; the import would refuse the link). Day of the Tentacle's CD
  releases install `monster.sou` or its FLAC repack `monster.sof`, whichever the copy has: the import's
  manifest lists both as optional files (tools/gamedb/gdp-manifest.py). Loom's VGA talkie (`loom-en-data`)
  is left out for now: its music is a ripped CD track, which the manifest can't describe yet (todo).
- The free games: Debian's `beneath-a-steel-sky`, `flight-of-the-amazon-queen`, `lure-of-the-temptress`,
  `drascula` with `drascula-music`, installed with ScummVM by Walnut Games.

## Status

Smoke runs on the PS3 (recipe [games/scummvm/benchmark/recipe.sh](../../games/scummvm/benchmark/recipe.sh),
variants per game; the player's copies in `~/Games/scummvm/<GAME>`, never in the repository; their checksums
are in each game's technical.md):

| Game | Copy | Run | Result |
|---|---|---|---|
| [Day of the Tentacle](../../games/day-of-the-tentacle/technical.md) | Spanish floppy | 2026-10-28 `dott` | into the hotel lobby, picture right, sound running |
| [The Secret of Monkey Island](../../games/the-secret-of-monkey-island/technical.md) | Spanish VGA floppy | 2026-10-29 `monkey` | Guybrush on the dock, right |
| [Monkey Island 2](../../games/monkey-island-2/technical.md) | Spanish floppy | 2026-10-28 `monkey2` | stops at the game's copy-protection screen, drawn right |
| [Fate of Atlantis](../../games/indiana-jones-and-the-fate-of-atlantis/technical.md) | Spanish floppy | 2026-10-29 `atlantis` | Indy in the store room, right |
| [Loom](../../games/loom/technical.md) | Spanish EGA | 2026-10-29 `loom` | the difficulty choice, right |
| Full Throttle | Spanish CD | 2026-10-29 `ft` | the alley after the SMUSH intro, right (no entry yet) |
| The Dig | Spanish CD | 2026-10-29 `dig` | at the shuttle after the intro, right (no entry yet) |
| [Beneath a Steel Sky](../../games/beneath-a-steel-sky/technical.md) | Debian's | 2026-10-19 `base` | the intro with sound; Escape didn't reach the window then |
| [Flight of the Amazon Queen](../../games/flight-of-the-amazon-queen/technical.md) | Debian's | 2026-10-20 `queen` | "Running Flight of the Amazon Queen (CD/DOS/English)" |
| [Lure of the Temptress](../../games/lure-of-the-temptress/technical.md) | Debian's | 2026-10-20 `lure` | "Running Lure of the Temptress (VGA/DOS/English)" |
| [Drascula](../../games/drascula/technical.md) | Debian's | 2026-10-20 `drascula` | "Couldn't identify game 'drascula'" (ScummVM 2026.1.0); on the PC with `drascula` 1.0+ds4-4 it runs |

On the PC (2026-10-30, ScummVM 2026.1.0 in a Debian container, SDL's dummy video), every menu entry's
command started its game: the four free ones say "Running ...", and the five from the player's copies (the
packages our gdp made from them) wrote their target's detected edition into the game's config (`extra`,
`lang_Spanish`).

All the PS3 runs at 1920x1080 fullscreen with the 4:3 picture between bars and the colours right (ScummVM's default
graphics, the 8-bit picture through our big-endian SDL), about 85-90 MB peak RSS, the AdLib music running
with no audible dropouts. The Gallium HUD's numbers mean nothing here (6-9 fps, "stalls"): ScummVM
presents a frame only when the picture changes. Its ALSA sequencer errors (`/dev/snd/seq`) are the MIDI
port probe, harmless with the AdLib emulator. Full Throttle (282 MB) and The Dig (245 MB) were removed
from the console after their runs for the disk; they have no entry yet.
