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
- [0003](packages/yquake2/patches/0003-yq2.cfg-W-A-S-D-and-a-game-controller-s-buttons-by-d.patch)
  the default bindings (`yq2.cfg`, run after id's `default.cfg` and before the player's `config.cfg`):
  W/A/S/D instead of Yamagi's E/S/D/F (the power shield from W to E), and a game controller's buttons,
  which Yamagi left unbound (the sticks moved and looked, nothing fired): R2 fires, L2 walks, Cross
  jumps, Circle crouches, L1/R1 the previous/next weapon, Square uses the item, Triangle the inventory,
  the D-pad left/right selects an item, up the help computer, down drops the item, Select the score;
  Start is the menu already. A saved `config.cfg` keeps its own bindings (Yamagi writes them all), so a
  home that has played keeps E/S/D/F until Options, Customize controls, "reset to defaults". State:
  built for the PS3 test (pending).
- Two menu entries of ours ([desktop/launchers](desktop/launchers/)), the gl1 renderer fullscreen at the
  desktop's resolution (`r_mode -2`): "Quake II (demo)", `-datadir /usr/share/games/quake2-demo`, and
  "Quake II", the full game, `-datadir /usr/share/games/quake2`. Both are packed hidden (`NoDisplay=true`)
  with `X-Walnut-Requires=` their data's `baseq2/pak0.pak` (`/usr/share/games/quake2-demo/...`,
  `/usr/share/games/quake2/...`): ps3-game-menu shows each only when that file exists. The full game's data is game-data-packager's `quake2-full-data` (build.toml `[data_setup]`).
- **The data** (licensed): `quake2-demo-data` (`/usr/share/games/quake2-demo/baseq2/`, the demo's
  `baseq2` with `demos/q2demo1.dm2` in its pak), built once with game-data-packager 89 (`make game-data`,
  the demo archive downloaded from id's mirrors and kept in `build/downloads/game-data`) as
  `quake2-demo-data_89_all.deb`, Section `local/games`, and published once to the licensed suite. Quake
  II's demo may not be redistributed at all. game-data-packager's output isn't reproducible: the version
  89 file the LAN repository published is in the retirement backup's `ps3-licensed-debs/`, and `make
  publish DEBS="<that file>"` before the next `make game-data` keeps it the same. The full game needs the
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
  draws smooth points with a variant of the bound fragment program (ps3.874: round on the PS3). Then
  every particle was a 40-pixel disc at any distance: nv30 never set `POINT_PARAMS_ENABLE`, so the
  RSX ignored the attenuated size Quake II asks for (`gl1_pointparameters 1`). Mesa 0051 with ps3rsx
  PS3_ABI 2 fixes it, checked with dev builds on 2026-10-27; the release check is docs/TESTS.md's
  batch mesa-0051.
- Movement shipped bound to E/S/D/F (Yamagi's defaults) and the pad's buttons unbound: 0003 gives W/A/S/D
  and a pad scheme to new homes (pending its PS3 test). The menu and HUD text is small at 1080p (the
  automatic scale is 3 there: 1920/640).
- `Sys_LoadLibrary failed: libcurl.so.4`: the optional download library, harmless.

## Investigations

- [2026-10-16 OpenAL white noise](investigations/2026-10-16-openal-white-noise.md): little-endian WAV
  samples played as noise (0001).
- [2026-10-18 the PRNG table](investigations/2026-10-18-prng-table.md): 32 MB of random-number
  table and 22.6 s of stalls (0002).
- [2026-10-26 point smooth](investigations/2026-10-26-point-smooth.md): square particles (Mesa 0050).
- [2026-10-27 point size attenuation](investigations/2026-10-27-point-size-attenuation.md): particles
  of one size at every distance (Mesa 0051, ps3rsx PS3_ABI 2).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": a strong candidate with its `gl1` renderer: light and portable.
