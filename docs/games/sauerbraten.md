# Sauerbraten and Red Eclipse

What this page measured is **BananaBread**: Debian's `cube2` is the BananaBread fork of the Cube 2
engine with BananaBread's maps (`cube2-data`), not Sauerbraten itself (its Walnut Games entry is
`bananabread`; the bench recipe kept the name `sauerbraten`). The real Sauerbraten is a separate
entry, [below](#sauerbraten-the-real-game-2020-edition).


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
| base, SSD root (n=4) | 39.1 | 0.05 s / 25.6 / 103 ms | 25 / 117 MB | 71, 75 |
| base, USB root | 38.5 | 1.6 s / 24.1 / 459 ms | 20 / 117 MB | 70 (x3) |
| base, game data on the SSD (USB root) | 39.3 | 1.0 s / 26.4 / 407 ms | 21 / 117 MB | 70 |
| base | 38.8 | 1.2 s / 25.0 / 473 ms | 19 / 117 MB | 69 |
| `shortnormals` | 45.1 | 1.6 s / 30.4 / 512 ms | 20 / 121 MB | 69 |
| `NV30_FORCE_DXT=opaque`, SSD root (x2; off x2 beside it) | 39.0 (off 40.0) | 0.55 s / 23.9 / 177 ms (off 0.5 s / 26.0 / 185 ms) | 27 / 73-78 MB (off 117) | 74 |
| `PS3RSX_SHADOW_FAST=1`, SSD root (x2; off x2 beside it) | 39.7 (off 39.15) | 0.15 s / 26.2 / 118 ms (off 0.05 s / 25.7 / 96 ms) | 24 / 117-127 MB | 75 |

Picture right ("surprisingly smooth" between stalls, the user); sound plays with 60-181 ALSA
underruns a run. With the root on the SSD the load takes 7.1 s (6.9-7.2) against 15.4-19.3 s on the
USB root, its disk wait 1.7-1.9 s against 4.7-5.6 s, and the gameplay stalls fell from 1.6 s to
0.05 s with the longest frame at 103 ms (459 on USB): the USB stalls were disk faults. Steady fps did
not move (39.1 against 38.5, inside the 38.4-40.3 spread of all base runs). Moving only the game
data to the SSD (batch ssd1) gave nothing, because the binary, libraries and shader cache stayed on
the USB disk. Mesa's GLSL disk cache saves no load time here (17.4 s cold, 18.4 warm, on USB).
After the load the main thread is 56% on the CPU: 3.9 s of CPU, 1.9 s of disk wait; map inflate
(zlib) is 1.6 s of the CPU ([PROFILING.md](../PROFILING.md#start-up-profiles-on-the-ssd-root-batch-ssdroot2)).
Gameplay profile: Mesa 62.8% of the samples, the game 10.5%, ps3rsx 4.8%, audio about 5%
([HOTPATHS.md](../HOTPATHS.md#gameplay-profiles-on-the-ssd-root-batch-ssdroot2-p1b)).

The webcam microphone hears nothing above the room's hum in any of 20 Sauerbraten clips although
the sound card runs and the mixer and Vorbis decoder use CPU: judged by ear, not by the mic.

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
| SSD root | load 16.9 to 7.1 s, load disk wait 5.5 to 1.9 s, gameplay stalls 1.6 to 0.05 s, steady 38.5 to 39.1 | 71 | the root on the SSD helps loads and stalls, not frame cost |
| Mesa 0038 `NV30_FORCE_DXT=opaque` | 73 textures to DXT1 (88 MB to 11 MB), peak VRAM 119.8 to 73-78 MB, buffers 351 to 215 MB; steady 39.9 to 39.0, 1% low 26.0 to 23.9 (n=2, inside the spread); the picture is slightly softer | 74 | VRAM win, no fps change resolved; lossy, stays off by default ([MEMORY.md](../MEMORY.md#measured-on-the-ps3-batch-ssdroot2-p2)) |
| ps3rsx `shadow_fast` | 39.7 against 39.15 (x2), stalls and VRAM equal | 75 | no gain at n=2, stays off |

The query wait came from the water reflections: all of `nv30_query_result`'s 10.3% of the CPU was
`drawreflections > checkquery > glGetQueryObjectuiv`, the engine waiting for this frame's water
query mid-frame and Mesa spinning ([HOTPATHS.md, "Sauerbraten's query wait"](../HOTPATHS.md#target-5-sauerbratens-query-wait-mesa-0035-cube2-0010)).
In the first stall-recorder runs Sauerbraten's stalls were file faults and disk reads from the USB
disk (load 16.5 s, 5.3 s of it disk, row 55).

**Built, not measured yet:** cube2 0007-0009 (no PS3 run); Mesa 0037's sysmem stats with `shortnormals` (`sysmem_hint` says whether the
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
- 2026-10-19: the SSD root (row 71), force-DXT (row 74) and `shadow_fast` (row 75) A/Bs.

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

## Sauerbraten, the real game (2020 Edition)

Added 2026-10-25; one played session so far ([below](#the-first-session-2026-10-24)). Debian's `cube2` is not a BananaBread fork of the engine: its source is
Sauerbraten's own `src/` (svn at the 2020-12-29 release, minus the bundled enet and the IDE files; the release
archive's `src/` differs from it in two files, `sound.cpp` and `stream.cpp`, where Debian's has later upstream
fixes). Only Debian's *data*, `cube2-data`, is BananaBread's. So the engine for the real game is the `cube2` build
we already have, with every patch above, and no second engine package was made: building the release's source again
would give the same program without the patches. What was missing was the launcher and the data.

| Package | Suite | What |
|---|---|---|
| `sauerbraten` | public (testing, stable; CI builds it, `0.N`) | `/usr/games/sauerbraten`, the menu entry (Name `Sauerbraten`), an icon. Depends on `cube2`, Suggests `sauerbraten-data` |
| `sauerbraten-data` | `ps3-licensed` only, built locally (`0.0.20201229-1+ps3.N`, about 980 MB) | the release's `data/` and `packages/` whole, in `/usr/share/games/sauerbraten`, its docs in `/usr/share/doc/sauerbraten-data`. Depends on `sauerbraten`, so installing the data installs the game |

- The launcher runs `cube2_client -q$HOME/.sauerbraten -k/usr/share/sauerbraten/walnut -k/usr/share/games/sauerbraten -t1`:
  fullscreen at the desktop's resolution (the engine sizes its window from the desktop when `scr_w`/`scr_h` are unset;
  Debian's build defaults to a window, hence `-t1`), settings apart from BananaBread's `~/.cube2`, no user file written.
  `SAUERBRATEN_WINDOWED=1` starts in a window. The game's own `config.cfg` can still switch it later, as in the stock game.
- Walnut's defaults: the first package directory holds only `data/defaults.cfg` (`sauerbraten/files/walnut/`), which the
  engine runs only while `~/.sauerbraten/config.cfg` does not exist. It runs the release's own defaults.cfg, then sets
  `texcompress 512`; the game saves that into config.cfg at the first quit, and the player's settings rule from then
  on (`/texcompress 1024` restores the engine's value). A player with a config.cfg already gets nothing from it.
- `sauerbraten-data` is a second package beside `sauerbraten-nonfree-data` (kept as it is: it completes `cube2-data` in
  `/usr/share/games/cube2` for the BananaBread entry, 912 files). Reusing it was not possible, since the real game needs
  the maps, textures and music too, in its own directory so neither game sees the other's files. The orig tarball is made
  from the same pinned release archive (`make sauerbraten-data-src`, `tools/sauerbraten-full-data.sh`), never committed.
  Mixed, partly non-commercial licences: `tools/licensed-packages.txt`, no CI workflow. Not in `ps3-games-licensed`'s
  Depends (980 MB); install by name.
- Endianness: nothing to fix. The engine already runs big-endian on the PS3 (the BananaBread maps read through
  `lilswap`), and the release's maps and models use the same formats.
- Patches in effect (all of `cube2/patches`, since it is the same engine): 0001 `texcompress`, 0002 `gpuskel 0` and no
  GPU skinning shaders, 0003-0005, 0006 libspeq skinning, 0007-0009 fewer GL calls, 0010 `oqwaterlag`. 0011
  `vanormalshort` is built in and off by default. Nothing is left unported. The real maps fill the VRAM at the engine's
  texture settings (the first session, below), hence Walnut's `texcompress 512` default.
- Recipe `games/bench/recipes/sauerbraten-edition.sh` (variants `complex`, `metl3`, `lean`, `gpuskel`; `session()` is the
  launcher alone; `PLAY_MARK` is the `read map` line); entry `gamedb/content/games/sauerbraten/`. Its PC reference
  (`tools/play-pc.sh`) installs the entry's packages from Debian, which has no `sauerbraten`: `PC=0 play-test.sh start
  sauerbraten` until the PC image gets our two debs.

### The first session (2026-10-24)

An online insta-CTF match on eternal_valley, then berlin_wall (`build/bench/ps3-bench-20261024T233328Z-session-sauerbraten-edition-base`):
54 fps median, 14.7 s of stutter a minute, a segfault after 202 s of play. What the logs say:

- **The crash was Mesa's occlusion queries, not the VRAM.** `nip` resolves (stable-debug's `mesa-libgallium-dbgsym`
  ps3.721) to `nouveau_heap_free` (nouveau_heap.c:106, `r->in_use = 0`), called from `nv30_query_object_del`
  (nv30_query.c:76). The heap node it wrote was `nouveau_mm_free_work`'s function descriptor in libgallium's read-only
  `.opd`: the query object had been freed and its memory reused for a fence work. nv30 has 124 report slots; the engine
  ends up to 2048 occlusion queries a frame and reads them a frame later, so slots were taken back from queries that
  still pointed at them (upstream's code). Mesa 0046 fixes it ([DRIVER.md](../DRIVER.md#the-mesa-side)); the
  same bug gave queries each other's results, which shows as geometry popping (the user's "a bit glitchy").
- **The `out of VRAM for a 8100 KiB BO` bursts** (1920x1080x4, 135 failures each) came at 61.9, 121.7 and 181.5 s, each
  1.5 s into one of the bench's `xwd` screenshots (60, 120, 180 s; 2.7-3.0 s each, X busy): window-sized buffers asked
  for while X was stalled (most likely DRI3 back buffers: both cube2's and Xorg's VRAM swing by 8100 KiB together in the
  samples), with no VRAM left. They are not the crash's cause (the last burst was 54 s before it).
- **VRAM was full.** cube2_client held 180 MB of VRAM on eternal_valley and 210 MB after the map change (probably the previous
  map's textures still loaded), Xorg 27-35 MB: the whole 250 MB. The VRAM swap gave all 96 of its chunks back as
  designed (`pages in VRAM` 15421 before, 0 during), so its pages went to the disk; MemAvailable 22-45 MB. The 30 MB of
  GPU-only VRAM (224-254 MB) held 132 KiB: on the PS3 every nv30 texture asks for CPU-visible VRAM (see below).
- **Textures are most of it.** Of the release's 2155 world texture files named by default_map_settings.cfg, the 512-pixel
  ones are 772 files and 76% of the bytes (RGBA8 with mip levels on the RSX); `texcompress 1024` leaves them plain, 512
  stores them in an eighth (DXT1) or a quarter (DXT5). For BananaBread, Mesa's equivalent (`NV30_FORCE_DXT=opaque`, row
  74) took VRAM from 117 to 73-78 MB at equal fps and load time, with softer rock textures. Hence the new default
  (unmeasured on the real game: the next session's VRAM column and the user's look at the textures decide whether it
  stays).
- Not done: render targets and depth buffers in the GPU-only 30 MB. nv30 on the PS3 maps every miptree CPU-visible
  because `nv30_transfer_rect_cpu` may map it; a depth buffer or render target that is never read back by the CPU could
  go there (8 MB each at 1080p), but whether every transfer path avoids the CPU map is unproven.

### The second session (2026-10-25)

Bots, about 6 minutes, Mesa ps3.819 (0046 in) and sauerbraten 0.819, whose `texcompress 512` did not apply: the
player's `~/.sauerbraten/config.cfg` already existed (and stays the player's). No query crash; a different one at 400 s
(`build/bench/ps3-bench-20261025T013202Z-session-sauerbraten-edition-base`):

- **The crash: a write through NULL after GART ran out.** dmesg: three `out of GART for a 4096 KiB BO` lines, then
  `segfault at 0` in libc (a doubleword store of a memcpy). The lr is in libgallium: its distance from libc's base in
  the crash (-0x4105ec4) put on the library layout of a fresh cube2_client on the PS3 lands at libgallium offset
  0x60a13c, which ps3.819's symbols resolve to the memcpy in `st_setup_current()` (st_atom_array.cpp:343), inlined
  into `st_update_array_templ`. That copies the current (zero-stride) vertex attributes into the stream uploader
  without checking that `u_upload_alloc()` got anything. The uploader's next 1 MiB buffer is a chunk of a 4 MiB
  nouveau_mm GART slab, which no longer fit: cube2_client held 186-194 MB of VRAM and 46-51 MB of the 63 MB GART (its
  buffers that found no VRAM went to GART), Xorg 7 MB. Mesa 0047 and 0048 handle it (DRIVER.md); the same signature
  (`out of GART for a 4096 KiB BO`, then a NULL write in libc) is BananaBread's crash in LOG-FINDINGS item 2.
- **The `out of VRAM for a 8100 KiB BO` bursts are the screenshots again**: at 301.5 and 361.6 s into the run, 1.4-1.5 s
  after the `xwd` shots at 300.08 and 360.09 s (the earlier shots found room). Each burst is 135 failures (10 printed,
  125 suppressed), and 135 = 1080 / 8: the X server's GetImage answers in bands of 8 lines of 7680 bytes (64 KiB), and
  each band asks for a screen-sized buffer (most likely the readback copy of the GL frontend in Xorg's glamor), which
  fails and falls back. They come from Xorg, only while the bench takes a screenshot of a full VRAM, and are not the
  game's.
- VRAM is as full as on 10-24, since the player's config keeps `texcompress 1024`. A launcher-environment alternative
  that works with any config is Mesa's `NV30_FORCE_DXT=opaque` (compression at upload, BananaBread -40% VRAM, softer
  textures): not set, the user's call after seeing it.

