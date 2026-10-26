# Yamagi Quake II: technical notes

Our build of Debian's `yquake2` 8.70 on its gl1 renderer, with the Quake II demo data. It plays the demo
pak's timedemo at 68.7 steady fps with almost no stalls (gamedb:batch/2026-10-18-mesa-407-lut) since its
random-number table shrank from 32 MB to 64 KB, and its played session had a 60 fps median, a 1% low of
33, no dropouts and no crash.

## Packages, data and settings

- `yamagi-quake2`, `yamagi-quake2-core` (`/usr/lib/yamagi-quake2/quake2`, renderers `ref_gl1` and
  `ref_gl3`), [packages/yquake2](packages/yquake2/), `COMPONENT=contrib`: debian-ports builds nothing
  arch-specific in contrib, so Debian has no ppc64 binary. The gl1 renderer (GL 1.4 fixed function) is
  the one that runs; gl3 and gles3 need GL 3.
- [0001](packages/yquake2/patches/0001-Sound-swap-16-bit-WAV-samples-to-host-byte-order-on-.patch)
  swaps the 16-bit WAV samples to host byte order at load: uploaded little-endian, they played as white
  noise through OpenAL ([the investigation](investigations/2026-10-16-openal-white-noise.md)).
- [0002](packages/yquake2/patches/0002-KISS-PRNG-lag-table-size-from-YQ2_KISS_QSIZE-default.patch)
  `YQ2_KISS_QSIZE`: the KISS PRNG's lag table, allocated at first use with 4096 entries by default
  instead of a static 16 MiB array in both `quake2` and `game.so`
  ([the investigation](investigations/2026-10-18-prng-table.md)). Its upstream offer is drafted in
  [upstream.md](upstream.md).
- One menu entry of ours, "Quake II (demo)" ([desktop/launchers](desktop/launchers/)): the gl1 renderer,
  fullscreen at the desktop's resolution (`r_mode -2`), `-datadir /usr/share/games/quake2-demo`.
- **The data** (licensed): `quake2-demo-data` (`/usr/share/games/quake2-demo/baseq2/`), built once with
  game-data-packager (`make game-data`) and published once to the licensed suite. The full game needs the
  player's own Quake II data (the data guide on the game's page).
- The config lives in `~/.local/share/YamagiQ2/baseq2/config.cfg` (`~/.yq2` before the XDG build).

## How it performs

- Bench (gamedb:batch/2026-10-18-mesa-407-lut, same boot, x2): `kiss4k` (now the default) against the old
  table: RSS 125 to 56 MB, swap used 80 to 9 MB, gameplay stalls 22.6 to 0.05 s, longest frame 4.5 s to
  91 ms, steady 65.7 to 68.7, 1% low 20.1 to 31.1, 46 MB free at the lowest (12 before), 48 MB of VRAM,
  no underruns.
- How it got there: first runs on 2026-10-17 gave 56.1 fps with OpenAL, 58.4 with SDL audio; Mesa ps3.246
  51.4, ps3.262 63.9 (+14-24%, gamedb:batch/2026-10-17-mesa-262).

The recipe ([benchmark/recipe.sh](benchmark/recipe.sh)) runs the engine directly (Debian's `quake2`
wrapper is contrib, not ours) on the demo pak's only demo, `+timedemo 1 +demomap q2demo1.dm2`, vsync
off. The timedemo repeats in one session: autoexec.cfg's `bench` alias sets `nextdemo` again,
disconnects (which prints the pass's line; a chained demomap would reconnect and clear the counters) and
starts the demo cold. Each pass also prints "ERROR: Disconnected from server". Variants: `sdlsound`,
`kissbig`, `kiss4k`, the OpenAL Soft variants.

## Known problems

- Particles (blood, sparks, bullet trails) drew as squares: nv30 ignored `GL_POINT_SMOOTH`. Mesa 0050
  draws smooth points with a variant of the bound fragment program; its first build (ps3.860) segfaulted
  at Quake II's first particles (a program variant never uploaded) and was revised (ps3.863); the PS3
  check is pending (docs/TESTS.md, batch mesa-0050).
- Movement ships bound to E/S/D/F (Yamagi's defaults), not W/A/S/D; the player rebinds it in Options,
  Customize controls. The menu and HUD text is small at 1080p.
- `Sys_LoadLibrary failed: libcurl.so.4`: the optional download library, harmless.

## Investigations

- [2026-10-16 OpenAL white noise](investigations/2026-10-16-openal-white-noise.md): little-endian WAV
  samples played as noise (0001).
- [2026-10-18 the PRNG table](investigations/2026-10-18-prng-table.md): 32 MB of random-number
  table and 22.6 s of stalls (0002).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": a strong candidate with its `gl1` renderer: light and portable.
