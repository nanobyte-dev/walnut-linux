#!/bin/bash
# How assets/openttd-bench.sav and openttd-bench-far.sav were made (2026-10-19, on the PC, amd64):
# a new game played by AIs in a null-video OpenTTD 15.1 (Debian's orig tarball, built from source with
# null_v.cpp patched: a per-year save, stats, AI strip, camera; none of it is in the package on the
# PS3), then saved with the AIs removed and the camera on the busiest town. Savegames are
# endian-independent, the version (362) is the same on the PS3's 15.1.
#   make.sh WORKDIR     (needs cmake, ninja, SDL2/fluidsynth/curl dev packages, network for BaNaNaS)
# Content used (all from BaNaNaS, licences as listed there): AIs NoNoCAB 9 (GPL v2, x2), AAAHogEx 115
# (GPL v3, x2), LuDiAI AfterFix v27 (GPL v2), SimpleAI 14 (GPL v2), with their libraries
# (Graph.AyStar, Pathfinder.Rail, Pathfinder.Road, Queue.BinaryHeap: GPL v2); base sets OpenGFX 8.0
# (GPL v2), nothing else: no NewGRF. The result needs only OpenGFX, OpenSFX, OpenMSX on the PS3
# (and no AI: the strip makes every company a human one, no script runs, no AI debug window).
set -eu
W=${1:?workdir}; here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$W/src" "$W/y/save/autosave" && cd "$W"
tar -C src -xf /path/to/openttd_15.1.orig.tar.xz         # build/sources/openttd/ in this repo
patch -d src/openttd-15.1 -p1 < "$here/null_v.diff"
cmake -B b -G Ninja -DCMAKE_BUILD_TYPE=Release src/openttd-15.1 && ninja -C b openttd
# content: a dedicated build (-DOPTION_DEDICATED=ON) has the `content` console command:
#   content update; content select <id> ...; content download   (ids from `content state`)
cp "$here/gen.cfg" y/gen.cfg
# 1. 810000 ticks (30 game years from 1950, ~16 min), seed 777; BENCH_YEARSAVE writes y<year>.sav
#    in y/save/autosave every January from 1958
BENCH_YEARSAVE=1 b/openttd -x -c "$W/y/gen.cfg" -g -G 777 -v null:ticks=810000 -s null -m null
# 2. y1962.sav: 132 trains, 600 road vehicles, 5 ships, 83 aircraft, 30 towns (pop 65311), 318 stations.
#    Strip the AIs and set the camera (BENCH_ZOOM=N zooms out N steps first), save as exit.sav
cp y/gen.cfg gen.cfg; mkdir -p save/autosave
BENCH_CAMERA=1 b/openttd -x -c "$W/gen.cfg" -r 1920x1080 -g y/save/autosave/y1962.sav -v null:ticks=1 -s null -m null
cp save/autosave/exit.sav openttd-bench.sav
BENCH_CAMERA=1 BENCH_ZOOM=1 b/openttd -x -c "$W/gen.cfg" -r 1920x1080 -g y/save/autosave/y1962.sav -v null:ticks=1 -s null -m null
cp save/autosave/exit.sav openttd-bench-far.sav
