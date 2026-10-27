# OpenRCT2: technical notes

## Status

Published, Untested: OpenRCT2 0.5.6 ported to big-endian ppc64, packaged, installed on the PS3 and baselined, not
played yet. Under qemu-ppc64 the build matches amd64 bit for bit on every check (gtest 275/275, 35 parks after
10000 ticks, 210 screenshots, 169 track designs: [the port](investigations/2026-10-19-big-endian-port.md)).
Software renderer only (the GL engine needs GL 3.3 core). It needs RollerCoaster Tycoon 2's data from the
player's own copy: game-data-packager's `roller-coaster-tycoon2-data` (build.toml `[data_setup]`, in
`/usr/share/games/roller-coaster-tycoon2`, a path upstream searches; not yet tried on the PS3) or our licensed
`rct2-data` (from the CD, `/usr/share/games/rct2`, the path our patch 0005 adds). On the PS3 it runs at 21-24 fps in Electric Fields (10 fps in Six Flags Magic
Mountain) with the console's RAM nearly gone.

- **Package** (`0.5.6+ds-1+ps3.N`, [packages/openrct2](packages/openrct2/), `TYPE=upstream`): debs `openrct2`
  (any), `openrct2-data`, `openrct2-music` (all). Patches
  [0001](packages/openrct2/patches/0001-big-endian-core-of-PR-27114-AmigaOS-port.patch)-[0004](packages/openrct2/patches/0004-big-endian-S6-S4-swaps-as-functions-open-park-hook.patch)
  and [0006](packages/openrct2/patches/0006-big-endian-network-serialiser-object-checksum-coords.patch) are the big-endian port,
  [0005](packages/openrct2/patches/0005-linux-rct2-search-path-werror-option.patch) the RCT2 search path (the game finds RCT2 in
  `/usr/share/games/rct2`, no config needed) and a `-Werror` option. Build rules: Release, scripting,
  OpenGL, Discord, HTTP and the version checker off, `OPENRCT2_WERROR=OFF`, `DOWNLOAD_*=OFF` with the asset
  zips unpacked into data/ first; g2.dat and friends are built by the ppc64 `openrct2-cli` itself during the
  build (identical to little-endian); tests not run in the build (they need RCT2 data). About 45 min a build
  under qemu. Sizes: openrct2 5.7 MB (installed 31 MB), openrct2-data 23 MB (37 MB), openrct2-music 43 MB.
- The headless CLI needs `game_path` in its config (upstream searches for RCT2 only when not headless):
  `openrct2-cli set-rct2 /usr/share/games/rct2` once; the GUI finds it by itself. The RCT2 data lacks
  `ObjData/BATTRRAM.POB` (a medieval music file of the CD install): a harmless warning.
- **Data**: `rct2-data.sh <RCT2.iso>` (`make licensed-data PKG=rct2-data INPUT=<image>`) builds the licensed
  `rct2-data` deb (arch all, `/usr/share/games/rct2`, about 115 MB without the ride music; `RCT2_MUSIC=1` adds
  it) from the player's own RollerCoaster Tycoon 2 CD. It unpacks the InstallShield cabinets with `unshield`
  and runs nothing from the disc; the package goes to the licensed suite only, and the data is never committed.

## On the PS3 (batch ssdroot8)

First runs (gamedb:batch/2026-10-19-ssdroot8-openrct2): kernel .472, SSD root, Mesa ps3.545~dev, openrct2
ps3.574, the recipe ([benchmark/recipe.sh](benchmark/recipe.sh): Electric Fields at 960x540 x2, 300 s per run,
`STALLS=1 MEM=1`), 2 runs of each variant, rotated, none interfered; the right picture in all 16 photos.

| Variant | Steady fps (HUD) | 1% low | Frames over 100 ms | CPU | Lowest MemAvailable | Peak RSS |
|---|---|---|---|---|---|---|
| `base` (sound, threads, 960x540 x2) | 23.5 (23, 24) | 11.0 | 11 and 16 (13 s) | 399 s | 7.5 and 12.3 MiB | 129-135 MB |
| `lean` (no sound, 1 thread, 1 arena) | 22 (23, 21) | 10.9 | 16 and 13 (12 s) | 328 s | 14.5 and 15.4 MiB | 130-137 MB |
| `big` (Six Flags Magic Mountain) | 10 (10, 10) | 3.4 | 287 and 280 (52 s) | 357 s | 9.1 and 9.3 MiB | 135 MB |
| `sdlsw` (SDL software renderer, no GL) | no number | no number | no number | 289 s | 14.5 and 15.7 MiB | 133-138 MB |

- The HUD fps is the rate of the GL swap; the game's simulation is capped at 40 ticks a second and runs below
  that. `sdlsw` has no Mesa in the process, so no HUD and no other fps source: 28% less CPU than `base` is
  its only number; it needs a frame counter of its own.
- `lean` buys 70 s of CPU and about 5 MiB of the lowest MemAvailable, not fps. `big` is CPU-bound: the
  simulation of 2,700 guests makes every frame longer than 100 ms.
- The 7.5-9.2 s longest frame is the park's load under swap, not gameplay: in all six GL runs it begins 19-26 s
  after the start, made of vswap (1.8-3.2 s), reclaim (1.4-2.3 s) and CPU (2,300-3,600 major faults,
  5,000-9,300 swap-ins); a second 1.3-4.0 s frame follows. After that `base` and `lean` have a few 100-450 ms
  frames and no more.
- Memory: the process keeps 100-138 MB in swap (the 70 MB entity table, `kMaxEntities` 65535, is 58 MB
  resident at the start and 25 MB swapped at the end), VRAM peak 25.7 MiB, GART 19 MiB. MemAvailable falls to
  7.5-16 MiB in every variant: the console is at its floor with nothing else running. The entity table at
  16384 (about -43 MB) is the first thing to try for the load stall, after the oracle is re-run.
- ALSA underruns: 289-317 (`base`), 410-416 (`big`), 238-299 in `lean` and `sdlsw` although `lean` has its sound
  off (why is not known). Not judged by ear.

## Investigations

- [2026-10-19 the big-endian port](investigations/2026-10-19-big-endian-port.md): what was ported, the bugs
  beyond PR #27114, the checks, the RAM on the PC.
- [2026-10-19 the big-endian port estimate](investigations/2026-10-19-big-endian-port-estimate.md) (written
  before the port).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "Ranked table": no hard no: the GL renderer needs GL 3.3 core (no GL2 path), but the software renderer through SDL works; contrib, `architecture-is-little-endian`; the BE port can reuse the closed AmigaOS PR #27114 (~1,700 endian lines in 33 files, applies to 0.5.6 with 2 conflicts) plus TD6/TD4 import, RLE zoom, ImageImporter, Ogg/FLAC fixes: 7-11 person-days; data: our rct2-data deb; measured GUI no-GL 282-300 MB RSS, headless 154 MB, trimmed estimate 210-230 MB. Effort L. Verdict: worth a build, staged (RAM decides).

