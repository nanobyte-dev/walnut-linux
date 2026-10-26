# Warzone 2100: technical notes

Warzone 2100 4.3.5, the last release with a GL 2.1 renderer, is our package, and it runs on the PS3 with the
right picture and sound after a set of big-endian, AltiVec and driver fixes. Its played session
(2026-10-25, a skirmish) had a 38 fps median and 5.4 s of stutter a minute. As a console game it is a poor
fit: mouse and keyboard only, a sparse 1999 interface, small at 1080p (Options > Video > Display Scale
helps). Stable `warzone2100` 4.3.5-1+ps3.644; the user played it next to the PC build on 2026-10-20.

## Packages and patches

`warzone2100`, `warzone2100-data`, `warzone2100-music` ([packages/warzone2100](packages/warzone2100/));
in no suite (deployed by name). The recipe's header has the settings and the PC's RAM numbers.

| Patch | What |
|---|---|
| [0001](packages/warzone2100/patches/0001-wzmaplib-include-cstdint-for-gcc-16.patch) | wzmaplib includes `<cstdint>` (gcc 16's libstdc++ no longer pulls it in) |
| [0002](packages/warzone2100/patches/0002-autogame-WZ_AUTOGAME_SPEED-sets-the-game-speed.patch) | `--autogame`: `WZ_AUTOGAME_SPEED` sets the game speed (default stays 500x) |
| [0003](packages/warzone2100/patches/0003-terrain-transparent-decal-colour-by-bytes-not-an-endia.patch) | the terrain's transparent decal colour was an endian-dependent literal |
| [0004](packages/warzone2100/patches/0004-etcpak-DXT1-DXT5-blocks-right-on-big-endian-hosts.patch) | the game's own DXT encoder (etcpak) writes right blocks on big-endian hosts |
| [0005](packages/warzone2100/patches/0005-radar-overlay-colour-by-bytes-not-by-shifts.patch) | the radar overlay colour back from a UDWORD by bytes, not by shifts |
| [0006](packages/warzone2100/patches/0006-gl-unbind-GL_TEXTURE_2D_ARRAY-only-when-texture-arrays-exist.patch) | GL_TEXTURE_2D_ARRAY unbound only when texture arrays exist |
| [0007](packages/warzone2100/patches/0007-stb_image_resize-no-loop-vectorization-on-AltiVec-without-VSX.patch) | stb_image_resize built without loop vectorization when the target has AltiVec but no VSX: GCC 16.2 miscompiles it at `-O3 -mcpu=cell` |
| [0008](packages/warzone2100/patches/0008-oggopus-native-endian-samples-for-OpenAL.patch) | the Opus decoder's samples in native byte order for OpenAL |
| [0009](packages/warzone2100/patches/0009-gl-GLSL-1.20-texture-lookups-without-a-bias-argument.patch) | GLSL 1.20 texture lookups without a bias argument (the RSX's TXB samples wrong), so the game works on a Mesa without 0045 |
| [0010](packages/warzone2100/patches/0010-texture-size-512-by-default-on-the-Cell-build.patch) | texture size 512 by default on the Cell build: the game's 2048 ran the RSX out of VRAM on a first start (GL_OUT_OF_MEMORY, no grass) |

## How it performs

- Played (2026-10-25): 38 fps median, 1% low 17, 5.4 s of stutter a minute, clean sound (1.8 faults a
  minute); the picture as the game looks (no PC copy: our 4.3.5 build).
- Smoke runs (2026-10-20): the map load ran from 43 s to 80 s after the start (`gameLoad` to the replay
  file in game.log), so screenshots need to be taken after about 90 s; the recipe counts its shot points
  from `Started writing replay file` (`SHOT_RE`).

## Known problems

- Mouse and keyboard only; the interface is small at 1080p.
- Textures are limited to 512 pixels (0010).
- The LOD distance option does nothing on GLSL 1.20 (0009 drops the bias whatever the setting).

## Traps for the checks

- **Mesa's software S3TC decoding is wrong on big-endian** (llvmpipe and softpipe under qemu: green comes back
  0, so DXT textures look magenta), although the game's DXT blocks are byte-identical to the amd64 build's.
  Under qemu use `texturecompression=0`; the RSX decodes DXT in hardware.
- **The team colours change from run to run.** The skirmish lobby shuffles them with `rand()`
  (`resetPlayerConfiguration` in multiint.cpp, seeded with the time) unless the config has `colourMP`
  (default -1, random), and the skirmish json has no colour key. A yellow base next to a pink one on a PS3 |
  PC sheet is not an endianness bug; the recipe pins player 0 to pink.
- The loading screen is the map preview, a tall column in the middle with the start positions and a row of
  white "stars" along the bottom: not a broken 3D view.

## Investigations

- [2026-10-20 the black 3D view: etcpak and a miscompile](investigations/2026-10-20-black-3d-view.md)
  (0004, 0007).
- [2026-10-20 the white-noise music](investigations/2026-10-20-opus-white-noise.md) (0008).
- [2026-10-20 the dark 3D view: the RSX's fragment TXB](investigations/2026-10-20-dark-3d-view.md) (Mesa
  0045, 0009).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": listed as probably out of reach: GL 3.x+ only (with GZDoom and 0 A.D.). Release 4.3.5, the last with a GL 2.1 renderer, was ported since.
