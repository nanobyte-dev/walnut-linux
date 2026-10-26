# Nexuiz Classic: technical notes

## Status

Nexuiz Classic 2.5.2 runs on our [DarkPlaces](../../engines/darkplaces/technical.md) build `darkplaces-gl2`, the
last DarkPlaces with the GL 1.x/2.0 renderpaths, with Debian's unchanged `nexuiz-data` and `nexuiz-textures`
2.5.2-14 (764 MB; `nexuiz-music` left out). Built, not installed on the PS3: `darkplaces-gl2`, `nexuiz-data`
and `nexuiz-textures` are about 800 MB, which the user decides.

- `/usr/games/nexuiz-gl2` is Debian's wrapper (`-nexuiz -basedir /usr/share/games/nexuiz`), then
  `nexuiz-lean.cfg`: `vid_gl20 0`, `gl_picmip 2`, no realtime lighting, bloom or water,
  `r_texture_dds_save 1` (a compressed texture cache in `~/.nexuiz`), mono 22.05 kHz sound, no decals.
  textures.pk3 is 522 MB, read on demand.
- Recipe ([benchmark/recipe.sh](benchmark/recipe.sh), `SUITE=0`): the engine's own `-benchmark
  demos/bench1.dem` with `-benchmarkruns N`, which prints "N frames S seconds F fps" per pass. Order on the
  PS3: base (the combiner path), then `gl20` and `hires`; also `stereo`, `nosound`, `bench` (every pass
  counted, as the Quake II recipe).
- Risks, in order: the GL 2.0 path's shaders on nv30, VRAM and RAM with Nexuiz's maps, the sound's byte order
  (SDL, 16-bit; the Quake II OpenAL noise was such a bug), the CPU cost of the generic skeletal code.
