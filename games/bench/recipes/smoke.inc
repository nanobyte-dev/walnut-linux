# Shared by the smoke recipes (docs/GAME-PLAN.md, "Smoke tests (not benchmarked)"): games tried once
# to see whether they start and play, not measured. A recipe sources this file first. Not a recipe
# itself (run.sh list only shows recipes/*.sh).
#   SUITE=0     kept out of `run.sh suite`
#   DURATION    120 s by default, SMOKE_SECS=N overrides it (run.sh smoke caps a run at 360 s,
#               BENCH_TIMEOUT=N raises that: SMOKE_SECS=600 BENCH_TIMEOUT=700 run.sh smoke GAME)
#   WARMUP, MEASURE   only for the HUD summary in the report (10 s after the load, then 90 s); no
#               fps is parsed from the game and nothing is compared with it
#   vblank_mode=0     no swap interval from Mesa, whatever the game asks for (X-Moto's recipe does this too)
# The game is started in the closest thing to its play state the command line allows; a recipe
# says in its header when that is only a menu and the player has to start it (keyboard or the DS3).
SUITE=0
DURATION=${SMOKE_SECS:-120}
WARMUP=10
MEASURE=90
ENV+=(vblank_mode=0)
