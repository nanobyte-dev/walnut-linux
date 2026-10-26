# Trigger Rally: technical notes

## Status

Debian's Trigger Rally 0.6.6 (`trigger-rally`, `-data`, 116 MB) is installed on the PS3 and has a smoke
recipe, but it is menu only (no command line): Single Race with the arrow keys, so it waits for the user at
the controller; no run yet. setup() ([benchmark/recipe.sh](benchmark/recipe.sh)) writes cheap graphics (no
foliage, weather, dirt, anisotropy; point snowflakes) into its config; variant `full` is the game's defaults.
