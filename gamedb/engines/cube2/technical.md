# Cube 2: technical notes

Debian's `cube2` (`0.0.20201229+dfsg-1+ps3.N`, [packages/cube2](packages/cube2/)) is Sauerbraten's own
engine source (svn at the 2020-12-29 release, minus the bundled enet and the IDE files; the release
archive's `src/` differs from it in two files, `sound.cpp` and `stream.cpp`, where Debian's has later
upstream fixes). Only Debian's *data*, `cube2-data`, is BananaBread's. So one engine build with one set of
patches runs both [BananaBread](../../games/bananabread/technical.md) and
[Sauerbraten](../../games/sauerbraten/technical.md), and [Red Eclipse](../../games/redeclipse/technical.md)
is the same engine family without our patches. The package replaces Debian's; the release candidate is
ps3.354, ps3.442~dev added 0011.

## Patches

| Patch | What | Switch |
|---|---|---|
| [0001](packages/cube2/patches/0001-Compress-textures-at-upload-on-Mesa-too-texcompress.patch) | textures compressed at upload on Mesa too: upstream turns it off on Mesa (`if(!mesa) usetexcompress = 2` in `rendergl.cpp`), so only DDS files went in compressed | `texcompress` (default: textures of 1024 and up) |
| [0002](packages/cube2/patches/0002-CPU-skinning-by-default-and-no-GPU-skinning-shaders-.patch) | CPU skinning by default and no GPU skinning shaders built: nv30 has no indirect constant addressing, so the bone-array vertex shader becomes if/else trees (11 fps on the GPU against 28 on the CPU) | `gpuskel 0` |
| [0003](packages/cube2/patches/0003-Build-only-the-world-shader-variants-that-can-be-dra.patch) | only the world shader variants that can be drawn (dynamic lights and water fade only as far as `maxdynlights`, `waterrefract`, `waterfade` ask) | |
| [0004](packages/cube2/patches/0004-Sky-boxes-and-cube-maps-from-their-.dds-and-one-side.patch) | sky boxes and cube maps from their 256 px .dds instead of six 2048 px PNGs decoded twice (the likely load-time OOM) | `skydds 1` |
| [0005](packages/cube2/patches/0005-Free-the-lightmaps-CPU-copies-once-uploaded.patch) | the lightmaps' CPU copies freed once uploaded | `lmfreedata 1` |
| [0006](packages/cube2/patches/0006-CPU-skinning-through-libspeq-on-the-PS3-s-SPEs.patch) | CPU skinning through libspeq on the SPEs (dlopen; the engine's own loop when libspeq is missing, declines or is late) | `speqskin 0`; `SPEQ_SKIN` |
| 0007-0009 | fewer GL calls: uniforms sent only when changed (`gluniformcache`), a batch's textures bound together (`glbatchtmu`), particle vertices uploaded only by renderers with particles (`glpartvbo`); cave 1657 to 1300 GL calls a frame on llvmpipe, same pixels | variants `nouni`, `notmu`, `nopart`, `allbatchoff` |
| [0010](packages/cube2/patches/0010-Water-occlusion-queries-without-waiting-for-the-GPU-.patch) | water occlusion queries without waiting for the GPU | `oqwaterlag 1` (0 as before) |
| [0011](packages/cube2/patches/0011-World-normals-and-tangents-as-16-bit-signed-normaliz.patch) | world normals and tangents as 16-bit signed normalized (`svertex`, 40 bytes), which nv30 fetches, instead of GL_BYTE that u_vbuf converts at every world draw | `vanormalshort 1` (default 0) |

0011's default stays 0 because the shading differs slightly: s/32767 can't equal b/127 except at 0 and
+-1, so about 170 of 518,400 pixels differ by at most 2/255 on llvmpipe; cave's world VBOs grow from 4.40
to 5.50 MB. On the PS3 it gave BananaBread +17% (38.8 to 45.1, gamedb:batch/2026-10-18-batch17). The
decision is open. cube2 0007-0009 have no PS3 run yet.

## Investigations

- [2026-10-17 skeletal models skinned on the SPEs](investigations/2026-10-17-spu-skinning.md) (0002, 0006).
- [2026-10-18 the water query wait](investigations/2026-10-18-water-query-wait.md) (0010, Mesa 0035).
- [2026-10-18 the world's normals as signed bytes](investigations/2026-10-18-byte-normals.md) (0011, Mesa
  0033, 0037, 0043).
