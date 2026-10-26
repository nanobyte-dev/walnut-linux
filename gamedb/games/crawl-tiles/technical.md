# Dungeon Crawl Stone Soup: technical notes

## Status

Debian's Dungeon Crawl Stone Soup 0.34.1 (the tiles build). First run in batch ssdroot3
(gamedb:batch/2026-10-19-ssdroot3-smoke-new): it starts, but was still on "Loading maps..." in its small splash
window at 60 s, with 67 s of CPU in the 120 s: the load takes longer than 120 s (how much longer is open), so its
17 MB free and 91 MB peak are the load's, not play's. Ending it with SIGTERM prints Crawl's "crash" report, which
is the harness. A 300 s run, then the fullscreen check, are next; not played yet
([benchmark/recipe.sh](benchmark/recipe.sh)).
