# OpenRCT2 on the PS3

OpenRCT2 0.5.6 ported to big-endian ppc64 (milestone 1, 2026-10-19): under qemu-ppc64 the build matches amd64 bit for bit on every check (gtest 275/275, 35 parks after 10000 ticks, 210 screenshots, 169 track designs). Software renderer only; needs the licensed rct2-data package (the user's RCT2 CD, ps3-licensed only). RAM on the PC 230-310 MB; the first PS3 run is the next step.


## Progress (resume here)
- [x] downloads in /mnt/data_linux/Temp/ps3/openrct2/dl (tarball + 5 asset zips, sha256 = upstream assets.json)
- [x] Debian contrib 0.5.6+ds-1 packaging read (/mnt/data_linux/Temp/ps3/openrct2/debsrc)
- [x] PR #27114 diff fetched (dl/pr27114.diff, dl/pr27114.json), endian core applied on v0.5.6 in
      /mnt/data_linux/Temp/ps3/openrct2/src (git, branch ps3), 2 conflicts resolved
- [x] ppc64 sysroot (sysroot.sh -> /mnt/data_linux/Temp/ps3/openrct2/sysroot, root-owned, 1.6 GB)
- [x] dev image ps3-orct2-dev:1 (dev-image/Dockerfile: cross g++-16-powerpc64 + amd64 deps + gdb)
- [x] PR core cleaned of Amiga bits, gaps fixed, cross build (build.sh be|le), gtest 275/275 on both
      (BE under qemu-ppc64, incl. the 19 replays with LE-recorded checksums)
- [x] oracle (oracle.sh TICKS): 26 SC6 + 9 test parks, simulate + save uncompressed park, chunk compare:
      35/35 identical at 100 ticks (after RCT12ResearchItem + RCT12TrackElement fixes)
- [x] oracle at 5000 ticks: 35/35 identical; open-park oracle (OPEN=1, guests arrive) at 10000 ticks: 35/35
- [x] DWARF check of the S6/S4 swap tables (swapcheck.py): no field missing (see below)
- [x] screenshots (shots.sh) of the 35 open-park saves at 10000 ticks, 6 views each: 210/210 byte-identical
- [x] RAM on the PC (ram.sh, ram-all.sh, smaps.sh): table below
- [x] openrct2/ package (package.conf, debian/, patches/ 0001-0005), config.mk + Makefile pins: commit 341fd04;
      patch 0006 (serialiser) commit 70e96eb
- [x] recipe games/bench/recipes/openrct2.sh, selftest passes: commit bdbfead
- [x] make openrct2-deb (build-deb.sh, chroot on the overflow disk): first build ps3.571 (45 min, 5 patches)
      OK; pkgtest.sh (debs installed in the ppc64 deps chroot, qemu): Electric Fields 3000 open-park ticks
      gives the amd64 park, chunk for chunk (only authoring differs: version string)
- [x] final build ps3.574 (patches 0001-0006, changelog fix; commits 70e96eb, 6239a24): pkgtest again identical
      to amd64 (Electric Fields, 3000 open-park ticks, checksum 20994dc4a8b051a1, every chunk but authoring)
- [x] overflow trees deleted (builds, sysroot, chroot copies, oracle outputs; summaries in results/); kept the
      work tree /mnt/data_linux/Temp/ps3/openrct2/src (dev branch has the unexported dev-only tests) and dl/
      (tarball, zips, replays.zip, pr27114.diff/.json). Dev image removed (dev-image/Dockerfile rebuilds it;
      sysroot.sh rebuilds the sysroot, ~10 min)

## How to run things (all scripts in this dir)
- build.sh be|le [targets]: cross ppc64 / native amd64 build of the work tree into /mnt/.../openrct2/b-be, b-le
- run.sh 'cmd': dev container with /src /b-be /b-le /sysroot /data(rct2) /w(scratch) /dl /scripts mounted;
  ppc64 binaries run through binfmt qemu-ppc64-static with QEMU_LD_PREFIX=/sysroot
- data dir for runs: /w/share/openrct2 (src data + g2/fonts/palettes/tracks.dat + the 4 asset zips), symlinked as
  b-*/data; config /w/home/.config/OpenRCT2/config.ini (game_path=/data); replays copied into the source
  testdata (git-excluded)
- tests: run.sh 'cd /b-be; ./OpenRCT2Tests --gtest_brief=1'
- oracle: run.sh '/scripts/oracle.sh 5000' -> /w/oracle/5000/summary.txt; parkdiff.py A B (chunk compare),
  tilediff.py, chunkdump.py for digging into a difference

## Bugs found beyond the PR (each fixed in the work tree)
- PR's RCT12ResearchItem: RawValue swapped but the byte members read in LE order -> research lists garbage on BE
- PR's maze-only swap of RCT12 track bytes 5/6: importer reads Colour/Sequence that alias MazeEntry ->
  track flags2 differed on mazes (bpb.sv6, BigMapTest.sv6); fixed by host-ordered members + unconditional swap
- PR's park TILES swap was Amiga-only (#ifdef __amigaos__) and keyed mazes on a rideType heuristic:
  replaced by TileElement::LittleEndianToHost with host-ordered union bytes (path additionStatus, track sequence)
- CSStringConverter: bsearch key read as the first 2 bytes of a wchar_t (high half on BE) -> RCT2 accented
  characters not converted
- TD6/TD4/TD7 track designs, scores.dat, file index header, sprite tool (g2.dat build), chunk writer header,
  Sawyer/RCT1 checksums, RLE magnify row offsets, PNG importer offsets, Ogg/FLAC formats, 16/8-bit texture path,
  coordinate structs written raw by the DataSerialiser (track design actions)
- build: DISABLE_HTTP misses <future>; ScriptingTests built without scripting

## Layout
- scripts: build/tmp/openrct2/ (this dir); big trees on /mnt/data_linux/Temp/ps3/openrct2/
- work tree: /mnt/data_linux/Temp/ps3/openrct2/src (clone of the earlier orct2-be clone; has pr27114 branch,
  merge base 3418b4f8; v0.5.6 = 82b309798)

## PR #27114 provenance
- "AmigaOS 3.x (m68k, big-endian) port [RFC]", https://github.com/OpenRCT2/OpenRCT2/pull/27114
- author Thomas Luebker (GitHub thomas-luebker, 53316655+thomas-luebker@users.noreply.github.com), 43 commits
  2026-09-10..12, head a5dbca57c5be, closed unmerged 2026-09-12 (maintainers: too invasive, AI-generated)
- licence: contributed to OpenRCT2, so GPL-3.0-or-later like the rest of the tree (file headers unchanged)

## Swap-table check (DWARF)
- ByteOrderDump.cpp (dev-only test, not exported) runs ByteSwapS6Data/ByteSwapS4Data on a pattern buffer
  (byte i = i*7+3, so 2/4/8-byte reversals are unambiguous) with entity slots set to every kind; typetree.py
  (gdb python) dumps the S6Data/S4 layout from DWARF; swapcheck.py compares (result: swapcheck1.txt).
- S6: 342159 multi-byte fields outside unions, none missing or wrong width, no stray swaps. Not swapped by
  ByteSwapS6Data: Header.{MagicNumber,NumPackedObjects,Version} (swapHeader runs earlier, by design) and
  Rides[].pad002 (padding, never read).
- S4 (base and LL): none missing; the 2 "missing" fields belong to UnkEntity, but explosion entities are
  imported as RCT12EntityParticle and its Frame is swapped.
- Unions reviewed against what the importers read: Vehicle TrackProgress/TrackTypeAndDirection/SwingPosition
  read as u16 (swapped as u16; boat-hire BoatLocation kept as bytes by the ride-mode check), Peep TimeToSitdown
  (S6 u16, swapped) vs S4 CurrentCar/CurrentSeat (bytes, not swapped), Ride nameArguments (S6 reads the u16
  Number, swapped as two u16), reliability u16, ResearchItem RawValue (u32, byte view now host-ordered),
  RCT12xy8 (bytes, not swapped), RCT1 peepFlagsLL (u32 on LL, 2xu16 before).

## Results, BE (ppc64 under qemu) vs LE (amd64), same source
- gtest: 275/275 on both (incl. 19 ReplayTests, recorded on x86, entity checksums tick by tick)
- g2.dat/fonts.dat/palettes.dat/tracks.dat built by the BE `sprite build`: byte-identical to LE
- oracle: every RCT2 scenario (26 SC6 in our rct2-data) + 9 test parks (SV6 and .park): load, simulate,
  save uncompressed .park, compare the entity checksum and every chunk except authoring:
  100 ticks 35/35, 5000 ticks 35/35, 10000 ticks with the park opened (guests in 28 of them, up to 2782)
  35/35. Determinism needs OPENRCT2_FIXED_SEED (ScenarioBegin seeds from the clock) and
  SOURCE_DATE_EPOCH (authoring timestamps); both are hooks in patch 0003.
- renderer: 210/210 screenshots identical (giant zoom 2, rotations 0-3; 1920x1080 at zoom 0 rot 0 and
  zoom 1 rot 2) of the 35 parks after 10000 open-park ticks
- track designs: all 169 TD6 in rct2-data imported, serialised (DataSerialiser, the network/replay form) and
  exported back to TD6 on both hosts: identical (dev-only test ByteOrderDump.TrackDesigns, td-le.txt /
  td-be.txt in /w). This found patch 0006: the object entry checksum went into the serialiser in host order,
  and coordinate structs carried their (uninitialised) padding.
- not covered: the zoomed-in (magnify) RLE path (the CLI screenshot can't take a negative zoom; the fix is
  the one LoadLE16), SDL texture formats other than 32-bit, audio (no device in the container: Ogg/FLAC
  formats fixed by reading, the user judges sound on the PS3), RCT1 data (none here: S4/TD4 checked only by
  the DWARF swap check), network (ignored as agreed), scripting (built off).

## RAM (PC, amd64 build of the same tree; Xvfb 1920x1080; RSS median over 120 s after 60 s)
SDL software renderer with SDL_FRAMEBUFFER_ACCELERATION=0 (no GL driver in the process; SDL3 under
sdl2-compat otherwise puts llvmpipe behind the window surface: +61 MB libLLVM, +13 gallium, +49 threads).
Parks: the open-park saves after 10000 ticks.
| park | case | RSS | anon | HWM | threads |
|---|---|---|---|---|---|
| Six Flags Magic Mountain (2782 guests) | 1920x1080 | 309 | 276 | 335 | 29 |
| same | 960x540 x2 | 300 | 267 | 319 | 29 |
| same | lean (x2, sound off, multithreading off, MALLOC_ARENA_MAX=1) | 260 | 228 | 282 | 5 |
| Electric Fields (small) | 1920x1080 | 264 | 233 | 286 | 29 |
| same | 960x540 x2 | 253 | 221 | 269 | 29 |
| same | lean | 232 | 201 | 248 | 5 |
With SDL's GL renderer on llvmpipe: +140 MB (402-451 MB, the first runs). The PS3 would add nv30's Mesa
userland instead (smaller, its textures in VRAM).
smaps of the lean Magic Mountain run: heap 113 MB (tile elements, objects, strings), 70 MB one block
= GameState_t (73.6 MB: EntityRegistry 57.7 MB = 65535 x Entity_t 512 B + per-entity arrays), 21 + 15 MB
(object images, g1 data), file-backed 25 MB (binary 7, libcrypto 4, SDL 2, ICU...).

Memory knobs (biggest first):
1. kMaxEntities 65535 -> 16384 (EntityRegistry.h): EntityRegistry 57.7 -> ~14.4 MB, about -43 MB, every
   page is touched at start (zero-filled). RCT2 parks hold <= 10000 (RCTC 15000); an OpenRCT2 park with more
   would not load. Needs a re-run of the oracle (entity ids are allocated from the free list).
   Alternative without the limit: allocate the registry so that unused slots stay untouched (calloc/mmap
   zero pages instead of value-initialisation).
2. GL out of the process (variant sdlsw): saves the GL driver's share; on the PS3 costs the scaling on the CPU.
3. Sound off: about -10 to -35 MB (OpenSFX asset pack, mixer buffers; the music streams).
4. multithreading=false + MALLOC_ARENA_MAX=1: -25 MB (fewer arenas and threads); the PPE has 2 threads anyway.
5. 960x540 x2 instead of 1920x1080: -9 to -11 MB (frame buffers), and a quarter of the painting.
6. g1.dat (17 MB) is read into the heap whole; mmap would make it file-backed and reclaimable.
7. Object images load for the park's objects only; a smaller park needs less (Electric Fields -40 MB).

## Package
- openrct2/ (TYPE=upstream), version 0.5.6+ds-1+ps3.N; debs: openrct2 (any), openrct2-data, openrct2-music (all)
- build: build/tmp/openrct2/build-deb.sh openrct2 (chroot and ccache on /mnt/.../openrct2/debbuild); about
  45 min on this host (qemu, 16 jobs, LTO link included); debs in build/debs/openrct2/
- sizes (ps3.571; ps3.574 the same): openrct2 5.7 MB (installed 31 MB: openrct2 17 MB, openrct2-cli 14 MB), openrct2-data 23 MB
  (installed 37 MB), openrct2-music 43 MB, openrct2-dbgsym 118 MB
- headless CLI needs game_path in its config (upstream: Context::Initialise only searches for RCT2 when not
  headless): `openrct2-cli set-rct2 /usr/share/games/rct2` once; the GUI finds it by itself (patch 0005)
- the RCT2 data lacks ObjData/BATTRRAM.POB (a medieval music file of the CD install): harmless warning
- rules: Release, ENABLE_SCRIPTING/OPENGL/DISCORD/HTTP/VERSION_CHECKER off, OPENRCT2_WERROR=OFF (patch
  0005), DOWNLOAD_*=OFF with the zips unpacked into data/ first; g2.dat etc. are built by the ppc64
  openrct2-cli itself during the build (identical to LE, see above); tests not run (need RCT2 data)
- docs keep uncompressed .txt/.md (the game shows changelog.txt); /usr/games/openrct2{,-cli}
- the game finds RCT2 in /usr/share/games/rct2 (patch 0005) -> rct2-data deb, no config needed

## Next steps for the PS3 (milestone 2, with the user)
1. install: openrct2 openrct2-data openrct2-music (ps3-dev, once proven) + rct2-data (ps3-licensed)
2. smoke: games/bench/run.sh smoke openrct2 (base: Electric Fields 960x540 x2), webcam photo; first start
   indexes objects (time it); then variants lean, big, sdlsw; STALLS=1 MEM=1
3. watch: MemAvailable, swap-ins, stalls (expect ~230-260 MB process RSS + Mesa: partly in swap); if it
   thrashes, try kMaxEntities 16384 (knob 1) before anything else
4. sound by ear (Ogg/FLAC OpenMusic paths changed by the port; RCT2's WAV/CSS through SDL conversion)
5. on the PS3 too: a TD6 track design placed from the construction window, a save and reload

## Scripts here (dev tooling, not committed)
build.sh (cross/native builds of the work tree), run.sh (dev container), sysroot.sh, dev-image/Dockerfile,
ppc64-toolchain.cmake, oracle.sh + parkdiff.py/tilediff.py/chunkdump.py, shots.sh, ram.sh/ram-all.sh/smaps.sh,
typetree.py + swapcheck.py (+ the dev-only ByteOrderDump test on the work tree's dev branch), pkgtest.sh,
build-deb.sh. results/: the oracle and screenshot summaries, the TD6 dumps, the swap check, RAM samples.
Work tree branches: export = the series (git format-patch 82b309798..export, then the house header style),
dev = export + the dev-only tests.
