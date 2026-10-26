# Warmux: technical notes

## Status

Debian's Warmux 11.04.1. First run in batch ssdroot3 (gamedb:batch/2026-10-19-ssdroot3-smoke-new): it starts,
the main menu in a small window, the sound running with 26 underruns; 29 MB free, 90 MB peak RSS. Fullscreen:
the config is `~/.config/wormux/config.xml` (the recipe's BACKUP path was wrong); `setup()` now sets
`full_screen` to true when the file exists, which it does after a first run. Menu only, it waits for the user
([benchmark/recipe.sh](benchmark/recipe.sh)).
