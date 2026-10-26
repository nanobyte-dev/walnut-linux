+++
id = "mission-ends-by-itself"
games = ["hedgewars"]
driver_patches = []
game_patches = ["hedgewars/0001"]
sessions = []
batches = ["2026-10-19-ssdroot4-smoke", "2026-10-19-ssdroot8-smoke"]
+++

# Hedgewars: the training mission ends by itself after 55 s (2026-10-19)

**Symptom.** The engine runs on the PS3 with sound (562-650 underruns), but the game ends by itself after
54-55 s with rc=0, in two runs (base at 19:19Z and window at 23:00Z, gamedb:batch/2026-10-19-ssdroot8-smoke),
with no input in either.

**Evidence.** That is the engine's end of game, not our driver: the last lines of game.log are the water
Droplet sounds and `Victory.ogg`, hwdrive.py logs `engine says q`, and `q` is what the engine sends from
`gtATFinishGame` (uGearsHandlersMess.pas) after the mission's `EndGame()`. The Movement mission calls that
from `victory()`, which runs when its final crate (`crates[2]`, spawned at 2639,1973) is deleted, normally by
a hedgehog collecting it. Nobody walks here, so on the PS3 the crate is probably lost to the water. The same
mission with Debian's amd64 engine under Xvfb, driven by the same hwdrive.py with no input, ran the whole 250
s without ending: the ppc64 engine's world differs.

**Cause (suspected).** The upstream engine's Lua tests already fail 2 of 7 on ppc64 (`stuckcake.lua`:
`EInvalidOp`; `gravity_get_set.lua`: a wrong grenade height at gravity 200 and 300), so the physics of
falling gears is the suspect, not the IPC, the recipe or the map data (the `draw` command reads its points
byte by byte).

**Status.** Not fixed, parked: an engine bug to find with the Lua tests on ppc64 (a crate-drop test would
show it). Nothing to change in the recipe or the driver: they send no quit and set no time limit;
hwdrive.py's message now says that `q` is the game's own end.
