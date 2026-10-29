# Endless Sky: technical notes

## Status

A draft (tried): our `endless-sky`, `endless-sky-data` (`0.11.2-1+ps3.N`, [packages/endless-sky](packages/endless-sky/)),
replacing Debian's, which has no GL2 shaders and the wrong pixel and sound byte order on big-endian. Debian's
source plus `patches/`: big-endian pixel and WAV byte order, mono sounds, `ES_LOW_MEMORY` defaults, the
game's OpenGL 2 path with GLSL 1.20 shaders in `shaders-gl2`, sprites as 2D atlases, the fog map as
luminance, a cheap outline shader; `packaging/` runs only the unit tests in the build and adds a decode
hash dump. On the PS3 since ps3.624 (2026-10-20) it flies at smoke level: starfield, asteroids, other ships,
the radar and the HUD text show, 480 s without an OOM ([the flight smoke](investigations/2026-10-20-flight-smoke.md)).

Measured on 2026-10-29 (ps3.874 from the stable suite, kernel .874, Mesa ps3.924, the VRAM swap on, the stall
recorder; `ps3-bench-20261029T000953Z-smoke-endless-sky-base`): the keys reach the flight 304 s after the start (the recipe's `ES_LOAD_WAIT` of 240 s, not the game:
the menu is up between 58 and 98 s after the start, screenshots of a `menu` run, `ps3-bench-20261029T042726Z-smoke-endless-sky-menu`), then in flight, over the 90 s from 10 s after Depart (from the HUD's frame times; the run's own window
measured the menu): 30 fps (the median frame 32.5 ms), 1% low 25, two frames over 100 ms (1.5 s, one of them
the screenshot's capture). Smooth at 30. 122 MB peak RSS, no OOM, no "out of temps", sound running with no
audible dropouts. The picture: the ship in deep space with the game's "Lost 1" message, the radar and the
status panel drawn (xwd). The load to the menu, 1 to 1.5 minutes, is the part left to shorten: zlib's inflate and libjpeg lead its
profile (`BENCH_PERF_LOAD=1`, the first 19 s), with the swap active.

## Threads in the stall frames

Checked on 2026-10-29 after gamedb:luanti's stalls turned out to be its render thread waiting for the CPU its own worker threads had (luanti 0010 puts them at nice 10). From the stall record (stalls.py's `stall_main_pct` and `stall_rest_pct`: the CPU of the main thread and of the game's other threads in the stall frames, in % of their time, 100 being one PPE thread; Luanti before its fix: 22 and 83) and the profile (`bench/analysis/stallperf.py`). Runs `ps3-bench-20261029T000953Z-smoke-endless-sky-base` (STALLS=1) and `ps3-bench-20261029T171540Z-smoke-endless-sky-base` (STALLS=1 PERF=1, disturbed at 331 s by an SSH command, left out of the records). Both windows (19-109 s) are the data load, not the flight (from about 300 s). There the main thread ran 43% of the stall time and the other threads 78%; the profile has the two loader threads (patch 0003's PS3 default) at 38% and 36% of the samples and the main thread at 16%: the main thread shows the loading screen and waits for the loaders, as designed. Lowering their priority would only make the load longer. The flight itself had 2 frames over 100 ms in 90 s (TESTS.md, 10-29 02:02).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-a-space.md](../../research/gl3-census-a-space.md), "Endless Sky (own C++ engine, SDL2 + GLEW, upstream master 2026-10-18; Debian sid/ports 0.11.2-1)": the only game of part A worth porting: upstream ships an "experimental OpenGL 2" path (3D textures instead of texture arrays, no VAO, EXT FBO) that needs GLSL 1.20 shaders as a plugin (effort M); the blocker was memory (1.3 GB RSS on the PC at the lowest texture settings, 477 MB of it textures). Scores: popularity 4, novelty 5, playability 2; ppc64 binary in debian-ports; data free, ~400 MB.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "The top 3 and first steps": rank 3 (the GL2 path): go/no-go on the PC first (what the ~700 MB of non-texture RSS is), then the 3D-texture route against nv30, the 22 shaders at GLSL 1.20, DXT at upload, a PS3 smoke run.

## Investigations

- [2026-10-19 memory and renderer](investigations/2026-10-19-memory-and-renderer.md): the ~700 MB explained (glibc fragmentation of 24 loader threads), the GL2 path and its shaders, the big-endian bugs, and the load-phase OOMs on the PS3 that patches 0009-0012 answered (state of 2026-10-20).
- [2026-10-19 GL3 census: Endless Sky](investigations/2026-10-19-gl3-census.md): GL needs from the source and a trace, the port, memory, CPU, big-endian, data.
- [2026-10-20 the first flights without an OOM](investigations/2026-10-20-flight-smoke.md): ps3.602 (no OOM, no HUD: the outline shader's temporaries), ps3.624 (the cheap outlines, playable at smoke level).
