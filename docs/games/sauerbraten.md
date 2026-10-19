# Sauerbraten and Red Eclipse

Sauerbraten (the Cube 2 engine, our `cube2` build) plays the user's recorded demo at 38.5 steady fps
with 1.6 s of gameplay stalls (row 70), and at 45.1 with its world normals sent as shorts (row 69,
+17%, not the default yet). It is the suite's heaviest GLSL game: 117 MB of VRAM and about 20 MB of
RAM free at the lowest. Red Eclipse, the same engine family without our patches, runs at lean
settings. Open items are in [backlog/games.md](../backlog/games.md).

## Packages and patches

`cube2`, `cube2-server` (`0.0.20201229+dfsg-1+ps3.N`, package directory `cube2/`), replacing Debian's.
The release candidate is ps3.354; ps3.442~dev (with 0011) is in ps3-dev. The data is Debian's
`cube2-data` 1.3 (the BananaBread content, 20 maps) plus `sauerbraten-nonfree-data` (the models and
sounds Debian leaves out, CC BY-NC, `ps3-licensed` only, pulled in by `ps3-games-licensed`).
Sauerbraten is not in `ps3-games`' Depends: it deploys by name.

| Patch | What | Switch |
|---|---|---|
| 0001 | textures compressed at upload on Mesa too. Upstream turns it off on Mesa (`if(!mesa) usetexcompress = 2` in `rendergl.cpp`), so only DDS files went in compressed | `texcompress` (default: textures of 1024 and up) |
| 0002 | CPU skinning by default and no GPU skinning shaders built. nv30 has no indirect constant addressing, so the bone-array vertex shader becomes if/else trees: 11 fps on the GPU against 28 on the CPU | `gpuskel 0` |
| 0003 | only the world shader variants that can be drawn (dynamic lights and water fade only as far as `maxdynlights`, `waterrefract`, `waterfade` ask) | |
| 0004 | sky boxes and cube maps from their 256 px .dds instead of six 2048 px PNGs decoded twice (the likely load-time OOM) | `skydds 1` |
| 0005 | the lightmaps' CPU copies freed once uploaded | `lmfreedata 1` |
| 0006 | CPU skinning through libspeq on the SPEs (dlopen; the engine's own loop when libspeq is missing, declines or is late) | `speqskin 0` turns it off; `SPEQ_SKIN` |
| 0007-0009 | fewer GL calls: uniforms sent only when changed (`gluniformcache`), a batch's textures bound together (`glbatchtmu`), particle vertices uploaded only by renderers with particles (`glpartvbo`); cave 1657 to 1300 GL calls a frame on llvmpipe, same pixels | variants `nouni`, `notmu`, `nopart`, `allbatchoff` |
| 0010 | water occlusion queries without waiting for the GPU | `oqwaterlag 1` (0 as before) |
| 0011 | world normals and tangents as 16-bit signed normalized (`svertex`, 40 bytes), which nv30 fetches, instead of GL_BYTE that u_vbuf converts at every world draw (and nouveau keeps a malloc copy of every world VBO for) | `vanormalshort 1` (default 0) |

0011's default stays 0 because the shading differs slightly: s/32767 can't equal b/127 except at 0
and +-1, so about 170 of 518,400 pixels differ by at most 2/255 on llvmpipe. Cave's world VBOs grow
from 4.40 to 5.50 MB. Details: [MEMORY.md, "Sauerbraten's world normals as shorts"](../MEMORY.md#sauerbratens-world-normals-as-shorts-cube2-0011-vanormalshort).

## The recipe

`games/bench/recipes/sauerbraten.sh`, base: the user's recorded demo
(`games/bench/assets/sauerbraten-bench.dmo`, 2026-10-17: an FFA round on cave with two bots, 93 s of
game time), copied to `~/.cube2/demo/bench.dmo` and played with `mode -1; map bench`, as the data's
`demo` command does it. A CubeScript (`-x`) counts frames with a sleep re-armed every frame and wall
time from `getmillis 1`, and prints `ps3bench: loop <k>: <n> frames in <ms> ms: <fps> fps, longest
<ms> ms` per playback. The window opens 60 s after the first `mapstart` and closes at the playback's
end; `demoend` plays the demo again (the reload is left out: `WIN_ON_RE`, `WIN_OFF_RE`) until the
window holds about 120 s, then `ps3bench: steady ...` and quit. The bots are played back, not
simulated, so every run is the same. Since row 64 the demo is first person with the nonfree data
(the gun and HUD visible), heavier than the spectator demo before, and its runs swing a lot (23-38
in rows 64-65). `getfps` itself averages only the last 60 frames. Settings go into
`~/.cube2/autoexec.cfg`; MemAvailable and SwapFree every 5 s into during.log.

Variant `spawn` is the older method: `-l` loads cave, the player stands at its only start for 30 s
of counting, then its own steady window. `gun`, `towers` and the skinning variants use it. The
missing HUD gun is explained: `-l` starts coop edit, where the player has the pistol, and the
BananaBread data has no pistol or fist HUD model.

Other variants: `gpuskel`, `cpuskel`, `gungpu`, `spuskin`, `ppeskin`, `offskin`, `demospu`,
`demoppe`, `demooff`; memory `pngsky`, `keeplm`, `dyn5`, `dyn1`, `dyn0`, `nofade`, `nowater`, `sd2`,
`sd1`, `nobumpmdl`, `tex1`, `tex2`, `maxtex512`, `tc512`, `tc256`, `snd22`, `notc`, `lean` (the bundle
meant to ship: no water reflection or refraction, `shaderdetail 1`, no bump- or env-mapped models,
`maxdynlights 1`, `texcompress 512`, 22 kHz sound); `rgbspu`, `rgbppe`; `oqlag`, `oqnowait`;
`shortnormals`, `bytenormals`; `nouni`, `notmu`, `nopart`, `allbatchoff`.

## Current results

| Configuration | Steady fps | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Row |
|---|---|---|---|---|
| base, USB root | 38.5 | 1.6 s / 24.1 / 459 ms | 20 / 117 MB | 70 (x3) |
| base, game data on the SSD | 39.3 | 1.0 s / 26.4 / 407 ms | 21 / 117 MB | 70 |
| base | 38.8 | 1.2 s / 25.0 / 473 ms | 19 / 117 MB | 69 |
| `shortnormals` | 45.1 | 1.6 s / 30.4 / 512 ms | 20 / 121 MB | 69 |

Picture right ("surprisingly smooth" between stalls, the user); sound plays with 127-184 ALSA
underruns a run. The load takes 15.3-16.9 s (batch ssd1); the game data on the SSD gave no clear
effect, because the binary, libraries and shader cache stayed on the USB disk and the load's disk
wait stayed at 4.8-5.5 s. Mesa's GLSL disk cache saves no load time here (17.4 s cold, 18.4 warm).

Memory ([MEMORY.md](../MEMORY.md)): 78 MB Rss plus 12.3 MB swapped, malloc 63.4 MB used and
17.6 MB free in its arenas (glibc's raised mmap threshold keeps 16-17 MB free in the main heap),
117 MB of VRAM of which textures are about 87 MB. Without the VRAM swap (smoke's default) it was
OOM-killed; with it, the VRAM swap is full.

## What was tried

| Change | Result | Row | Verdict |
|---|---|---|---|
| Mesa 0033 `NV30_TRANSLATE_LUT` (8-bit vertex attributes through a table) | 25.9 to 39.6 steady (+53%), 1% low 18.3 to 26.9, on the earlier spectator demo | 57 | Mesa's default (`=0` turns it off); not A/B'd again on the first-person demo |
| CPU skinning through libspeq | spawn: `spuskin` 23.6 steady with 21 stalls (4.3 s), `ppeskin` 28.1 with none, `offskin` 23.0; demo: 25.8 / 25.5 / 25.3 | 54, 55 | libspeq's PPE path is best; the SPU path adds hitches. Plain CPU skinning stays the default, the SPU hook selectable |
| RGB DXT1 on the SPEs (libspeq 0.394) | `rgbspu` 39.1 against `rgbppe` 39.4: the demo uploads no RGB DXT1, only a few DXT5 | 59 | not exercised here |
| `MALLOC=mmap`, `MALLOC=trim` | rounds disagreed with each other | 61 | noise, not shipped |
| Mesa 0035 `NV30_QUERY_WAIT=200` | 45.6 against 25.0 in one run, then 23.0/33.4 against 26.0/37.9 interleaved; CPU lower with it | 64, 65 | no fps gain, stays off |
| cube2 0010 `oqwaterlag` | 39.4 steady against base 25.0, one run | 64 | promising, repeat (PLAN 2.6) |
| cube2 0011 `vanormalshort` | 38.8 to 45.1 (+17%), stalls equal, VRAM +4 MB | 69 | default still 0 (pixels), decision open |
| Game prefetch (`GAMEPREFETCH=fetch`) | load 18.1-18.6 s against 15.2-18.8 | 68 | no gain, default off |
| SSD game data, SSD swap spill | 39.3 / 38.5 against 38.5; load -3 to -6%, inside the spread | 70 | no proven gain |

The query wait came from the water reflections: all of `nv30_query_result`'s 10.3% of the CPU was
`drawreflections > checkquery > glGetQueryObjectuiv`, the engine waiting for this frame's water
query mid-frame and Mesa spinning ([HOTPATHS.md, "Sauerbraten's query wait"](../HOTPATHS.md#target-5-sauerbratens-query-wait-mesa-0035-cube2-0010)).
In the first stall-recorder runs Sauerbraten's stalls were file faults and disk reads from the USB
disk (load 16.5 s, 5.3 s of it disk, row 55).

**Built, not measured yet:** cube2 0007-0009 (no PS3 run); Mesa 0038 `NV30_FORCE_DXT=opaque`
(TESTS.md ssdroot2 P2); Mesa 0037's sysmem stats with `shortnormals` (`sysmem_hint` says whether the
readbacks also set nouveau's keep-copy hint); `texreduce 1` (variant `tex1`, an estimated 45-60 MB of
the 117 MB VRAM).

## History

- 2026-10-16 (research): Sauerbraten and Red Eclipse turn texture compression off on Mesa; a
  one-line patch makes Sauerbraten a third S3TC game.
- 2026-10-17: OOM-killed without the VRAM swap; with it 138 MB of VRAM and the swap full. First run
  with CPU skinning: 15 fps HUD median including the over-60 s load. GPU skinning ran without a
  wedge but at 11 fps and badly stuttering. Our cube2 +ps3.272 brought 0002-0005.
- 2026-10-18: the LUT (+53%), the demo with the nonfree data (row 64 on), query wait and oqlag
  (rows 64-65), shortnormals (row 69), the SSD arms (row 70).

## Red Eclipse

Red Eclipse 1.6.0 (Debian's `redeclipse`, `redeclipse-common`, `redeclipse-data`: 842 MB download,
959 MB installed) is a smoke test, not benchmarked. It runs: in the match on torus as a spectator,
lean sound and textures, a player name set, no OOM, 86 fps HUD peak (2026-10-18). It has none of
cube2's memory patches.

Recipe `games/bench/recipes/redeclipse.sh` (`SUITE=0`): `-x'dm <map>'` (a deathmatch with bots on
the engine's loopback server); `RE_MAP` or the variants `singularity`, `disco`, `cyanide` pick the
map (torus, 43 KB, is the smallest real one). Variants `tex512`, `fullsound`, `bots0`, `bots3`,
`defaults`, `menu`.

What the first run taught (it was OOM-killed loading torus):
- **The memory is the sound.** `addsound` loads each sample at registration and SDL_mixer decodes it
  whole into PCM in the device's format: about 190 samples at the start, and at the map load every
  `mapsound` of the map's .cfg (torus: 74 ambience files, 24 MB of .ogg). Debian's amd64 build under
  Xvfb and llvmpipe on the PC: 534 MB RSS after loading torus at 44.1 kHz stereo, 381 MB with no
  sample loaded, 397 MB with `soundmono 1` and `soundfreq 11025`, 375 MB with the other lean settings,
  326 MB with `maxtexsize 256` and `texreduce 3`. The menu alone: 262 MB, 231 without samples. On the
  PS3 the process grew to about 200 MB during that load (55 resident, about 150 in zram).
  `soundmono` and `soundfreq` count only before the audio device opens, so setup() writes them to
  `init.cfg`. Volumes don't stop the decoding, and `SDL_AUDIODRIVER=none` makes the game quit.
- 1.6 reads `localinit.cfg` for the loopback server (not `localexec.cfg`), and the variable is
  `sv_botlimit`: the first run had 3 bots, the recipe now 1.
- The first start's "Welcome to Red Eclipse" profile menu stays up while the player's name is empty
  and blocks spawning; autoexec.cfg ends with `playername RE_NAME`.
- `usetexcompress` is read-only (min 1 > max 0), so the old `tc` variant did nothing; compression
  needs cube2's 0001, which applies to Red Eclipse's tree.
- autoexec.cfg sets `vsync 0`, `maxfps 0`, `texreduce 3`, `maxtexsize 256`, `shaderdetail 1`, and
  turns off water reflection, refraction, env maps, caustics, glare, grass, shadow maps, depth
  effects, motion blur, dynamic lights, bump- and env-mapped models, GPU skinning, decals,
  anisotropy, gore, ragdolls and mumble (every name checked in the 1.6 source).

Next for Red Eclipse: cube2's memory patches carried over, samples loaded when first played (PLAN
3.8; [backlog/games.md](../backlog/games.md)).

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md).
