# Simutrans: technical notes

## Status

Debian's Simutrans 124.5 with pak64. First run in batch ssdroot3 (gamedb:batch/2026-10-19-ssdroot3-smoke-new,
SSD root, 120 s, one run): it starts a new map, with a credits dialog on top, at 25 fps HUD, run with
`-nosound`; 34 MB free at the lowest, 74 MB peak RSS. The recipe ([benchmark/recipe.sh](benchmark/recipe.sh))
now sends Escape to close the dialog (`during()`; the window title `Simutrans` unverified), and the shots count
from after its keys (`SHOT_FROM=mark`). It started again in gamedb:batch/2026-10-19-ssdroot5-smoke. Not played
yet.

Again on 2026-11-14 (kernel .1529, ps3rsx 0.1534, Mesa ps3.1508, gamedb:batch/2026-11-14-s1114-smoke): Escape closed the credits dialog (the window title `Simutrans` is right), and the new pak64 map is drawn fullscreen 1920x1080 with its towns, roads and water. Slow: 18 fps on the HUD, 1% low 8.5, 57 frames over 100 ms in 90 s (7.2 s), longest 381 ms, with nothing being built.
