# Dungeon Crawl Stone Soup: technical notes

## Status

Debian's Dungeon Crawl Stone Soup 0.34.1 (the tiles build), started by our `crawl-walnut` (below: the first
start now loads in 14.5 s, fullscreen). First run in batch ssdroot3
(gamedb:batch/2026-10-19-ssdroot3-smoke-new): it starts, but was still on "Loading maps..." in its small splash
window at 60 s, with 67 s of CPU in the 120 s: the load takes longer than 120 s (how much longer is open), so its
17 MB free and 91 MB peak are the load's, not play's. Ending it with SIGTERM prints Crawl's "crash" report, which
is the harness. A 300 s run, then the fullscreen check, are next; not played yet
([benchmark/recipe.sh](benchmark/recipe.sh)).

## The first start: "Loading maps..." for minutes

On a fresh home Crawl compiles its 143 vault files (`dat/des/*.des`, Lua inside) and its text database
into caches in `~/.crawl/saves/des` and `saves/db` before the first screen: 7 s of one core on a PC
(4.7 s for Debian's console crawl in a Docker container on the dev PC), minutes on the PS3's PPE (67 s
of CPU in the first 120 s run and still loading; the 300 s run of 2026-10-20, with the cache from the
first, was idle in the game 25 s in). Every later start reads the caches.

Each cache file records the save version, its source file's mtime and the byte order and word size
(`WORD_LEN`: 8 on a little-endian PC, -8 on the PS3), so a cache is only good on a machine of the same
byte order, for the same `crawl-common` deb (arch all: the same files and mtimes everywhere). A PC's
cache would be refused and remade; one made on ppc64 is what the PS3 writes itself (the package's
cache and a PC's differ only in that byte and the byte-order-dependent data; the mtimes match). And
crawl-tiles reads the console crawl's cache as its own: on the PC it started on one in 0.18 s and
rewrote none of it (2026-10-28).

Our package `crawl-walnut` ([packages/crawl-walnut](packages/crawl-walnut/)) makes the caches at build
time (Debian's console `crawl -builddb` under qemu in the ppc64 chroot, against exactly
`crawl-common` 2:0.34.1-2, which it depends on) and installs them in `/usr/share/crawl-walnut/saves`.
Its `/usr/games/crawl-walnut` copies the missing files into `~/.crawl/saves` (never over a file there)
and starts `crawl-tiles -extra-opt-first tile_full_screen=true`: fullscreen at the desktop's size
(Crawl's own default, `auto`, opens a 1830x990 window on a 1920x1080 screen; a player's rc file still
decides). Our menu entry (`desktop/launchers/crawl-tiles.desktop`, through `ps3-game`) starts it, and
Debian's entry is diverted (build.toml). On the PS3 (2026-10-29, `run.sh smoke crawl-tiles walnut`, the
caches removed first, crawl-walnut 0.931~dev): "Loading complete, press any key to start" 14.5 s after
the start, fullscreen 1920x1080, and the home's cache afterwards byte-equal to the package's (crawl took
it and remade nothing): the first start is as quick as any later one
(`ps3-bench-20261028T221502Z-smoke-crawl-tiles-walnut`).
