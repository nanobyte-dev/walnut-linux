# Sauerbraten: technical notes

The real game: Sauerbraten's 2020 Edition release with its maps, models, sounds and music, on the same
[Cube 2 engine build](../../engines/cube2/technical.md) that runs [BananaBread](../bananabread/technical.md).
Debian's `cube2` is Sauerbraten's own engine source, so no second engine package was made (building the
release's source again would give the same program without the patches); what was missing was the
launcher and the data. It plays right, but its big maps fill the 250 MB of VRAM, and both played sessions
ended in a crash in Mesa when that happened. Those two crashes are fixed (Mesa 0046, then 0047 and 0048);
a session with all three is still to be played.

## Packages and settings

| Package | Suite | What |
|---|---|---|
| `sauerbraten-walnut` ([packages/sauerbraten-walnut](packages/sauerbraten-walnut/)) | public (testing, stable; CI builds it, `0.N`; up to 0.874 it was named `sauerbraten`, Debian's non-free package's name, and now Conflicts/Replaces it) | `/usr/games/sauerbraten`, the menu entry (Name `Sauerbraten`), an icon. Depends on `cube2`, Suggests `sauerbraten-data` |
| `sauerbraten-data` (`licensed/sauerbraten-data`) | the licensed suite only, built locally (`0.0.20201229-1+ps3.N`, about 980 MB) | the release's `data/` and `packages/` whole, in `/usr/share/games/sauerbraten`. Depends on `sauerbraten`, so installing the data installs the game. Install by name (no `[data_setup]` yet) |

- The launcher runs `cube2_client -q$HOME/.sauerbraten -k/usr/share/sauerbraten/walnut
  -k/usr/share/games/sauerbraten -t1`: fullscreen at the desktop's resolution (the engine sizes its window
  from the desktop when `scr_w`/`scr_h` are unset; Debian's build defaults to a window, hence `-t1`),
  settings apart from BananaBread's `~/.cube2`, no user file written. `SAUERBRATEN_WINDOWED=1` starts in
  a window.
- **Walnut's defaults**: the first package directory holds only `data/defaults.cfg`
  ([files/walnut](packages/sauerbraten-walnut/files/walnut/)), which the engine runs only while
  `~/.sauerbraten/config.cfg` does not exist. It runs the release's own defaults.cfg, then sets
  `texcompress 512`: of the release's 2155 world texture files, the 512-pixel ones are 772 files and 76%
  of the bytes, which `texcompress 1024` leaves plain and 512 stores in an eighth (DXT1) or a quarter
  (DXT5). The game saves it into config.cfg at the first quit and the player's settings rule from then
  on (`/texcompress 1024` restores the engine's value). Unmeasured on the real game yet: the next
  session's VRAM column decides whether it stays.
- `sauerbraten-data` is a second package beside BananaBread's `sauerbraten-nonfree-data`: the real game
  needs the maps, textures and music too, in its own directory so neither game sees the other's files.
  The orig tarball is made from the pinned release archive by its `sauerbraten-full-data.sh` (`make
  sauerbraten-data-src`), never committed; mixed, partly non-commercial licences, so it is licensed (no CI workflow).
- Endianness: nothing to fix. The engine already runs big-endian (the maps read through `lilswap`), and
  the release's maps and models use the same formats.
- All of the engine's patches apply (0001-0010; 0011 `vanormalshort` built in and off).

## How it performs

Two played sessions, both crashes, both on the online and bot maps that fill the VRAM:

- 2026-10-24 (an online insta-CTF match on eternal_valley, then berlin_wall): 54 fps median, 14.7 s of
  stutter a minute, a segfault after 202 s of play in `nv30_query_object_del`: nv30's 124 report slots
  were taken back from queries that still pointed at them (the engine ends up to 2048 occlusion queries a
  frame and reads them a frame later), which also gave queries each other's results (geometry popping,
  the user's "a bit glitchy"). Fixed by Mesa 0046.
- 2026-10-25 (bots, about 6 minutes, Mesa ps3.819 with 0046): 38 fps median; no query crash, but a write
  through NULL in libc at 400 s, after three `out of GART for a 4096 KiB BO` lines: Mesa's
  `st_setup_current()` copied the current vertex attributes into a `u_upload_alloc()` result it never
  checked. Fixed by Mesa 0047 and 0048 (Mesa ps3.827).

What the sessions measured about memory: cube2_client held 180 MB of VRAM on eternal_valley and 210 MB
after the map change (probably the previous map's textures still loaded), Xorg 27-35 MB: the whole
250 MB. The VRAM swap gave all 96 of its chunks back as designed, so its pages went to the disk;
MemAvailable 22-45 MB. The 30 MB of GPU-only VRAM held 132 KiB: on the PS3 every nv30 texture asks for
CPU-visible VRAM. The second session ran with the player's own config.cfg, so `texcompress 512` did not
apply. The crash analysis is in [the investigation](investigations/2026-10-24-online-crashes.md).

The benchmark ([recipe](benchmark/recipe.sh), variants `complex`, `metl3`, `lean`, `gpuskel`) was called
`sauerbraten-edition` before 2026-10-26 (`run.sh` still takes the old name); the played test
([rating/test.toml](rating/test.toml)) is the launcher alone, its `play_mark` the `read map` line. Its PC
reference (`tools/play-pc.sh`) installs the entry's packages from Debian, which has no `sauerbraten`: use
`PC=0 play-test.sh start sauerbraten` until the PC image gets our two debs.

## Known problems

- **VRAM runs full on the big maps.** New installs compress more textures (`texcompress 512`); an
  existing config keeps its own setting. A launcher-environment alternative that works with any config
  is Mesa's `NV30_FORCE_DXT=opaque` (BananaBread: -40% VRAM, softer textures,
  gamedb:batch/2026-10-19-ssdroot2-force-dxt): the user agreed to it as the launcher's default on
  2026-10-26; not set yet, it comes with its own session after the crash fixes' (todo.toml).
- **`out of VRAM for a 8100 KiB BO` bursts** (135 failures each) come 1.4-1.5 s after each of the bench's
  `xwd` screenshots when the VRAM is full: X's GetImage answers in bands of 8 lines (135 = 1080 / 8) and
  each band asks for a screen-sized buffer (most likely glamor's readback copy), which fails and falls
  back. They are Xorg's, only while a screenshot of a full VRAM is taken, and not the game's.
- Not done: render targets and depth buffers in the GPU-only 30 MB. nv30 maps every miptree CPU-visible
  because `nv30_transfer_rect_cpu` may map it; a buffer never read back by the CPU could go there (8 MB
  each at 1080p), but whether every transfer path avoids the CPU map is unproven.

## Investigations

- [2026-10-24 the online crashes](investigations/2026-10-24-online-crashes.md): the query slots (Mesa
  0046) and the NULL upload buffer after GART ran out (Mesa 0047, 0048).
- The engine's ([Cube 2](../../engines/cube2/technical.md#investigations)).

## Research


What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": "Sauerbraten (Cube 2)" a strong candidate: a modest engine with a built-in map editor; a step up in GPU load.
