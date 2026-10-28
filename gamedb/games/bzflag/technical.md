# BZFlag: technical notes

## Status

Debian's `bzflag-client`, `bzflag-data` and `bzflag-server` 2.4.30-4 from debian-ports, unchanged (about 20 MB
installed). It runs: smoke run on 2026-10-28 (kernel .874, Mesa ps3.924; runs
`ps3-bench-20261028T194448Z` and `T194805Z`): the client joins a bzfs on the console with three robot tanks,
fullscreen at 1920x1080 (`geom=1920x1080+0+0 fs=1`), the robots fight each other, our tank spawns on `i` and
drives. The Gallium HUD over the 90 s window: 59 fps steady, 1% low 30, one frame over 100 ms (243 ms, the
screenshot's capture), 136% CPU (client, robots and server together), 65 MB RSS, 48 MB of VRAM, 23 MB of
GART. Sound ran (`RUNNING`, 18 dropouts logged, 1 audible). Not checked yet: the sound by ear, the pad, a long
match.

The picture: world, tanks, pyramids, boxes, shots, radar, HUD and the console text are drawn; the sky and
ground are dark even at `-time 12:00:00`, and the PC reference (llvmpipe at GL 2.1, `make bench-ref
GAME=bzflag`) is just as dark on its join screen, so that is the game's look, not the RSX. (The PC run itself
never joined: "World cache files disappeared. Join canceled", a cache directory the container lacks.)

Smoke-tested: one launch to see whether it starts and plays (`SUITE=0`, no benchmark numbers kept; the smoke
runs are described in [SUITE.md](../../../docs/games/SUITE.md#smoke-runs)). The recipe
([benchmark/recipe.sh](benchmark/recipe.sh)) starts `bzfs` on port 5154 (a generated world, jumping,
teleporters, ricochet) and the client with `-solo 3 smoke@127.0.0.1:5154`, which connects at once; XTEST
presses `i` (spawn) and Up 10 s after the window. The client is fullscreen unless `-window` is given, at
the screen's resolution, so no console default needs a patch.

Controls: the mouse steers by default (`allowInputChange` switches to the keyboard or a joystick when one is
used). The DualShock 3 works only once Options > Input Settings names it (`joystickname`, default off) and
its buttons are bound by hand: no auto-mapped scheme yet (todo.toml).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-16, [game-candidates.md](../../research/game-candidates.md), "Candidates": BZFlag 2.4.30, Debian `bzflag-client` (18 MB); builds on ppc64; fixed function, GLEW: tier A; small RAM; no benchmark mode; its own mixer on SDL. Verdict for the suite: no.
