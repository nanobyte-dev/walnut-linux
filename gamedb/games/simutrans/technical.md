# Simutrans: technical notes

## Status

Debian's Simutrans 124.5 with pak64. First run in batch ssdroot3 (gamedb:batch/2026-10-19-ssdroot3-smoke-new,
SSD root, 120 s, one run): it starts a new map, with a credits dialog on top, at 25 fps HUD, run with
`-nosound`; 34 MB free at the lowest, 74 MB peak RSS. The recipe ([benchmark/recipe.sh](benchmark/recipe.sh))
now sends Escape to close the dialog (`during()`; the window title `Simutrans` unverified), and the shots count
from after its keys (`SHOT_FROM=mark`). It started again in gamedb:batch/2026-10-19-ssdroot5-smoke. Not played
yet.
