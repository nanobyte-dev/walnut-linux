+++
id = "big-endian-port-estimate"
games = ["openrct2"]
+++

# OpenRCT2 on the PS3: big-endian port estimate (2026-10-19)

Moved on 2026-10-26 from [game-reeval.md](../../../research/game-reeval.md), where it was part B of the re-evaluation of 2026-10-19 (the file `reeval-part-B-openrct2.md` its ranked table names). It was written before the port: the package and its PS3 runs are in the game's bundle ([technical notes](../technical.md), [the port](2026-10-19-big-endian-port.md)).

2026-10-19. Read-only look at the source plus web research. Nothing was built for ppc64. The RAM
figure in section 9 was measured in a throwaway amd64 container.

Sources examined:
- upstream tag **v0.5.6** (commit 82b30979, 2026-10-10, the newest release and the version Debian ships);
- the branch of the closed **PR #27114** "AmigaOS 3.x (m68k, big-endian) port [RFC]" (head a5dbca57,
  fetched as `refs/pull/27114/head`). It is based on develop at v0.5.5+33 (upstream parent 3418b4f8).

### 0. The short version

Upstream refuses big-endian on purpose. `src/openrct2/platform/Platform.h` has
`static_assert(std::endian::native == std::endian::little, "OpenRCT2 is known to be broken on big
endian...")`, added by PR #23684 (merged 2025-01-27). So this would be our own out-of-tree patch.

The part that changes the estimate: one person already did most of the byte-order work last month. That
was PR #27114 (Thomas Luebker, 43 commits, 2026-09-10/12, AmigaOS m68k). He reported entity checksums
matching x86-64 and arm64. Gymnasiast closed it the same day: "quite invasive", AI-generated, not
mergeable. The code is still fetchable.

- Its endian-relevant core is **about 1,700 added lines in 33 files**. The other ~4,400 lines are
  AmigaOS platform code, an SDL shim and docs.
- It applies to v0.5.6 with `git apply --3way` with **conflicts in only 2 files**
  (`DataSerialiserTraits.h`, `SawyerCoding.cpp`).
- I found **gaps** it does not cover (section 7). Most important: TD6/TD4 track designs, the RLE zoom-in
  path, PNG image import, and Ogg/FLAC music.

The real risk is RAM, not byte order. The GUI measured 282–300 MB RSS on amd64 with no GL at all
(section 9), on a 217.5 MiB machine.

The renderer has to be the software one. The OpenGL engine needs GL 3.3 core (`#version 330 core`,
`GL_R8UI` integer textures, `GL_TEXTURE_2D_ARRAY`), which nv30 cannot do.

### 1. (a) Packed structs and the file formats they map to

`#pragma pack(push, 1)` occurs in **24 headers/sources**. I counted 112 struct/union names inside them;
some are forward declarations counted twice.

Field types inside the legacy-format packed regions (RCT12.h, RCT2.h, RCT1.h, TD46.h, tile_element/*.h),
counted by declaration line:

| type | lines |
|---|---|
| uint8_t | 644 |
| uint16_t | 218 |
| uint32_t | 78 |
| int16_t | 40 |
| money32 | 37 |
| money16 | 29 |
| int32_t | 20 |
| others (2-byte) | fixed16_2dp 10, StringId 9, ObjectEntryIndex 7, TrackElemType 3 |

That makes roughly **450 multi-byte field declarations**. Many are arrays, e.g. `Peep[32]`, the
expenditure tables and the 5 MB tile element array.

| Header | Structs | Format / use | How it's read |
|---|---|---|---|
| rct2/RCT2.h | S6Header, S6Info, S6Data (~5.6 MB), Ride, Vehicle, Peep, Entity, RideRatingCalculationData, StexEntry, TD6Track, TD6EntranceElement, TD6SceneryElement, ScoresHeader, ScoresEntry (15) | RCT2 .SC6 scenarios and .SV6 saves, .TD6 tracks, scores.dat | `SawyerChunkReader::ReadChunk(&_s6.X, sizeof)`: a raw memcpy into the struct; fields then used directly by S6Importer.cpp |
| rct1/RCT1.h | S4, Ride, Vehicle, Peep, Entity, ResearchItem, TD4, TD4AA… (12) | RCT1 .SC4/.SV4, .TD4 | raw memcpy (RCT1File decode, S4Importer, T4Importer) |
| rct12/RCT12.h | 51 names: RCT12 tile elements (16-byte, 10 kinds), entity bases (balloon, duck, litter…), Banner, NewsItem, Award, PeepThought, RideMeasurement, ResearchItem, MapAnimation… | shared RCT1/RCT2 save sub-structures | inside the S4/S6 memcpy |
| rct12/TD46.h | TD46MazeElement, TD46TrackElement, TD7TrackElement | .TD4/.TD6 track elements | `_stream.Read(&elem, sizeof)` in T6Importer/T4Importer |
| world/tile_element/*.h (11 files) | TileElementBase + Surface, Path, Track, SmallScenery, LargeScenery, Wall, Entrance, Banner | OpenRCT2's in-memory map; written raw into the .park TILES chunk (`cs.write(tileElements.data(), n*sizeof(TileElement))`, ParkFile.cpp:1274) | in-memory fine, on disk raw. About 25 multi-byte fields: ObjectEntryIndex, RideId, BannerIndex, TrackElemType, mazeEntry, StationIndex… |
| sawyer_coding/SawyerChunk.h | ChunkHeader (encoding u8 + length u32) | header of every Sawyer chunk (SC6/SV6/SC4/DAT objects) | ReadValue<ChunkHeader> |
| drawing/G1Element.h | G1Header (2×u32); StoredG1Element (u32 offset + 6×u16, in .cpp) | g1.dat, g2.dat, csg1i.dat | ReadValue<G1Header> + `stream->Read(array, count*16)` |
| object/Object.h | RCTObjectEntry (u32 flags + name + u32 checksum) | object .DAT headers, object lists in S6/park files | ReadValue / raw |
| core/OrcaStream.hpp | Header (64 B), ChunkEntry | .park / .parkpatch / .parkrep container | ReadValue<Header/ChunkEntry> |
| network/NetworkPacket.h, NetworkTypes.h | PacketHeader, PacketLegacyHeader, NetworkObjectId | multiplayer | explicitly swapped |
| ride/RideRatings.h, entity/EntityRegistry.h, GameStateSnapshots.cpp | Tuple, EntitiesChecksum, EntitySnapshot | internal only | |
| openrct2-ui/.../opengl/GLSLTypes.h | Vec2/3/4 | GL uniforms | irrelevant (the GL engine is unusable anyway) |

### 2. (b) IStream ReadValue/WriteValue, Endianness.h

- At v0.5.6, `IStream::ReadValue/WriteValue` do no swapping at all: host order, memcpy.
- `ReadValue<>` has **194 call sites in 27 files**.
  - About **80 read multi-byte scalars from game data**: uint16 22, int32 19, int16 15, uint32 13,
    money16/32/64 5, plus multi-byte flag enums. These sit in the object .DAT parsers
    (`object/*Object.cpp`, ImageTable), the Sawyer reader, the FileIndex caches, the title sequence
    reader and so on.
  - About **18 read whole structs**: RCTObjectEntry 8, G1Header 3, ChunkHeader 2, ScoresHeader/Entry 2,
    Orca Header/ChunkEntry/FileIndexHeader 3.
  - The rest are uint8/int8, which are safe.
- Writes: `WriteValue(` 79 + `WriteValue<` 70 in 6 files. `ReadArray<` 10, `WriteArray(` 38.
- Raw struct reads: `Read(&struct, sizeof)` 15 sites. They are in T6Importer (6), T4Importer (4) and
  S6Importer (3), plus the g1 element table and SawyerChunkReader::ReadChunk.
- Today **nothing on the game-data path already swaps**. `core/Endianness.h` has one helper,
  `ByteSwapBE`, which swaps unconditionally (it assumes the host is LE). It is used only by the
  network/DataSerialiser layer: 58 uses in DataSerialiserTraits.h and 5 in network/*. One place, the
  `HostToNetwork` helper in `network/Socket.h`, is host-aware already.
- How the Amiga PR does it:
  - `IStream::ReadValue/WriteValue` swaps every scalar from LE to host.
  - Structs get a `LittleEndianToHost()` member that ReadValue calls: ChunkHeader, G1Header,
    RCTObjectEntry, Orca Header, ChunkEntry.
  - OrcaStream's `readInteger/writeInteger` are a single choke point: 4 lines.
  - ParkFile swaps the 16-bit fields of the raw TILES chunk and the raw DAT object entries.
  - S6Importer/S4Importer get hand-written per-offset swap tables, 506 + 428 lines.
  - The g1 element table is swapped after reading.
  - That one generic IStream change covers the ~80 scalar sites without touching them.

### 3. (c) Bit-fields and unions whose layout differs on BE

- **C bit-fields: none.** No `type name : N;` in the engine. Flags are masks on whole bytes or words,
  which is byte-order neutral once the word is loaded correctly.
- **Unions that alias a wide integer over smaller members: 24**, found by a script over all unions.
  - In legacy format structs, where the swap has to know which view the importer uses (13):
    - RCT2.h ×6: Ride.nameArguments, Ride.reliability, Vehicle.TrackProgress,
      Vehicle.TrackTypeAndDirection/BoatLocation, Vehicle.SwingPosition/FerrisWheelVar, Peep.TimeToSitdown.
    - RCT1.h ×6: the same plus peepFlagsLL.
    - RCT12.h ×4: RCT12xy8 xy, track MazeEntry/Sequence+Colour, ResearchItem RawValue.
    - TD46.h ×1: maze element `all`/mazeEntry.
  - In OpenRCT2's own in-memory types (8):
    - Ride.h reliability, Vehicle.h ×2 (track_progress, SwingPosition), Peep.h
      timeToSitdown/currentCar/currentSeat, Research.h rawValue, TrackElement.h uRide/uMaze,
      PathElement.h, MapHelpers.cpp.
    - These are only a problem where the code writes one view and reads or saves the other. Example:
      Peep `currentCar`/`currentSeat` written as bytes, saved as u16 `timeToSitdown`. The Amiga PR
      reorders the byte members on BE (Peep, Guest, Vehicle, Ride, Research, Marketing,
      ScenarioObjective, Widget: about 90 lines).
  - Hardest case: the boat-hire `TrackTypeAndDirection`/`BoatLocation` union at Vehicle 0x36. It is an
    int16 for track vehicles and two bytes for boats. The swap needs the ride mode, and the PR handles
    it by looking it up.

### 4. (d) Pixels, colours, the software renderer, SDL

- The renderer core is **8-bit palette indices** (`PaletteIndex`, X8DrawingEngine), so it is
  byte-oriented and neutral.
- The palette goes to native `uint32_t` through `SDL_MapRGB/SDL_MapRGBA` (HardwareDisplayDrawingEngine
  :188/197), so the **32-bit texture path is correct on BE**. LightFX mixes with shifts on uint32, which
  is also fine.
- The BE bugs here are small:
  - `HardwareDisplayDrawingEngine::CopyBitsToTexture` 16-bit and 8-bit texture paths take
    `*reinterpret_cast<const uint8_t*>(&palette[i])`, the first byte. That is the low byte on LE and the
    high byte on BE. It matters only if SDL picks a 2- or 1-byte texture format. The code picks the
    *smallest* non-indexed format the renderer offers, so check what SDL's opengl renderer lists on
    nouveau. Fix: shift instead of cast, ~4 lines.
  - `Drawing.Sprite.RLE.cpp:36` (`DrawRLESpriteMagnify`, zoomed in) does
    `memcpy(&lineOffset, ..., 2)`, which is host order. The minify path at :90 assembles bytes and is
    correct. **The Amiga PR misses this one**, so zoom-in sprites would be garbage. 1 line.
  - `ImageImporter.cpp:205` writes the RLE row offsets as native `uint16_t`. It is used for PNG-sourced
    images: `sprite build`, and .parkobj objects whose images are PNG. Since the drawing code reads LE,
    images imported on BE would break. **Not in the PR.** ~5 lines.
  - g1 RLE decode, `Drawing.Sprite.cpp:1245`, already assembles bytes.
- One case that needs no fix: SDL audio, which is a separate subsystem. The WAV, CSS1.DAT and
  OpenSFX samples are tagged `AUDIO_S16LSB`, and SDL_BuildAudioCVT swaps them correctly.
- **Two audio bugs that are not in the PR:**
  - `OggAudioSource.cpp:110` calls `ov_read(..., bigendianp = SDL_BYTEORDER == SDL_BIG_ENDIAN, ...)` but
    declares `AUDIO_S16LSB` (line 79). On BE that double-swaps into noise. Fix: pass 0.
  - `FlacAudioSource.cpp` stores native int16 but declares S16LSB (line 310). Fix: S16SYS.
  - Both affect OpenMusic and title music, not the original RCT2 sounds. ~2 lines each.
- **Renderer paths in v0.5.6.** Only two remain: `softwareWithHardwareDisplay` (CPU renders 8-bit,
  uploads a 32-bit SDL texture each frame) and `openGL`.
  - OpenGL needs a **3.3 core profile** (10 shaders, all `#version 330 core`, integer textures, texture
    arrays). There is **no GL 2.1 or GLES2 path**, so the PS3 must use the software renderer
    (`DISABLE_OPENGL=ON` saves size).
  - The SDL_Renderer behind it can use SDL's own GL 2.1 "opengl" renderer or "opengles2", both fine on
    nv30. It also does the window scaling.

### 5. (e) Third-party parts

- **Scripting: QuickJS-ng 0.13.0**, vendored amalgam (`src/thirdparty/quickjs-ng`). Duktape is gone.
  Debian's `+ds` build links the system `libqjs.so.0` instead (seen in the heaptrack run), and SDL2 via
  sdl2-compat on `libSDL3`.
  - Its typed-array and DataView code is endian-aware (`is_be()`, `bswap16/32/64`).
  - I found no BE CI for it. Byte-order bugs would hit only plugins.
  - `ENABLE_SCRIPTING=OFF` removes it entirely. The PR built that way, and that's what I'd do first.
- Other vendored headers: sfl (containers), picosha2, base64, linenoise, filesystem.hpp. All are
  byte-order neutral.
- System libraries (all in Debian ppc64 already): SDL2, libpng, zlib, zstd, libzip, libcurl, OpenSSL,
  fontconfig, freetype, ICU, FLAC, vorbis, nlohmann-json, speexdsp. None has byte-order problems.
- Optional pieces to switch off on the PS3: `DISABLE_DISCORD_RPC`, `DISABLE_HTTP`,
  `DISABLE_VERSION_CHECKER`, `DISABLE_NETWORK` (multiplayer).

### 6. (f) Network and serialisation

- `DataSerialiser` (game actions, network, replays, `GameStateSnapshots`) always calls `ByteSwapBE`.
  That is "to big-endian", **correct only on an LE host**. On BE it would write LE, which is
  self-consistent but incompatible with LE peers and with replays recorded on LE.
- The PR makes `ByteSwapBE` the identity on BE hosts (6 lines in Endianness.h). It also makes
  `ChecksumStream` and FNV-1a (`Crypt.OpenRCT2.cpp`) hash LE words, so entity checksums match x86.
  `SawyerCoding.cpp` (4 `uint32_t*` casts) and the `RCT1File.cpp` checksum casts are handled too.
- With network disabled, only replays and checksums depend on this. They are still worth fixing,
  because they are the verification oracle (section 8).

### 7. Gaps in the Amiga PR (things I'd still have to write)

| Piece | Where | Size |
|---|---|---|
| TD6/TD4 track designs: raw `Read(&TD46TrackElement…)`, `&td6, 0xA3` header, TD6 scenery/entrance elements. Also T6Exporter when saving a track. Pre-built coaster designs and the track design picker use these. | rct2/T6Importer.cpp, T6Exporter.cpp, rct1/T4Importer.cpp, TD46.h | ~120–180 lines |
| RLE magnify path line offset | drawing/Drawing.Sprite.RLE.cpp:36 | 1 line |
| PNG ImageImporter row offsets | drawing/ImageImporter.cpp | ~5 lines |
| Ogg/FLAC sample order | openrct2-ui/audio/OggAudioSource.cpp, FlacAudioSource.cpp | ~4 lines |
| 16/8-bit texture copy | HardwareDisplayDrawingEngine.cpp | ~4 lines |
| scores.dat (legacy RCT2 scores import) structs | ScenarioRepository | ~10 lines, low value |
| PR assumptions specific to m68k (32-bit `int` toolchain header, 4-byte pointers, JobPool inlined, no threads). On ppc64 BE these must be undone or `#ifdef __amigaos__`-guarded: keep threads, keep ICU. | various | review, not new code |
| the rest of the 89 `memcpy` and 15 pointer-cast sites in the engine, still to review one by one | engine | review |

### 8. (g) Tests and CI

- **No BE CI upstream.** `.github/workflows/ci.yml` at v0.5.6 covers Windows (x86/x64/arm64, MinGW),
  macOS, Linux x86_64 (AppImage, Docker, clang + tests), Android and Emscripten.
- Tests: **223 gtest cases in 28 files.**
  - FormattingTests (68) would catch the Formatter bug. Also StringTest, BitSet, SawyerCoding (16),
    Crypt (8), TileElements, S6ImportExport, RideRatings, PlayTests, Pathfinding and EntityImport.
  - `Endianness.cpp` (5) asserts that `ByteSwapBE` *always* swaps, so it must be changed once
    `ByteSwapBE` becomes host-aware.
  - **ReplayTests**: parametrised over the replays bundle (`assets.json`: OpenRCT2/replays v0.0.97). It
    compares game-state checksums recorded on LE. This is the best oracle for "the simulation is
    bit-exact on BE".
- Cheap oracles we can run on our side:
  - `openrct2-cli simulate <park> <ticks>` prints the entity checksum.
  - `openrct2-cli convert X.SC6 X.park` writes a .park that should be byte-identical to the amd64
    output.
  - `openrct2-cli screenshot` with a giant screenshot gives a pixel-exact renderer check.
- All of these run headless under qemu-user in our existing ppc64 chroot (`make chroot`,
  PACKAGING.md), with no PS3 time spent. Then compare against Debian's amd64 package in a container.

### 9. RAM

Measured, not taken from reports. The setup:
- Debian sid amd64 `openrct2 0.5.6+ds-1` in a throwaway `debian:sid-slim` podman container.
- Our rct2-data (from the deb) mounted read-only.
- Software renderer, sound off, `Six Flags Magic Mountain` (a big park) and `Electric Fields` (a small
  one). Run as a non-root user.
- Logs and scripts are in `build/tmp/orct2-be/ram/notes.txt` and `ram2/`. The data copy has been
  deleted.

amd64 and ppc64 are both LP64, so pointer and struct sizes match and these numbers carry over roughly.

| Run | VmRSS | VmHWM | Notes |
|---|---|---|---|
| GUI 1280x720, Xvfb + Mesa llvmpipe, first start (cold object index) | 453–476 MB | 539–578 MB | includes libLLVM 60 MB + gallium 13 MB file pages, 79 threads (24-core host) |
| same, `MALLOC_ARENA_MAX=2`, `LP_NUM_THREADS=1`, warm caches | 394 MB | 406 MB | 1920x1080: 423 MB; 960x540: 387 MB |
| **GUI without any GL** (`SDL_VIDEODRIVER=dummy`, SDL software renderer, arena 2, warm) | **282 MB** (anon 251) | 295 MB | 1920x1080: **300 MB** |
| headless, cold | 155–197 MB | 348–371 MB | the startup peak is the object index build |
| **headless, warm**, arena 2 | **136 MB** (anon 112) | 154 MB | the simulation-only floor |

RSS stays flat over 3 minutes, with no growth.

Heaptrack (debug symbols from debian-debug) breaks down the heap peak: **242 MB in the GUI, 139 MB
headless**.
- **73.6 MB is a single static `GameState`**, allocated at start-up and zero-filled, so every page is
  touched. Most of it is the entity table `Entity_t entities[kMaxEntities]` with `kMaxEntities = 65535`
  (EntityRegistry.h:25). RCT2's own limit is 10,000 (RCTC 15,000).
- 41 MB is tile-element vectors (MapInit/S6Importer).
- The GUI adds about 100 MB on top of headless:
  - 49 MB of object images (`ImageTable::AddImage`, 110 k images; headless never loads them);
  - 23 MB in `make_unique` arrays (g1/sprite data);
  - about 26 MB more in other buffers and SDL.

For the PS3 this is the real problem:
- MemTotal there is 217.5 MiB (docs/MEMORY.md). OpenArena at 86–89 MB peak RSS already leaves only
  10–16 MB MemAvailable.
- OpenRCT2's GUI is **~280–300 MB RSS before nouveau/Mesa's own share**, more than the whole machine.
- Cheap trims I'd make in the port:
  - `kMaxEntities` 65535 → 16384 saves about 55 MB of RSS. Those pages are never used and would sit
    swapped out anyway, but they're paid at start-up.
  - `MALLOC_ARENA_MAX=2`, `multithreading = false`, render at 960x540.
  - That brings the estimate to **~210–230 MB for the game process** on the PS3. It still means
    running partly in swap (VRAM swap + disk).
- Not known: whether the hot working set (the park's objects, images in use, the tile map) fits in
  roughly 150 MB. Only a PS3 run with `STALLS=1` can tell, and that run needs the BE port first.

### 10. Speed on the PPE (estimate, nothing measured)

- Per frame, the CPU paints the 8-bit viewport (dirty rectangles only, but scrolling and an animated
  park redraw most of it). It then converts palette indices to 32-bit and uploads the texture.
  - At 1920x1080 the conversion is 2.07 M lookups and an 8.3 MB upload.
  - The upload goes through XDR/GART: M2MF XDR→VRAM is ~2 GB/s in RSX.md, so about 4 ms. SDL's
    opengl renderer does `glTexSubImage2D`.
- The simulation runs at a fixed 40 Hz; frames above that are interpolated only with "uncap FPS".
- Viewport painting can use a JobPool (the `multithreading` option, `Viewport.cpp`). The PPE has 2 SMT
  threads, so expect perhaps 1.3x, not 2x.
- Data points I could find: a Pi 3B+ (Cortex-A53, 1.4 GHz) gets 15–22 fps "playable" at an unstated
  resolution (RetroPie forum). Devs say the cost scales with resolution.
- I'd expect the in-order PPE in this branchy C++ to be in the Pi 3B+ to Pi 4 range. My guesses:
  - **~10–15 fps at native 1080p**;
  - **~25–40 fps rendering at 960x540 with `window_scale = 2`**, where SDL/GPU does the 2x upscale. That
    is the setting I'd ship.
  - Large parks (2,000+ guests) also cost simulation time on the PPE.
- It won't reach the 60 fps "light game" target at 1080p. The game itself caps the simulation at 40
  ticks per second. For a management sim the user-visible measure is stalls and 1% lows, not the
  average.

### 11. Other ports, forks, distros

- **Upstream:**
  - #16096 (2021, s390x build failure): maintainers said BE is unsupported; they'd need CI and a
    maintainer first.
  - #8091 (2018, janisozaur's first "convert endianness on read", ~48 lines): never merged, closed
    2019. FreeBSD used it on POWER in 2019. In 2020 grepwood offered to test it on a **PS3**; nothing
    came of it.
  - #23684 (2025): added the static_assert.
  - #27114 (2026-09, AmigaOS m68k): the near-complete BE work above, closed unmerged.
  - The 2018 forum thread "PowerMac OpenRCT2 possible?" ended with "patches welcome, nobody has the
    hardware".
- **Forks:** a GitHub repository search for openrct2 with powerpc, amiga, wiiu or ps3 returned nothing.
  - **No PS3 port exists**, neither GameOS homebrew nor Linux. Web searches, psx-place included, found
    none; the only PS3 mention is grepwood's unfulfilled 2020 offer.
  - The console ports are all LE: Switch homebrew (rsn8887, 2019), Android, Emscripten.
  - Original RCT2 never came to PS3 either. So a working PS3 build would be a first.
- **Distros:**
  - Debian: Build-Depends `architecture-is-little-endian` since 0.4.16+ds-1 (2024-11). Before that it
    had a list of LE architectures, since 0.3.5.1+ds-3 (2022-02), after #16096.
  - FreeBSD: `NOT_FOR_ARCHS= powerpc powerpc64 powerpcspe` "Only for little-endian architectures".
  - OpenBSD: `NOT_FOR_ARCHS= ${BE_ARCHS}`.
  - Gentoo: no ppc/ppc64 keywords.
  - Alpine: ppc64le only.
  - Void: no package found.
  - Fedora, Adélie, openSUSE and NetBSD: not checked (fetches blocked).
  - Nobody ships a BE patch.

### 12. Effort

| Step | Person-days |
|---|---|
| Rebase the PR's ~1,700-line endian core onto v0.5.6 (2 conflicts). Strip or guard the m68k-only changes. Lift the static_assert. Build with `ENABLE_SCRIPTING=OFF DISABLE_OPENGL=ON DISABLE_NETWORK=ON DISABLE_DISCORD_RPC=ON DISABLE_HTTP=ON` in the ppc64 chroot (qemu-user; a cold C++20 build of ~700 k lines is likely 1–2 h there) | 1.5–2 |
| Fill the gaps in section 7 (TD6/TD4 import and export, RLE magnify, ImageImporter, Ogg/FLAC, 16-bit path); fix the Endianness test | 1.5–2 |
| Verify headless under qemu: gtest suite, ReplayTests, `simulate` checksums and `convert` byte-compare on all 26 RCT2 scenarios in our data plus `screenshot` pixel-compare against amd64 Debian 0.5.6. Fix what falls out; I expect a handful of missed sites. | 2–4 |
| Packaging: our deb recipe (Debian 0.5.6+ds source, our patch series, drop `architecture-is-little-endian`, depend on rct2-data; the openrct2-data arch-all part is reusable). Bench recipe on the PS3 (software renderer, window_scale 2, SDL GL renderer), webcam check, RAM and stall measurement. | 2–3 |
| **Total** | **7–11 person-days** to "plays correctly, measured" |

Scripting and plugins later: +1–2 days with a QuickJS-ng test pass. Multiplayer: not worth it.

Lines touched, all told: **~1,900–2,100 added lines in ~40 files**, most of it reused from #27114.
Written from scratch it would be about 3–4 weeks. The PR's S6 and S4 per-offset swap tables (934 lines)
were the bulk of the work, and those are done.

### 13. Risks (biggest first)

1. **RAM.** The GUI process is 282–300 MB RSS measured on amd64 (no GL), about 210–230 MB after trims, against 217.5 MiB MemTotal. See section 9.
2. **Silent simulation divergence**: a missed field gives a park that loads and looks right but where
   one value is byte-swapped (a ride's reliability, a peep's car or seat, the boat-hire union). The
   replay and checksum oracles under qemu are the defence, so do them before any PS3 time.
3. **The AI-generated PR is unreviewed upstream.** Its offset tables were written by hand against the
   documented offsets. It matched checksums on the author's tests, which is not all 26 of ours plus the 169 TD6 designs.
   And maintaining a ~2,000-line out-of-tree patch across OpenRCT2's fast release cadence (0.5.x
   monthly) means a rebase per release. Better to pin to 0.5.6.
4. **Speed**: 1080p native is too slow; plan on 960x540 ×2.
5. Track designs (TD6) are uncovered by the PR, and the user will notice them fast in ride construction (our rct2-data has 169 .TD6 designs).

### 14. Verdict

**Worth a build, staged, with RAM as the go/no-go.**

It is not "try now": there is no ppc64 binary anywhere, and it needs our own patch series. The
byte-order side is cheap for what it is. The hard part, the S6/S4 per-offset swaps, the Orca/IStream
choke points and the checksums, already exists as GPL-3 code from #27114 and applies to v0.5.6 with 2
conflicts. That leaves 7–11 person-days to a measured PS3 run.

The thing that can still kill it is memory. The game process measures 282–300 MB RSS on amd64 before
any GL driver, on a machine with 217.5 MiB MemTotal. With the trims in section 9 it is about
210–230 MB. So:

1. **Milestone 1 (~4–5 days):** the BE patch, the gap fixes and the RAM trims (`kMaxEntities`, arena,
   no JobPool), all verified headless under qemu-user against amd64. That means gtest, ReplayTests,
   `simulate` checksums and `convert` byte-compares on our 26 scenarios, plus `screenshot` compares.
   No PS3 time.
2. **Milestone 2 (~1 day of PS3 time):** one supervised run, desktop stopped, `STALLS=1`, 960x540
   with `window_scale = 2`. Webcam check, stalls, swap-ins, MemAvailable. If swap stalls dominate,
   stop there and record it as "RAM-bound". If it holds, finish packaging and the bench recipe.

An optional pre-filter before milestone 1, about 1 hour, still not a benchmark: run amd64 OpenRCT2 in
a container with `--memory=180m` and swap allowed, and count major faults per second while playing.
That estimates the hot working set. If it thrashes there, it will thrash on the PS3, and you can skip
the port.

Novelty: no PS3 port exists, neither GameOS homebrew nor Linux; the only mention is an unfulfilled 2020
offer to test on one. It would be the first OpenRCT2 on a PS3, and the first on any big-endian Linux
since the 2019 FreeBSD/POWER build of 0.2.1.
