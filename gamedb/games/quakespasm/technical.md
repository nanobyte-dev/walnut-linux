# QuakeSpasm: technical notes

Debian's `quakespasm` 0.96.3, unpatched, with the Quake shareware data. It plays the shareware's demo1
timedemo at 91.0 steady fps with almost no stalling (gamedb:batch/2026-10-18-mesa-407-switches), and its
played session of 2026-10-26 (the GLSL launcher) had a 70 fps median with a 1% low of 42 and
no audible dropouts: one of the games that meet the 60 fps target with room to spare.

## Packages, data and settings

- `quakespasm` from Debian, nothing of ours in the binary. Two menu entries of ours
  ([desktop/launchers](desktop/launchers/)) replace Debian's (diverted): "QuakeSpasm (GLSL)" and a
  `-noglsl` one, both `-basedir /usr/share/games/quake -nopackedpixels -width 1920 -height 1080
  -fullscreen`; a third, Quake (2021 re-release), for the re-release's own folder (below). Don't install
  the `quake` launcher package: it pulls GTK 4.
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

## The 2021 re-release

QuakeSpasm 0.94 and later play Nightdive's 2021 re-release (its Quakespasm.txt, "Quake '2021
re-release'"), and Debian's 0.96.3 does it on the PS3 unchanged: started with `-basedir` on a folder
holding the re-release's `id1`, it loads the strings of the new QuakeC ("Loaded 1317 strings from
'localization/loc_english.txt'"), runs the demo loop (demo1 in the Necropolis) and `+map e1m1`, with
the right picture (xwd fullscreen, looked at) and `id1/music/track06.ogg` open for e1m1.

- **What GOG's installer holds.** `setup_quake_enhanced_1.0_(60828).exe` (607645056 bytes, sha256
  da3b4acc...4cff, "Quake Enhanced", GOG game id 1739637082): `id1/pak0.pak` (180815252 bytes, 1121
  entries: 70 BSPs, 42 `.lit` coloured-lighting files, md5 models and animations, PNG menu art, the
  Kex engine's bots and controller rumble files, a new `progs.dat`); `id1/music/track02-11.ogg`, byte
  for byte the Ogg rip game-data-packager already knows as `quake_campaign_soundtrack.zip`;
  `QuakeEX.kpf` (a zip of the Kex engine's shaders and text: QuakeSpasm reads only
  `localization/loc_english.txt` from it, from the base directory or the kpf there); the add-ons'
  re-release paks (`hipnotic` 76 MiB with music, `rogue` 42 MiB with music, `dopa` 39 MiB, `mg1`
  474 MiB, `ctf` 20 MiB); the Kex engine's executable, DLLs and movies. No `pak1.pak`: the
  re-release's pak0 is the whole game, so it can't stand in for quake-registered's paks.
- **Memory.** The same as the original game: VmHWM after 40-60 s on e1m1 was 71.5 and 72.3 MB against
  71.1 MB for quake-registered's paks, and 56.4 against 53.9 MB in another pair (game mode's reclaim
  moves the figure by more than the edition does); 25-28 MB free at the end of each run.
- **Packaging.** game-data-packager 0012 adds `quake-rerelease` (edition `remaster`) in
  `/usr/share/games/quake-rerelease`: the pak, the strings taken out of `QuakeEX.kpf` (77 KB, not the
  13.5 MB kpf) and the music as optional files (gdp makes quake-music only as an expansion of
  quake-registered, so the re-release carries its own copy). GOG's installer is unpacked with
  innoextract for it. Our launcher `quakespasm-rerelease.desktop` (Quake (2021 re-release), GLSL,
  `NoDisplay` with `X-Walnut-Requires` on the pak) starts it with `-basedir
  /usr/share/games/quake-rerelease`. The import on the PS3 found two gdp bugs, fixed as 0013 and 0014:
  `-p` given before the game name was reset by the quake subparser's shortcut options, so with
  quake-registered installed gdp also built quake-music and quake-dimension-of-the-machine from the
  installer (walnut-data refuses a deb it did not ask for); and a GOG `setup_*.exe` gave every
  package an icon converted from the installer's `.ico` (python3-pil), which the manifest can't know.
- **Not done.** The re-release's add-ons are not packaged: its hipnotic, rogue and dopa paks are
  builds gdp doesn't know, and gdp's `quake-dimension-of-the-machine` (the same `mg1/pak0.pak`) is an
  expansion of quake-registered installed into `/usr/share/games/quake/mg1`, where the original
  game's QuakeC can't run it. Steam's re-release and other GOG builds are unchecked (other checksums
  would need alternatives).

## Known problems

- Both editions share `~/.quakespasm/id1` (QuakeSpasm's user directory doesn't depend on
  `-basedir`): a save made in the re-release is offered in the original game and the other way round,
  and the two `progs.dat` differ. Not tried yet.

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
