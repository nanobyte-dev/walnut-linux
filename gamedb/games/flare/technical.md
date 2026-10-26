# Flare: technical notes

## Status

Debian's Flare 1.15. First run in batch ssdroot3 (gamedb:batch/2026-10-19-ssdroot3-smoke-new): it starts
fullscreen with a language dialog over the title scene, HUD 59 fps (its `max_fps=60`), the sound running with 5
underruns; 48 MB free, 72 MB peak RSS. Menu only: the player picks a language and starts a character
([benchmark/recipe.sh](benchmark/recipe.sh)).
