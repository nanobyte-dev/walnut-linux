+++
id = "two-frame-rates"
games = ["supertuxkart"]
driver_patches = []
game_patches = ["supertuxkart/0005"]
sessions = []
batches = ["2026-10-18-batch8", "2026-10-18-batch16", "2026-10-18-batch17", "2026-10-19-ssdroot1-baseline"]
+++

# SuperTuxKart: the "Average FPS" is a tick rate (2026-10-19)

**Symptom.** The recipe read the game's own `Number of frames: N time T, Average FPS: F` line as the frame
rate: 105-111 fps for `lean`, while the Gallium HUD said 19-22 and the race looked like 20.

**Evidence.** The number of frames was 21602 in every run. Patch 0005 counts `ProfileWorld::update` calls,
one per physics tick, and STK steps physics at 120 Hz, so 180 s of race is 21600 ticks whatever the
renderer does. The HUD counts frames that reached the screen: 5436-5692 swaps in a 300 s run, 3.8-4.0
physics ticks per frame.

**Cause.** The "Average FPS" is 120 times the race time over the wall time (0.88-0.92 of real time: the
race runs a little slower than a clock when frames are long): a simulation speed, not a frame rate.

**Fix.** The recipe no longer reads the game's line as fps (`FPS_RE` and `STEADY_RE` removed): fps and
steady come from the HUD, as the `base` row always had them, and `parse()` keeps `stk_ticks`,
`stk_tick_s` and `stk_tickrate` in bench.txt. The `fps` and `fps_steady` values of
gamedb:batch/2026-10-18-batch8, gamedb:batch/2026-10-18-batch16 and gamedb:batch/2026-10-18-batch17 were
corrected to the HUD medians (they held 106-111).
