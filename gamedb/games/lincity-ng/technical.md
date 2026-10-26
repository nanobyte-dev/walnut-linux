# LinCity-NG: technical notes

## Status

Debian's LinCity-NG 2.15.0. First run in batch ssdroot3 (gamedb:batch/2026-10-19-ssdroot3-smoke-new): it
starts, the main menu fullscreen (`SDL Mode 1918x1053`), the sound card running with 16 underruns; 34 MB free,
65 MB peak RSS. Menu only: the player loads a scenario, so it waits for the user
([benchmark/recipe.sh](benchmark/recipe.sh)).
