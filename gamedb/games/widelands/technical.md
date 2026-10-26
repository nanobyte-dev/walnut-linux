# Widelands: technical notes

## Status

Debian's Widelands 1.3.1. Its first run (batch ssdroot3) exited after 2 s: `--fullscreen=true` takes no value
(fixed in e40e282); the next (gamedb:batch/2026-10-19-ssdroot4-smoke) refused `--fullscreen` together with
`--xres` (fixed in 57339ca); the third (gamedb:batch/2026-10-19-ssdroot5-smoke) was nudged by the user and is
out of the cells. A clean re-run is pending ([benchmark/recipe.sh](benchmark/recipe.sh)).
