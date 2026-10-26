+++
id = "memory-variants"
games = ["luanti"]
driver_patches = []
game_patches = []
sessions = []
batches = ["2026-10-18-luanti-memory", "2026-10-18-luanti-clean-reruns", "2026-10-18-batch16"]
+++

# Luanti: the memory variants, the interfered round and its clean repeat (2026-10-17/18)

**Symptom.** The first run (2026-10-17) gave 67 fps HUD median in flight but a p10 of 23 and stalls to 0
while chunks loaded, with 177 MB swapped out and 32 MB free. Twenty memory variants were built, one lever
each (`saved`, `nopp`, `mip`, `chunk3`, `srvunload`, `active2`, `gen3`, `io`, `cliunload`, `leaves`,
`tradeoff`, `flatlight`, `nominimap`, `view40`, `view90`, `nomerge`, `merge100`, `meshdelay`,
`meshchunk2`, `noraycull`, `malloc`, `trim16`, `trim64`; the bundles `lean` and `leaner`).

**Evidence.** The round of gamedb:batch/2026-10-18-luanti-memory looked good: `cliunload` 6.5 to 0.8 s of
stalls, `lean` rendering less terrain. The interference retro-scan then found digs and clicks during
base, lean and tradeoff: somebody had played in those runs, so the cells were invalid
([STALLS.md, "Interference"](../../../../docs/STALLS.md#interference)). The clean re-runs
(gamedb:batch/2026-10-18-luanti-clean-reruns, kernel .433, the kicker) and the lean parts alone
(gamedb:batch/2026-10-18-batch16) replaced them:

| Variant | Result | Verdict |
|---|---|---|
| base (x2) | 45.5 fps, 1.5 and 2.1 s of stalls | stays the default |
| `cliunload` | worse: 3.8 and 5.2 s of stalls, 2 s hitches when unloaded blocks come back | rejected |
| `lean` | 56-57 fps, 0.4 and 0.9 s of stalls, but a black void on the webcam at 150 s in both runs (no sky, no terrain) | renders nothing |
| `chunk3` alone | the black void at 150 s: `lean`'s culprit | rejected |
| `srvunload`, `active2` | render, but stall more (6.3 and 6.7 s against about 2.5) | rejected |
| `nomerge` | 2.5 s of stalls | no gain |
| `view40` (one clean run of the first round) | 59 fps, closer fog, 0.9 s of stalls | the one variant worth offering, to repeat |

**Outcome.** None of the memory variants beat the default on clean runs; `view40` waits for its repeat
and then a menu option (the game's todo).
