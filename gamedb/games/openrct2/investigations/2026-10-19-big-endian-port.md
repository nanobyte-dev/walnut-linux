+++
id = "big-endian-port"
games = ["openrct2"]
driver_patches = []
game_patches = ["openrct2/0001", "openrct2/0002", "openrct2/0003", "openrct2/0004", "openrct2/0005", "openrct2/0006"]
sessions = []
batches = ["2026-10-19-ssdroot8-openrct2"]
+++

# OpenRCT2: the big-endian port, milestone 1 (2026-10-19)

The port followed [the estimate](2026-10-19-big-endian-port-estimate.md) of the same day. Upstream refuses
big-endian on purpose (a `static_assert` in `Platform.h`), so this is our own out-of-tree series.

**Starting point.** The closed PR #27114, "AmigaOS 3.x (m68k, big-endian) port [RFC]" (Thomas Luebker, 43
commits, 2026-09-10/12, head a5dbca57c5be, closed unmerged as too invasive and AI-generated), contributed to
OpenRCT2 and so GPL-3.0-or-later like the rest of the tree. Its endian core was applied on v0.5.6 (82b30979)
with 2 conflicts resolved (`DataSerialiserTraits.h`, `SawyerCoding.cpp`) and cleaned of the Amiga parts. The
work was done in a cross build (g++-16 for powerpc64 against a ppc64 sysroot) with the ppc64 binaries run
under qemu-ppc64-static, next to a native amd64 build of the same tree.

**Bugs found beyond the PR** (each fixed in the series):

- the PR's `RCT12ResearchItem`: `RawValue` swapped but the byte members read in little-endian order, so the
  research lists were garbage on big-endian;
- the PR's maze-only swap of RCT12 track bytes 5/6: the importer reads Colour/Sequence, which alias
  MazeEntry, so the track's flags2 differed on mazes (bpb.sv6, BigMapTest.sv6); fixed by host-ordered members
  and an unconditional swap;
- the PR's park TILES swap was Amiga-only (`#ifdef __amigaos__`) and keyed mazes on a rideType heuristic:
  replaced by `TileElement::LittleEndianToHost` with host-ordered union bytes (path additionStatus, track
  sequence);
- `CSStringConverter`: the bsearch key was read as the first 2 bytes of a `wchar_t` (the high half on
  big-endian), so RCT2's accented characters were not converted;
- TD6/TD4/TD7 track designs, scores.dat, the file index header, the sprite tool (the g2.dat build), the chunk
  writer header, the Sawyer and RCT1 checksums, the RLE magnify row offsets, the PNG importer's offsets, the
  Ogg/FLAC formats, the 16/8-bit texture path, and coordinate structs written raw by the DataSerialiser (the
  track design actions);
- the build: `DISABLE_HTTP` misses `<future>`; ScriptingTests were built without scripting.

**Checks, big-endian (ppc64 under qemu) against little-endian (amd64), the same source:**

- gtest 275/275 on both, including the 19 ReplayTests recorded on x86 (entity checksums tick by tick);
- g2.dat, fonts.dat, palettes.dat and tracks.dat built by the big-endian `sprite build`: byte-identical;
- the oracle: every RCT2 scenario (26 SC6 in our rct2-data) and 9 test parks (SV6 and .park): load, simulate,
  save an uncompressed .park, compare the entity checksum and every chunk except authoring: 100 ticks 35/35,
  5000 ticks 35/35, 10000 ticks with the park opened (guests in 28 of them, up to 2782) 35/35. Determinism
  needs `OPENRCT2_FIXED_SEED` (ScenarioBegin seeds from the clock) and `SOURCE_DATE_EPOCH` (authoring
  timestamps), both hooks in patch 0003;
- the renderer: 210/210 screenshots identical (giant zoom 2, rotations 0-3; 1920x1080 at zoom 0 rotation 0 and
  zoom 1 rotation 2) of the 35 parks after 10000 open-park ticks;
- track designs: all 169 TD6 in rct2-data imported, serialised (the network and replay form) and exported back
  to TD6 on both hosts: identical. This found patch 0006: the object entry checksum went into the serialiser in
  host order, and coordinate structs carried their (uninitialised) padding;
- a DWARF check of the S6/S4 swap tables: a dev-only test runs `ByteSwapS6Data`/`ByteSwapS4Data` on a pattern
  buffer (byte i = i*7+3, so 2-, 4- and 8-byte reversals are unambiguous), gdb dumps the layouts from DWARF, a
  script compares. S6: 342159 multi-byte fields outside unions, none missing or of the wrong width, no stray
  swaps (not swapped by design: the header's magic, object count and version, swapped earlier, and
  `Rides[].pad002`, never read). S4: none missing. The unions were reviewed against what the importers read;
- the package: the debs installed in a ppc64 chroot under qemu, Electric Fields after 3000 open-park ticks
  gives the amd64 park chunk for chunk (only authoring differs: the version string), checksum
  20994dc4a8b051a1.

Not covered: the zoomed-in (magnify) RLE path (the CLI screenshot can't take a negative zoom), SDL texture
formats other than 32-bit, audio (no device in the container: the Ogg/FLAC formats were fixed by reading; the
sound is for the user's ear on the PS3), RCT1 data (S4/TD4 checked only by the DWARF swap check), the network,
scripting (built off).

**RAM on the PC** (the amd64 build of the same tree under Xvfb at 1920x1080, the SDL software renderer with
`SDL_FRAMEBUFFER_ACCELERATION=0`, RSS median over 120 s after 60 s, the open-park saves after 10000 ticks):

| Park | Case | RSS | anon | HWM | threads |
|---|---|---|---|---|---|
| Six Flags Magic Mountain (2782 guests) | 1920x1080 | 309 | 276 | 335 | 29 |
| same | 960x540 x2 | 300 | 267 | 319 | 29 |
| same | lean (x2, sound off, multithreading off, `MALLOC_ARENA_MAX=1`) | 260 | 228 | 282 | 5 |
| Electric Fields (small) | 1920x1080 | 264 | 233 | 286 | 29 |
| same | 960x540 x2 | 253 | 221 | 269 | 29 |
| same | lean | 232 | 201 | 248 | 5 |

With SDL's GL renderer on llvmpipe: +140 MB (402-451 MB; SDL3 under sdl2-compat otherwise puts llvmpipe behind
the window surface). The smaps of the lean Magic Mountain run: heap 113 MB (tile elements, objects, strings),
70 MB in one block, GameState_t (73.6 MB: the EntityRegistry 57.7 MB = 65535 x Entity_t of 512 B plus per-entity
arrays), 21 + 15 MB (object images, g1 data), file-backed 25 MB. The memory knobs, biggest first:
`kMaxEntities` 65535 to 16384 (about -43 MB; RCT2 parks hold up to 10000; needs the oracle re-run), the GL
renderer out of the process (`sdlsw`), sound off (-10 to -35 MB), one thread and one malloc arena (-25 MB),
960x540 x2 (-9 to -11 MB), g1.dat mmap'd instead of read into the heap (17 MB), and smaller parks (Electric
Fields -40 MB). The PS3 numbers are in the [technical notes](../technical.md#on-the-ps3-batch-ssdroot8).
