+++
id = "reload-stalls"
games = ["openarena"]
driver_patches = ["linux/local-0009"]
game_patches = ["ioquake3/0025", "ioquake3/0026"]
sessions = []
batches = ["2026-10-17-stall-recorder", "2026-10-18-lost-decrementer", "2026-10-18-ioquake3-396", "2026-10-18-kernel-419-idle", "2026-10-18-batch16"]
+++

# What stalls in OpenArena: map reloads, bots joining and the lost decrementer (2026-10-17)

Moved on 2026-10-26 from docs/STALLS.md ("OpenArena") and docs/TESTS.md's history rows, merged. The
stall recorder, report.py's gameplay-only columns (which OpenArena's reloads led to) and the kernel's
lost decrementer, a cause outside the game that OpenArena showed first, stay described in
[docs/STALLS.md](../../../../docs/STALLS.md#why-frames-stalled-the-lost-decrementer).

## Symptom

The first stall-recorder runs (ioquake3 ps3.379, the timedemo looped, 20261017T233701Z and T234549Z)
had 17.6-17.9 s of stall time in the window, 81% of it on the main thread's CPU.

## Evidence

`passsplit.py` splits it by where it falls in a pass:

- 10.2-10.8 s are the map reload at the start of every pass: three load-screen frames of 340, 700 and
  380 ms, the world's textures read from pak4-textures.pk3, decoded and compressed to DXT on the PPE.
- 7.1-7.3 s are the demo itself: four stalls a pass of 130-400 ms, 6.9, 10.1, 13.5 and 16.7 s into it.

## Cause

The stalls in the demo are bots joining. The demo starts a match with bot_minplayers, which adds a bot
every 10 s of game time (every 200 timedemo frames), and OpenArena's qagame sends `loaddefered` with
every bot added during play, so the cgame loads its models, skins and textures at once
(cg_deferPlayers can't defer them). Real play does the same at every match with bots. A host trace
(llvmpipe, the demo, three passes) has per pass 5 joins, 15 MD3 models, 15 skins, 35 image lookups and
23 texture uploads during the demo. Sounds load once per session (OpenAL keeps them across levels).

Many of the stalls left had no cause in the game at all: an idle PPE thread lost its decrementer
interrupt and woke late. The recorder itself masked that (it keeps a thread busy), so these runs describe
the stalls left with it masked; without the recorder OpenArena had 16.1 and 13.3 s of gameplay stalls,
6.5 and 6.2 s with the 10 ms cross-CPU kicker (`WAKER=10`, `WAKER_MODE=kick`,
gamedb:batch/2026-10-18-lost-decrementer). The reloads and the bot joins are real either way.

## Fix

ioquake3 0025-0026 (+ps3.396, both on by default, host frames identical over three passes):

- `r_keepImages 1` keeps the textures of image files across a map change and takes them back by name
  when the next level is the same map (host reload 0.46 -> 0.17 s, the joins' uploads gone).
- `cl_preloadLate 1` registers what the cgame registered during the last level's play again at the end
  of the next load of the same map. From the second pass on nothing loads during the demo; the longest
  frame went 37 -> 14 ms on the host.

Both start working at the second pass, which the window never includes. `cl_deferPlayers 1` (opt-in)
drops `loaddefered`: the bots keep a substitute model, which changes the picture. Recipe variants
`keepimg0`, `preload0`, `loads0` (both off) and `defer` (`gamedb/games/openarena/benchmark/recipe.sh`).
On the PS3 (docs/TESTS.md 10-18 08:25, commit 2d3243c) gameplay stalls went 9-10 s -> 0 and steady fps
65 -> 70.6:

<!-- gamedb:batch-table 2026-10-18-ioquake3-396 games=openarena metrics=fps_steady,stall_play_s,low1_play root=../../.. -->
| Game | Variant | `fps_steady` (fps) | `stall_play_s` (s) | `low1_play` (fps) |
|---|---|---|---|---|
| [OpenArena](../../../games/openarena/) | base@rec10+wkkick10 | 70.6 | 0 | 34.6 |
| [OpenArena](../../../games/openarena/) | loads0@rec10+wkkick10 | 65.2 | 9.4 | 31 |

Numbers: gamedb:batch/2026-10-18-ioquake3-396 (ioquake3 ps3.396 (r_keepImages + cl_preloadLate), libspeq 0.394 (RGB DXT on SPEs), Mesa ps3.408 default LUT; recorder + kick, 2026-10-18; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

With both on, gameplay stalls went 9.9 and 9.0 s to 0 and the longest frame 1057 and 807 ms to 64 and
47 ms, against the `loads0` variant.

The kernel's idle fix (local/0009, `ps3_idle_prep`) then replaced the kicker: 0.4 and 0.4 s of gameplay
stalls with it against 9.0 and 7.9 s without, the longest frame 0.22-0.38 s against 2.2-2.8 s, on kernel
.433 with no kicker (gamedb:batch/2026-10-18-kernel-419-idle, gamedb:batch/2026-10-18-batch16).

The opengl2 renderer has no `r_keepImages` (0025 is opengl1's); its reload story is
[opengl2-freezes](2026-10-18-opengl2-freezes.md).
