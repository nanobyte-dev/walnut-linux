# QuakeSpasm: technical notes

Debian's `quakespasm` 0.96.3, unpatched, with the Quake shareware data. It plays the shareware's demo1
timedemo at 91.0 steady fps with almost no stalling (gamedb:batch/2026-10-18-mesa-407-switches), and its
played session of 2026-10-26 (the GLSL launcher) had a 70 fps median with a 1% low of 42 and
no audible dropouts: one of the games that meet the 60 fps target with room to spare.

## Packages, data and settings

- `quakespasm` from Debian, nothing of ours in the binary. Two menu entries of ours
  ([desktop/launchers](desktop/launchers/)) replace Debian's (diverted): "QuakeSpasm (GLSL)" and a
  `-noglsl` one, both `-basedir /usr/share/games/quake -nopackedpixels -width 1920 -height 1080
  -fullscreen`. Don't install the `quake` launcher package: it pulls GTK 4.
- **`-nopackedpixels`**: 0.96.3's packed-pixel lightmaps give wrong colours and dlight wraparound on
  big-endian (quakespasm issue #114). Upstream master disables the path on big-endian; nv30 has no
  10-bit format anyway.
- **The data** (licensed, not in the public suite): `quake-shareware`
  (`/usr/share/games/quake/id1/pak0.pak`, Quake 1.06 shareware, 18 MB, demo1-3), built once on the PC by
  game-data-packager 89 (`make game-data`, the Debian games builder with `lhasa`; the shareware archive
  downloaded from id's mirrors and kept in `build/downloads/game-data`) as `quake-shareware_89_all.deb`,
  Section `non-free/games`, and published once to our licensed suite, since game-data-packager's output
  isn't reproducible: the version 89 files the LAN repository published are in the retirement backup's
  `ps3-licensed-debs/`, and `make publish DEBS="<those files>"` before the next `make game-data` keeps
  them the same. `ps3-games-licensed` pulls it in. A registered Quake needs the player's own `id1/pak0.pak` and `pak1.pak` (the data guide on the
  game's page).

## How it performs

- Bench (gamedb:batch/2026-10-18-mesa-407-switches, the kicker on, same boot): steady 91.0, 1% low 48.6,
  2.5 s of gameplay stalls (10 frames over 100 ms), longest 393 ms, 44 MB free at the lowest, 49 MB of
  VRAM, 5 underruns. Mesa's `NV30_TRANSLATE_LUT=1` changed nothing in fps (90.9/91.0 against 91.0/90.8)
  but saved about 3% of the CPU.
- Memory: 58 MB Rss, its 256 MB heap mapping 24.8 MB touched. The cache inside the heap never needs
  flushing at this size, so sounds and models stay loaded; nothing to cut
  ([MEMORY.md](../../../docs/MEMORY.md#findings-in-the-games-code)).
- How it got there: the first runs on 2026-10-17 gave 59.2 fps without GLSL, 64.8 with, 70.3 with the
  gamma pass, colours right; Mesa ps3.262 took it from 70.5 to 78.6 (+11%, gamedb:batch/2026-10-17-mesa-262).

The recipe ([benchmark/recipe.sh](benchmark/recipe.sh)) can't use `+timedemo` on the command line: the
shareware's own quake.rc runs `startdemos demo1 demo2 demo3` after the command line and the attract
loop replaces the timedemo, and the shareware refuses `-game` directories. So a `quake.rc` of ours in
`~/.quakespasm/id1` loops the timedemo by frame count (`bench` starts it and waits 1100 host frames;
demo1's timedemo is 969 frames), since QuakeSpasm has no hook at a demo's end: about 5 warm-up passes
of 13 s, then about 9 measured. A line starting with `-1 frames` means a timedemo started before the
last one ended. Base keeps GLSL on with gamma at 1.0, so the full-screen gamma pass doesn't run;
variants `noglsl` and `glslgamma`.

## Known problems

- The interface text is tiny at 1080p (8 px; `-15` on the session's Display score).
- A DualShock 3 plugged in by USB but not yet activated (PS button) made the menu cursor run up by
  itself: hid-sony left the idle sticks at 0 (full up and left). Kernel local/0050 starts the sticks
  centred; its PS3 check is pending (docs/TESTS.md, batch kernel-0050).
- `QUAKE ERROR: You must have the registered version to use modified games` (one run, 2026-10-16) was
  a launch-option error: the shareware refuses `-game`.

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-16, [game-candidates.md](../../research/game-candidates.md), "Candidates": ezQuake 3.6.8 (Debian `ezquake`, 6 MB) was the other Quake engine looked at: big-endian fixes upstream, a modern GL 4.x renderer (tier X) and a classic path (tier C, untried); left out because QuakeSpasm covers Quake (gamedb:ezquake).
- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": a strong candidate: Quake 1, GL 1.x, a tiny footprint, PPC-friendly.
