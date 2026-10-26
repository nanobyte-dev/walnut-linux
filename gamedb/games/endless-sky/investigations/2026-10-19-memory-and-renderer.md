+++
id = "memory-and-renderer"
games = ["endless-sky"]
+++

# Endless Sky on the PS3: memory and renderer (research, 2026-10-19)

Moved on 2026-10-26 from the docs' research folder (ENDLESS-SKY.md). The state below is as of 2026-10-20; the package is `gamedb/games/endless-sky/packages/endless-sky/`.

Go, effort M (1.5-2 weeks), memory borderline. The ~700 MB seen on the PC was glibc fragmentation from 24 loader threads plus sprite textures that belong in VRAM; with the defer-images preference, 2 threads and mono sounds the estimate is ~175 MB of XDR. Renderer: upstream GL2 path + 22 GLSL 1.20 shaders (written, run on llvmpipe at GL 2.1), a 2D DXT5 atlas instead of 3D textures (nv30 3D limit 512), the fog map as LUMINANCE8. Four big-endian bugs (~30 lines). First step: --parse-assets on the PS3 for anon memory and load time.

State (2026-10-20): steps 2-4 are done as the package `endless-sky` (gamedb/games/endless-sky/packages/endless-sky/: Debian's 0.11.2-1 plus 12 patches, unit tests and an amd64/ppc64 decode hash comparison pass, GL2 path and atlas checked on the PC at GL 2.1, recipe `endless-sky` in bench). Step 1 on the PS3: `--parse-assets` alone peaked at 138 MB RSS in 70 s, exit 0 (go). First smoke runs (ps3.580, build/bench/ps3-bench-20261019T194124Z/T194345Z-smoke-endless-sky-menu/base): the loading screen rendered right (GL2 shaders and atlas work on nv30), then ps3rsx logged "out of VRAM" from 30 s on and the kernel OOM-killed the game at about 125 s of loading (rc=137; endless-sky RSS tiny, about 200 MB in swap). Causes (found on the PC, 2026-10-20) and fix (patch 0009, ps3.589): see "Load-phase memory" below. ps3.589 still died in the load (flight smoke, OOM at 131 s; the menu smoke ran 360 s); patch 0010 (ps3.597) cut the decoded-sprite peak but ps3.597 died at about 100 s too (205 MB of anon in zram); patch 0011 (ps3.602) loads sound samples at the first play and ps3.602 ran 480 s without an OOM, but its flight showed no HUD, starfield or planets, with "out of temps" from the full outline shader: patch 0012 (ps3.624) makes the cheap outline the default. Not run on the PS3 again yet: smoke menu + flight with MEM=1 is in TESTS.md Next.

## Load-phase memory (the OOM at 125 s)

Three things, all in the load, none in the steady state:
1. **No texture compression.** Nothing set `NV30_FORCE_DXT` (Mesa 0038 is off by default; the recipe comment assumed it was on). The 1183 sprite atlases went to VRAM as power-of-two RGBA8: 384 MB by the PC's count when the flight starts (llvmpipe `texKB`), against 256 MB of VRAM that is also the swap tier. When VRAM ran out the BOs fell back to pinned system memory (GART, CMA chunks) and the kernel ran dry. DXT5 is a quarter, about 96 MB. The package now sets `NV30_FORCE_DXT=all` in main() when it is not set (ES_LOW_MEMORY builds; other drivers ignore it).
2. **100 decode chains.** SpriteLoadManager::Init started `TaskQueue::MAX_SYNC_TASKS` (100) load chains; each holds a decoded image from the worker until the main thread uploads it, and the workers are faster, so about 100 decoded images queued up: RSS minus texture storage peaked at 490 MB on the PC against a 265 MB plateau (+195 MB over the plateau, in the first second). Six chains (ES_LOW_MEMORY) peak at 345 MB (+80, one big sprite at a time). The load takes longer on the main thread's frame rate (6 uploads a frame): 10 s against 3 s on the PC at llvmpipe's frame rate; expect 10 to 30 s of the PS3's load screen.
3. **The grid copy.** AddBuffer builds the atlas grid in RAM (up to 4096x4096x4 = 64 MB) next to the decoded frames; the frames are now freed before the glTexImage2D call, so the driver's copy or the DXT compression never overlaps them. The biggest atlas at "Reduce large graphics = all" is a few tens of MB; the spikes in the PC trace (+60 to 70 MB for one upload) are decoded frames plus grid.

**Second round (ps3.589 on the PS3, batch ssdroot9, 2026-10-20).** DXT worked (665 textures, 371 MB to 91 MB, no "out of VRAM") and the menu smoke ran its 360 s, but the flight smoke was OOM-killed at 131 s. The recipe's keys had not caused it: xkeys found no window, the game was still loading (the load takes 150 s or more on the PS3, not the 45-80 s estimated; `ES_LOAD_WAIT` is 240 now). At the kill the game had 31 MB resident and 190 MB in swap, about 220 MB of anon in all against a VmHWM of 92 MB; `mallinfo2` showed about 100 MB of small-block arena (game data, constant) and large mmapped blocks growing 28 to 117 MB while VRAM filled from 52 to 123 MB (the menu run: 33 to 122 MB). VRAM was 209-218 of 220 MB used, so the vswap tier had no room and swap fell to zram, which is RAM. On the PC the step from menu through Enter Ship and Depart to flight adds about 6 MB of heap and no large blocks (planet, landscape and star sprites are deferred and small): the keys are not a memory event.

What grew is the sprite load once more: sprites are decoded at full size and halved ("Reduce large graphics") only when the main thread uploads them, and the biggest ones are huge at full size (decoded RGBA8: ember tear vortex 90 MB, milky way 64, tachytia 54, astral cetacean 54, jd detainer 45, nuke 30). With an artificial 20 ms per upload on the PC to imitate the slow PPE main thread, pending decoded pixels peaked at 287 MB with 100 chains, 124 MB with 6 and 121 MB with 6 plus a byte limit (one image alone is 90 MB). Patch 0010 (ps3.597): `ImageSet::Load` applies the reduction itself, one frame at a time in the loader thread (the collision masks still come from the full frame; the sprite keeps its full size; same box filter, so the textures are the same bytes), and the loader threads wait while 16 MB of decoded pixels are queued (12 chains). Peak pending with the patch: 24 MB (flight screenshot checked on the PC). Untried on the PS3 when written.

**Third round (ps3.597, batch ssdroot11, 2026-10-20).** Menu and flight smokes both OOM-killed at about 100 s (earlier than ps3.589's 131 s: 12 chains finish the sprite load sooner). Forcedxt reached its 600 textures (81 MB DXT), VmHWM 96.6 MB, anon-rss at the kill 2.6-7.5 MB, total-vm 432-445 MB. From the OOM report and the mem snapshots: the game had 47-52k pages (190-205 MB) in swap, Xorg 5k, swap used 267-285 of 393 MB; the swap is zram, so those pages are compressed RAM (about 140 MB of the 222 MB managed, by difference: free 22, LRU 10, slab 25, GART 17, stacks and page tables 6), and VRAM, the other tier, is 208 of 220 MB full of textures. The kernel runs out because the game's anon is too big for zram plus kernel plus X, not because anything is resident: at t=19 s the game already had 116 MB in swap with 23 MB resident. mallinfo2 at t=19/63 s: arena 98-103 MB (game data, constant: Conversation nodes 14 MB, Mission 10 MB, Files::Read 8 MB, systems, phrases; 87 MB in `--parse-assets` on the PC), large mmapped blocks 35-56 MB then 76-82 MB (sounds 41 MB plus the images in flight). The chains do not matter (a few KB each); GART pinned by staging is 17 MB (Mesa's pool, about 9 MB for the game), not the problem.

The cut that does not need the game's data structures rewritten: sounds (41 MB of 16-bit PCM, which compress badly in zram, and a session plays a few dozen of the 243). Patch 0011 (ps3.602): Sound::Load checks the header, Buffer() reads the samples on the first call. PC (llvmpipe, same flight): resident after the load 660 to 624 MB (-36 MB), mmapped blocks 390 to 363 MB, flight screenshot unchanged. Untried on the PS3. If it still dies, in order of size: the game data arena (about 100 MB; Conversation::AddNode 14 MB over 34k nodes, Mission::Load 10 MB: load conversations and missions that are not used yet, a big change in upstream's data model), a plain synchronous load without worker threads (the arena stays; saves little), and a smaller PENDING_LIMIT.

**Fourth round (ps3.602, batch ssdroot11/12, 2026-10-20): no OOM, but no HUD.** The flight smoke ran 480 s (hwm 98 MB, textures 373 to 91 MB by DXT, 390 MiB of VRAM for the game's buffers with vswap, about 24 fps late in the run) and the keys worked. The picture showed only the player's ship and the NPC messages at the bottom: no starfield, no planet, no asteroids, no HUD (radar, status bars, date, credits, targets); at 300-420 s the "Lost 1" help (flagship farther than 10000 from the system centre, MainPanel.cpp:583) came up, so the ship had also flown away, most likely a held Up key at a low frame rate, not a spawn at the wrong place (New Boston is 514 units from the star). The menu run, in contrast, drew everything (star compass, planet, panels, text, fills). The one error in game.log, identical in the ps3.589 menu run, is `temp:54 - out of temps!!` 36 times: Mesa's nv30 fragment compiler (nvfx_fragprog.c:53, 48 temporaries on nv4x) ran a program out of temporaries and goes on with temp 0 (garbage code, no GL error, dmesg clean). The only shader of ours that can need that many is outline.frag (2 x 9 x 8 = 144 lookups, unrolled); the main menu's pilot ship picture and the HUD's ship picture use it. The 36 lines are one compile, not 36 programs: the count is the same in the menu and the flight run. Patch 0012 (ps3.624) turns "Cheap ship outlines" (outlineLite, 8 lookups) on by default in ES_LOW_MEMORY builds; the game only falls back to it by itself when the full shader does not compile, and nv30 fails late, in the driver. The recipe now defaults to cheap outlines; `fullout` and `noblur` are variants.

What is not proven: that the failed outline program is what hides the HUD, the starfield and the planets (the HUD's first elements, the status sprite and the date and credits strings, come before its outline element; I could not reproduce anything on the PC, where llvmpipe has no register limit, and the PC flight with cheap outlines draws everything). What the PS3 re-test must show: no "out of temps" in game.log; at 60 s after Depart the starfield (streaks while moving), the radar, the status bars, date and credits, and NPC ships or asteroids if any are near. If the HUD is still missing: `run.sh smoke endless-sky "fullout noblur"` one at a time, `ENVS="- NV30_FORCE_DXT=off"` (the DXT atlases of the HUD sprites), and a hold of Up shorter than 5 s (a stuck Up flew the ship 10000 units away). Do not trust "Lost 1" as a spawn check: look at the picture 5 s after Depart.

What was not done, on purpose: uploading the atlas in pieces with glTexSubImage2D into a glTexImage2D(NULL) texture. Mesa 0038 never compresses a `glTexImage2D(NULL)` texture (render targets and sub-image-filled textures stay as the game asked), so the atlases would be RGBA8 in VRAM again, four times the size, to save a transient of a few tens of MB of RAM. Smaller pages would need a shader change (one texture per sprite) for no gain either; the 4096 limit stays.

PC numbers (amd64 build of the same source, Xvfb 1920x1080 + llvmpipe at GL 2.1 with the extension list cut to nv47's, `--tq-threads` 2 default, defer on, Reduce large graphics all, ES_LOW_MEMORY; "net" = VmRSS minus the bytes of uploaded textures, which llvmpipe keeps in RAM and the PS3 keeps in VRAM; it still contains about 100-140 MB that exist only on the PC: libLLVM 62, libgallium 14, llvmpipe context 38): before max net 490 MB, after 345 MB; plateau 265 MB (about 150 MB on the PS3 by that subtraction, the same as the 138 MB of `--parse-assets`). The tracing patch was a test-tree-only fprintf in AddBuffer (not shipped).


Measured on the PC, Debian sid amd64 endless-sky 0.11.2-1 (same version as ppc64), podman container
on /mnt/data_linux/Temp/ps3/es (deleted afterwards), Xvfb 1920x1080, llvmpipe, new pilot "Ann Lee",
bought a Sparrow, departed from New Boston, flew in Rutilicus. heaptrack 1.5 (with endless-sky-dbgsym),
malloc_stats() through gdb, smaps. Prefs for all runs: "Reduce large graphics" = all (index 2), haze and
motion blur off, audio through OpenAL Soft's null device (ALSOFT_DRIVERS=null; the earlier census runs
had no audio device, so the sounds were never loaded: 82 MB missing there). Scripts in scripts/, raw
numbers in results/. The host has 24 cores, which turned out to matter (below).

## 1. The ~700 MB: what it is

Steady RSS 1300-1350 MB (VmHWM 1550-1640), flight and menu the same within 20 MB: everything is loaded
at start. Live malloc bytes at flight (heaptrack leak view of a copy of the running trace, 670 MB,
no audio device; malloc_stats with audio: 758 MB):

| Part | MB | Notes |
|---|---:|---|
| llvmpipe texture storage (2744 sprite array textures, AddBuffer Sprite.cpp:52) | 522 | RGBA8; goes to VRAM on the PS3, not XDR |
| llvmpipe context, LLVM JIT, framebuffers (GameWindow) | 38 | Mesa nv30 is ~10 |
| sounds, 243 WAV files decoded to 16-bit **stereo** (Sound::Load duplicates mono) | 82 | 45 MB on disk |
| game data parsed into objects: UniverseObjects 65, GameData 23, Conversation 17 | 106 | 11 MB of text files |
| rest (Mask 2, misc) | ~10 | collision masks are tiny, not the problem |
| freed but still resident glibc memory | ~520-600 | the "unexplained 700" |
| file-backed (libLLVM 62, libgallium 14, binary 4, others) | ~100 | |

So the 700 MB is not data. It is **glibc fragmentation**: the loader threads (TaskQueue defaults to one
per core, 24 here) decode PNG/JPG into 4-byte-per-pixel ImageBuffers, per-thread arenas grow to
hundreds of MB at the loading peak (heaptrack peak 1.27 GB: 490 MB of JPG buffers + 292 MB PNG + 92 MB
libjpeg + textures, 94 big JPGs alive at once), then free() leaves it all resident.
Proof, same game, same flight, malloc_stats "in use" vs RSS:

| Run | RSS MB | VmHWM MB | malloc in use MB |
|---|---:|---:|---:|
| default threads (24), glibc defaults | 1337 | 1547 | 758 |
| + MALLOC_MMAP_THRESHOLD_=131072 | 849 | 1204 | 764 |
| + "Defer loading images" | 599 | 717 | 506 |
| + --tq-threads 1 | 590 | 624 | 505 |
| PS3-like: --tq-threads 2, glibc defaults, no defer | 946 | 1200 | 761 |
| --tq-threads 2, defer | 686 | 727 | 504 |
| --tq-threads 2, defer, MMAP_THRESHOLD_=131072, ARENA_MAX=2 | 584 | 630 | 505 |

The PS3 (2 hardware threads) gets 2 loader threads, so it never sees the 600 MB of waste, but the 185 MB
that remains at tq-threads 2 is still avoidable with the two malloc settings (or mallopt in main()).

Of the live 760 MB (with audio): textures 477-522, sounds 82, data 106, context 38. Textures are the
only big one, and they live in VRAM on the PS3.

Texture size by folder (RGBA8, after "reduce large graphics = all" halving, from the image headers,
results/idents.txt; measured live total with 3D-style arrays 477 MB, 2766 textures):
star 172, land 144, effect 94, ship 72, planet 39, ui 21, asteroid 17, _menu 16, outfit 15,
thumbnail 13, scene 12, projectile 10, other 6.

## 2. Cuts that exist already (no code)

- Preference "Defer loading images" (Preferences panel > Performance; preferences.txt line
  `"Defer loading images"`): land, thumbnail, outfit, scene, star, planet sprites are loaded when
  needed and culled after 100 in-game days (SpriteLoadManager.cpp). Live -258 MB, resident set of
  textures drops 477 to ~270 (effect, ship, ui, asteroid, _menu, projectile, map). Upstream already
  has the lazy loading; I found nothing else to add.
- "Reduce large graphics" = all (already assumed).
- `--tq-threads N`, and MALLOC_MMAP_THRESHOLD_=131072 / MALLOC_ARENA_MAX=2 (table above).
- Not a cut but a trap: ImageBuffer decodes JPG fully even for the dimensions-only read? No: ReadJPG
  allocates the full buffer (untouched pages) and returns; harmless for RSS.

## 3. Small patches (the avoidable rest)

1. Sound::Load stores mono as stereo: keep mono and expand in WavSupplier::NextDataChunk: 82 to 41 MB.
2. mallopt(M_MMAP_THRESHOLD, 128 KiB) and M_ARENA_MAX=2 in main() (or env in the launcher).
3. Optionally cap in-flight image decodes (the load queue runs ahead of the main-thread uploads).
4. Conversation/Mission/Set data 106 MB: no cheap cut found. A tail of it (conversation text 17 MB,
   missions) is cold and would sit in swap; I would not touch it.

## 4. PS3 estimate

XDR, after cuts (defer, tq 2, malloc settings, mono sound, Mesa nv30 instead of llvmpipe):

| | MB |
|---|---:|
| game data | 106 |
| sounds (mono) | 41 |
| Mesa nv30 context, SDL, OpenAL, GL buffers (1.5 MB of VBOs) | ~20 |
| other heap, thread stacks | ~10 |
| anon total | ~175 |
| file-backed code (binary 4, libs shared with the desktop) | ~30, evictable |

About 175 MB anon against the ~160 MB "no stalls" bar and 217 MB MemTotal: borderline, workable with
the cold data in swap (conversations, unvisited ship/outfit data), not comfortable. Without the sound
patch ~215 MB, too much.

VRAM: the 3D-texture route cannot take Mesa 0038 (it only compresses glTexImage2D of GL_TEXTURE_2D,
power-of-two sizes), so sprites stay RGBA8: ~235-270 MB resident with defer, over the 224 MB VRAM and
it is also the swap tier. That route is out. The 2D-atlas route below takes DXT5: computed from the
image headers (frames packed into one 2D texture up to 4096, padded to power of two):
resident set (effect, ship, ui, asteroid, _menu, projectile, map...) 91 MB DXT5 (361 MB RGBA8 padded),
plus on-demand star/planet/land/thumbnail/outfit while in a system or shop: +10 to 30 MB. So ~100-120 MB
of VRAM, about the same as BananaBread (Debian's cube2).

Startup: `--parse-assets` (all data, images, sounds, no upload, defer on) takes 5.4 s user on a modern
x86 core; PPE factor 8-15x gives 45-80 s to the menu. One time, acceptable.

## 5. Renderer work (GL2 path)

State upstream (0.11.2 and master 2026-10-18): GameWindow.cpp:242-247 retries a non-core context and,
for a GL_VERSION starting with "2", calls OpenGL::DisableOpenGL3(): no VAO, GL_TEXTURE_3D instead of
GL_TEXTURE_2D_ARRAY (Sprite.cpp:38), EXT FBO. There is no shipped GL2 shader set; the game takes
`shaders/*.vert|.frag` from plugins (GameData::LoadShaders, plugin shaders override by name) and prefixes
`#version <driver GLSL version>`.

**I wrote the 22 shaders and ran the game on them: gl2-plugin/ (plugin.txt + shaders/).** (That plugin, with the sprites in 3D textures, was the first version: the package's patch 0006 ships the shaders in `shaders-gl2/`, rewritten for the 2D atlas of patch 0004, so the plugin directory (endless-sky/gl2-plugin/ in the docs' research folder) was deleted on 2026-10-26; git has it.) Method: sed
(`in`->attribute/varying, `out`->gl_FragColor, `texture`->texture2D, drop precision/highp/mediump,
`2.f`->`2.0`) plus hand edits for the three array users (sprite.frag, batch.frag, outline.frag): sampler3D
and a helper `L(uv, layer) = vec3(uv, (layer+0.5)/frameCount)` for the slice centre; the swizzle mask uses
`(first+0.5)/frameCount` when uniqueSwizzleMaskFrames > 0, else 0.5. Run with
`MESA_GL_VERSION_OVERRIDE=2.1 MESA_GLSL_VERSION_OVERRIDE=120` on llvmpipe (test tool only): "Experimental
OpenGL 2 support has been enabled", all 22 shaders compile as `#version 120` (one fix: no highp), menu,
new pilot, shipyard (outlines), planet panel, departure, flight with starfield, HUD, radar and asteroids, and
the starmap with its fog map all render and the game held 60 fps there (screenshots in results/).
Nothing of the C++ was touched. What this does not prove: nv30's own limits.

What nv30 lacks and what to do:
1. 3D textures: GL_MAX_3D_TEXTURE_SIZE 512 on nv30. From the headers, ~43 sprites exceed 512 in width or
   height after halving (star 23 = 159 MB, effect 4, planet 2, ui 1, ...), so a part of the art cannot be
   a 3D texture; no DXT either (above). **Use a 2D grid atlas instead of a 3D texture**: Sprite.cpp
   AddBuffer lays the frames out in a cols x rows grid (cols*w and rows*h up to 4096, padded to POT,
   GL_TEXTURE_2D, DXT via 0038), Sprite stores cols/rows, and the three array shaders compute
   `uv = (vec2(col, row) + uv) / vec2(cols, rows)` with the coordinate clamped half a texel inside the
   cell. C++ side: AddBuffer (about 40 lines), SpriteShader/BatchShader/OutlineShader uniforms (one vec2
   each) and the same for Sprite::Texture users. 0038 needs its POT rule kept (hence the padding) or an
   NPOT test.
2. R8 fog map (FogShader.cpp:207-212, GL_R8/GL_RED): no ARB_texture_rg on nv30: GL_LUMINANCE8 + GL_LUMINANCE,
   the shader reads `.r` already (one line; it worked on llvmpipe because that has RG).
3. FBO: EXT path present; glClearBuffer is guarded. RenderBuffer.cpp uses glDrawBuffers(1): fine.
4. Fragment program size: outline.frag does 2 x 9 x 8 = 144 texture fetches in constant loops (shop ship
   outlines, HUD outline); sprite.frag's blur is 2 x 11 fetches. 4096-instruction limit is fine, but
   nv30's temporaries and texture-instruction limits are UNVERIFIED for outline.frag. Fallback: the
   preferences "Ship outlines in shops/HUD" off, or 4 taps instead of 9.
5. Draw load (GL2 run, apitrace of this exact path, 7866 frames, results/frames.txt): flight ~600 draws and
   ~4300 GL calls per frame (2300 glUniform, 140 glBindTexture, 250 glUseProgram); shipyard 1400-2200 draws
   (ship outlines); menu 300-400. Calibration from X-Moto on the PS3 (1960 draws / 18,500 calls = 44.5 fps,
   about 22 ms per frame): ~1.2 us per call, ~11 us per draw all in, so ~7 ms of GL submission per flight
   frame. The sim (ships, AI, projectiles at 60 Hz; no Lua) is light C++; not measured on a PPE.
   ESTIMATE: 40-60 fps in flight at 1080p, 60 likely at 720p; no batching needed to start. A sprite batcher
   (BatchShader exists) is the known fix if it is short.

## 6. Big-endian (read in source; the Debian ppc64 package is BE)

Text data (DataFile, saves, prefs) is endian-neutral, PNG/JPG go through libpng/libjpeg, mp3 through libmad
(native-endian samples). **Four real bugs**, all assuming little-endian uint32 pixels or LE samples:
- image/ImageBuffer.cpp:545 Premultiply (alpha = value & 0xFF000000: on BE that is R; runs on every PNG): all
  sprites would be wrong. Fix: byte access (p[0..3]) or an endian-dependent shift.
- image/Mask.cpp:31 `on = 0xFF000000` (alpha test for collision outlines) and :157.
- text/Font.cpp:279 `mask = 0xFF000000` (kerning from alpha): wrong glyph advances.
- audio/Sound.cpp:~60 reads WAV samples with reinterpret_cast<int16_t*>: BE plays noise. Needs a byte swap
  (fold into the mono patch).
Header parsing in Sound.cpp is byte-wise and fine. S effort (~30 lines), testable on the PC with
qemu-ppc64-static if wanted; I did not run a BE binary.

## 7. Verdict: GO, effort M (about 1.5-2 weeks), with a memory risk that decides it in step 4

Why go: the feared 700 MB is a 24-core glibc artifact plus textures that belong in VRAM; the game already
has lazy sprite loading and a GL2 path; the 22 shaders are done and run; the BE bugs are four small
spots; the draw load is the same class as X-Moto. Why only conditional: ~175 MB of XDR is at the edge of
the budget, VRAM only works through the 2D atlas + DXT route (new C++), and nv30 shader limits for the
outline filter are untested.

First steps, in order (each is a stop point):
1. ppc64 chroot: install Debian's endless-sky 0.11.2 ppc64 and endless-sky-data; run `--parse-assets` with
   the defer pref, `--tq-threads 2`, the two malloc env settings: record RSS/anon and the load time on
   the PS3 (user runs it; no SSH from me). Compare with the 175 MB estimate. If anon is over ~220 MB, stop.
2. Package our build with the BE patches (Premultiply, Mask, Font, Sound LE + mono) and the GL2 plugin
   as a drop-in; `mallopt` in main().
3. nv30 smoke: menu and flight with the 3D route first (no code, shows the shaders compile on nv30 and
   whether outline.frag fits); expect the big star/planet sprites and the fog map to fail there.
4. Write the 2D-grid atlas in Sprite.cpp + shader uniforms, R8 -> LUMINANCE, then switch on NV30_FORCE_DXT=all
   (linear filtering is the default pref, so 0038 takes them) and measure VRAM (expect 100-120 MB) and
   stalls in flight with the usual protocol (warm-up, webcam).
5. If fps is short: sprite batcher; offer shaders and atlas upstream.

Open items: PPE sim cost; nv30 compile of outline.frag; the atlas border clamp look; sound latency with
OpenAL Soft on the PS3 (user judges by ear); NV30 NPOT DXT (avoided by padding).
