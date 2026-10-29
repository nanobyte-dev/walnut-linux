# Backlog: everything not tried yet, ranked

This is everything we have not tried, and everything built but never measured, ranked across the
whole project. The order follows the user's rule of 2026-10-18: smoothness (stalls, 1% low, the
longest frame) ranks above average fps, so an item that removes stalls outranks one that adds a few
fps. Items that protect the console or its data sit with the first ones, and release work comes last
(the user, 2026-10-18 evening). Each line links to its full entry in an area file (the evidence,
effort, risk, what it needs and how to start); what is built and measured is in the reference docs
([docs/README.md](../README.md)), the stage plan and its open decisions D1-D14 in
[PLAN.md](../PLAN.md), the test queue in [TESTS.md](../TESTS.md).

State as of 2026-10-19 morning (TESTS.md through batch ssd1 and tonight's SSD-root batches).

**How to read a line.** Effort: S = a day or less, M = days, L = weeks. Needs: **PS3** = runs on the
console with nobody at it (the measurement protocol), **user** = the user at the console or TV
(reboots, listening, the controller, decisions), **host** = the PC only. A **wedge** (a graph
exception that stops the RSX until a console reboot) is the risk named most often in the area
files. "PLAN" is the stage item number in PLAN.md, "Was" the number the item had in the old
IDEAS.md. Terms: [glossary](../README.md#glossary).

Area files: [smoothness.md](smoothness.md) (stalls, loads, the disk, tools), [memory.md](memory.md)
(RAM, VRAM, compression), [graphics.md](graphics.md) (Mesa nv30, ps3rsx, the RSX),
[spu.md](spu.md), [games.md](games.md) (what the games found across them, and the index of the
games' own backlogs), [platform.md](platform.md) (storage, game mode, desktop, release),
[hardware.md](hardware.md) (backups, fan, lv1, out of scope). A line about one game links that
game's backlog, `gamedb/games/<id>/todo.toml`, where its details are.

## The ranked list

| # | Item | Payoff | Effort | Needs | PLAN | Was |
|---|---|---|---|---|---|---|
| 1 | [The kernel guard on the PS3](platform.md#the-kernel-guard-on-the-ps3) | GameOS's disk safe while the SSD root writes to it every boot | S (built) | host, user, PS3 | - | - |
| 2 | [SSD root against USB root](smoothness.md#ssd-root-against-usb-root) | loads and disk stalls: the disk is 34-41% of every load | M (built) | user, PS3 | 1.14 | #27 |
| 3 | [Texture compression at upload for every game](memory.md#texture-compression-at-upload-for-every-game) | texture VRAM /4 to /8 in the VRAM-bound games | M (built) | PS3, user | 2.4 | #4 |
| 4 | [VRAM budget in game mode](memory.md#vram-budget-in-game-mode) | fewer swap stalls in the heavy games | S-M | PS3, host | 2.3 | #5 |
| 5 | [Console frame release (local/0043) on the PS3](memory.md#console-frame-release-local0043-on-the-ps3) | +8 MB of RAM during every game | S (built) | PS3, user | 1.9 | #14 |
| 6 | [Game mode at the TV](platform.md#game-mode-at-the-tv) | the path every player takes, checked | S | user | 1.10 | #3 |
| 7 | [A benchmark round through game mode](smoothness.md#a-benchmark-round-through-game-mode) | whether players get the measured smoothness | M | PS3 | 2.2 | - |
| 8 | [The single-run results, repeated](games.md#every-games-open-items) | defaults on solid ground (BananaBread `oqlag` 39.4 against 25.0 in one run): a benchmark item in each of five games | M | PS3 | 2.6 | - |
| 9 | [SuperTuxKart's missing sound](../../gamedb/games/supertuxkart/todo.toml) | a playable game; unblocks D4 | S-M | user | 2.1 | #26 |
| 10 | [dsda-doom's chopping music](../../gamedb/games/dsda-doom/todo.toml) | two games' music; unblocks D3 | S-M | PS3, user | 2.8 | #8 |
| 11 | [XMB-side backups](hardware.md#xmb-side-backups) | a way back: the EEPROM half has no backup | S | user | 4.1 | - |
| 12 | [Validator checks for the vertex texture methods](graphics.md#validator-checks-for-the-vertex-texture-methods) | closes a wedge any client can cause | S | host, PS3 | 4.6 | #20 |
| 13 | [Start-up and warm-up after the disk](smoothness.md#start-up-and-warm-up-after-the-disk) | seconds off loads, fewer warm-up stalls | S-M | host, PS3 | 1.7 | #27 |
| 14 | [Long-session soak](smoothness.md#long-session-soak) | stalls and growth only long play shows | S-M | PS3, user nearby | 2.7 | - |
| 15 | [Power paths of local/0041](platform.md#power-paths-of-local0041) | a shutdown that can't hang | S | user | 1.8 | - |
| 16 | [Game-side batching in the remaining games](graphics.md#game-side-batching-in-the-remaining-games) | fps where a game is CPU-bound | M per game | host, PS3 | 3.1 | #6 |
| 17 | [ps3rsx shadow_fast](graphics.md#ps3rsx-shadow_fast) | 0.6-1.5% CPU in every game (measured: no gain) | S (built, measured) | PS3 | 1.9 | - |
| 18 | [Tiling and Z-cull](graphics.md#tiling-and-z-cull) | +10-30% in fill-bound scenes (est.) | M-L | host, PS3 | 3.2 | #19 |
| 19 | [Luanti: view40 as an option, and its memory](../../gamedb/games/luanti/todo.toml) | a 59 fps option, RAM | S-M | PS3, host | 2.9 | #2 |
| 20 | [OpenArena opengl2: what is left](../../gamedb/games/openarena/todo.toml) | a few MB, shorter reloads | S-M | host, PS3 | 3.7 | - |
| 21 | [The 7th SPE](spu.md#the-7th-spe) | one more SPE, no write of any kind | S | user, PS3 | 4.2 | #7 |
| 22 | [Indirect constant addressing (ARL) in vertex programs](graphics.md#indirect-constant-addressing-arl-in-vertex-programs) | usable GPU skinning, smaller shaders | M-L | host, PS3 | 3.4 | #11 |
| 23 | [RSX and SPU probes: E8b, E16, E17](graphics.md#rsx-and-spu-probes-e8b-e16-e17) | enabling: SPU writes into VRAM, WC mappings | S each | PS3 | 3.3 | #12 |
| 24 | [The 720p console](memory.md#the-720p-console) | 4 MB always, 2.25x fewer pixels | S | user's decision (D2) | 4.10 | #14 |
| 25 | [The interference detector's own checks](smoothness.md#the-interference-detectors-own-checks) | trust in every row since batch 15 | S | user | 1.13 | - |
| 26 | [Fan: the floor, the panic path, the steps not run](hardware.md#fan-the-floor-the-panic-path-the-steps-not-run) | the fan's exit paths proven | S | user (D5) | 4.4 | - |
| 27 | [Retire the stopgap kicker](platform.md#retire-the-stopgap-kicker) | cleanup | S | host (D13) | 1.2 | #1 |
| 28 | [Red Eclipse engine patches](../../gamedb/games/redeclipse/todo.toml) | full sound and settings | M | host, PS3 | 3.8 | - |
| 29 | [Extreme Tux Racer's last frame to 60](../../gamedb/games/etr/todo.toml) | 60 fps in a light game | S to look | host, PS3 | - | - |
| 30 | [Kernel stat options and ps3rsx counters](smoothness.md#kernel-stat-options-and-ps3rsx-counters) | exact stall causes instead of samples | S-M | host, a reboot | 3.11 | - |
| 31 | [The dcbz memcpy trap](graphics.md#the-dcbz-memcpy-trap) | the desktop's CPU drawing into VRAM | S check, M fix | PS3 | 4.14 | #9 |
| 32 | [OpenTTD's music and its gameplay benchmark](../../gamedb/games/openttd/todo.toml) | music that doesn't chop, a real benchmark | M | PS3, user | 2.8, 3.1 | - |
| 33 | [Smoke runs: menu-only games, Nexuiz, the new games](games.md#every-games-open-items) | more games for the suite | S | user, PS3 | 1.11, 1.12 | - |
| 34 | [SSD root follow-ups](platform.md#ssd-root-follow-ups) | the SSD root without the USB disk | M each | host, PS3, user | 1.14 | - |
| 35 | [Mesa 0039: nv30's shader cache A/B](smoothness.md#mesa-0039-nv30s-shader-cache-ab) | at most 0.4 s of a load | S (built) | PS3 | 1.7 | - |
| 36 | [Sound straight to ALSA](memory.md#sound-straight-to-alsa) | 0.5-3 MB per game plus the server | M | PS3 | 3.6 | - |
| 37 | [Cheaper Mesa 0028 and 0031](graphics.md#cheaper-mesa-0028-and-0031) | about a frame each in [X-Moto](../../gamedb/games/xmoto/) | S-M | host, PS3 | 3.9 | #10 |
| 38 | [ADPCM sound effects in ioquake3](../../gamedb/engines/ioquake3/todo.toml) | ~5 MB in OpenArena | M | host, PS3, user | 3.9 | #16 |
| 39 | [Mesa built with nv30 only](memory.md#mesa-built-with-nv30-only) | 0.3-0.5 MB per game (est.) | S-M | host | 3.5 | - |
| 40 | [Neverball: a longer replay](../../gamedb/games/neverball/todo.toml) | a row to trust | S | user, PS3 | 3.10 | - |
| 41 | [Read-only probes of lv1 and the firmware state](hardware.md#read-only-probes-of-lv1-and-the-firmware-state) | the yes/no on lv1 patches at run time; a few MB | S | user's OK (D8), PS3 | 4.3 | #15 |
| 42 | [BananaBread: texreduce](../../gamedb/games/bananabread/todo.toml) | ~45-60 MB of VRAM, blurrier textures | S | PS3 | - | - |
| 43 | [Smaller vertex data](memory.md#smaller-vertex-data) | vertex buffers /2 | M per engine | host, PS3 | 3.9 | #17 |
| 44 | [Shader IR dropped after linking](memory.md#shader-ir-dropped-after-linking) | unknown, measure first | M | host | 3.9 | #18 |
| 45 | [Audio libraries on the PPE](spu.md#audio-libraries-on-the-ppe) | measured in ssdroot5: OpenAL +2% on OpenArena, FluidSynth reverb +3% (effects off +18%), SuperTuxKart load -0.7 s (vorbis); the installed defaults since 10-20; left: the stable publish | S (built, measured) | PS3, user | 3.9 | #22 |
| 46 | [CPU hot paths left over](graphics.md#cpu-hot-paths-left-over) | about 1-2% each | S-M | host, PS3 | - | - |
| 47 | [Bigger reads on the USB disk](smoothness.md#bigger-reads-on-the-usb-disk) | small; USB roots only | S | PS3 | - | - |
| 48 | [Gaps in the measurement tools](smoothness.md#gaps-in-the-measurement-tools) | numbers to trust | S each | host, PS3 | - | - |
| 49 | [SPU meshing for Luanti and ClassiCube](games.md#every-games-open-items) | Luanti's load stalls, [ClassiCube](../../gamedb/games/classicube/)'s chunks | L | host, PS3 | 3.12 | #13 |
| 50 | [Vertex texture fetch, derivatives, instancing](graphics.md#vertex-texture-fetch-derivatives-instancing) | none for today's games | M each | host, PS3 | 3.12 | #21 |
| 51 | [Driver limits not pushed yet](graphics.md#driver-limits-not-pushed-yet) | wedge recovery, finer GART, VRAM above 224 MB | M-L | PS3 | - | - |
| 52 | [GL extensions the games worked around](graphics.md#gl-extensions-the-games-worked-around) | none today (workarounds exist) | M each | host, PS3 | - | - |
| 53 | [DualShock 3 in play](platform.md#dualshock-3-in-play) | the console from the sofa | M | user | 4.7 | - |
| 54 | [Wi-Fi from nm-applet, NetworkManager on eth0](platform.md#internal-wi-fi) | the network applet works | S | user | 4.8 | - |
| 55 | [Display variety](platform.md#display-variety) | TVs other than one 1080p monitor | M | user, a TV | 4.10 | - |
| 56 | [Hardware cursor and tearing](platform.md#hardware-cursor-and-tearing) | desktop polish | M | user | 4.11 | - |
| 57 | [Remote Start by Wake-on-LAN, idle auto-off](platform.md#remote-start-by-wake-on-lan-idle-auto-off) | fewer XMB trips, no forgotten console | S-M | user | 4.9 | #23 |
| 58 | [Desktop applications](platform.md#desktop-applications) | the desktop as a user meets it | M | PS3 | 4.14 | - |
| 59 | [GameOS auto-mount at boot](platform.md#gameos-auto-mount-at-boot) | convenience | S | user's decision (D9) | 4.5 | - |
| 60 | [Dev loop questions](platform.md#dev-loop-questions) | fewer XMB trips per wedge | S each | user | 4.15 | #23 |
| 61 | [A Linux region on the internal SSD](platform.md#a-linux-region-on-the-internal-ssd) | a region instead of an image file | L | user's decision | 4.12 | - |
| 62 | [Kernel config: the last few hundred KB](memory.md#kernel-config-the-last-few-hundred-kb) | under 1 MB | S | host, a reboot | - | - |
| 63 | [Kernel patches surveyed, not ported](platform.md#kernel-patches-surveyed-not-ported) | small fixes | S | host, a reboot | - | - |
| 64 | [libspeq leftovers and SPU work after E16](spu.md#libspeq-leftovers-and-spu-work-after-e16) | depends on E16 | M-L | host, PS3 | - | - |
| 65 | [Release image session and the stable publish](platform.md#release-image-session-and-the-stable-publish) | the games reachable from the stable suite | M | user, host | 5.1, 5.4 | - |
| 66 | [Upstream submissions](platform.md#upstream-submissions) | fixes for others, less for us to carry | S each | user (D10) | 5.8 | - |
| 67 | [The GameOS installer](platform.md#the-gameos-installer) | an install without a PC shell | L | host, PS3 | 5.3 | - |
| 68 | [A repository for others](platform.md#a-repository-for-others) | others can install | M | host, user | 5.2 | - |
| 69 | [End-user docs](platform.md#end-user-docs) | others can set it up | M | host | 5.5 | - |
| 70 | [Security update policy](platform.md#security-update-policy) | users know what gets fixed | S | host | 5.6 | - |
| 71 | [Other PS3 models](platform.md#other-ps3-models) | beyond one CECH-20xx | M | host | 5.7 | - |
| 72 | [OpenAL's stuck mixer: the sound card's DMA stops](games.md#openals-stuck-mixer-the-sound-cards-dma-stops) | sound back in four games, honest numbers | done (kernel local/0047, in the kernel since .648) | PS3 | 2.1 | - |
| 73 | [BananaBread data files, and a clean SuperTuxKart config](games.md#every-games-open-items) | picture closer to the PC's, a clean log | S | host, PS3 | - | - |
| 74 | [Mesa nv30 messages in the logs](games.md#mesa-nv30-messages-in-the-logs) | fallbacks we cannot see now | S | PS3 | - | - |
| 75 | [A failed buffer allocation must not crash](graphics.md#a-failed-buffer-allocation-must-not-crash) | no segfault at the GART/VRAM limit | M | host, PS3 | - | - |
| 76 | [Games to smoke next, and Wesnoth's music](games.md#every-games-open-items) | novel games for the suite | S each | PS3 | - | - |
| 77 | [The SSD root image is nearly full](platform.md#the-ssd-root-image-is-nearly-full) | room for more games | M | user | - | - |
| 78 | [ps3disk multi-region (packages/patched/kernel/linux/patches/ps3/0010): two bugs](platform.md#ps3disk-multi-region-packagespatchedkernellinuxpatchesps30010-two-bugs) | a failed probe and a remove that are safe | S | host, user | - | - |
| 79 | [ps3fb's exception dump prints the wrong FIFO entries](graphics.md#ps3fbs-exception-dump-prints-the-wrong-fifo-entries) | wedges diagnosable from one dump | S | host, a reboot | - | - |
| 80 | [Skia on big-endian](platform.md#skia-on-big-endian) | WebKitGTK 2.54+ and its security fixes, Ladybird | L? | host | 4.16 | - |
| 72 | [Host tests run automatically](platform.md#host-tests-run-automatically) | tests that run without someone remembering | S-M | host | 5.9 | - |

Parked by CLAUDE.md, not ranked: [video decoding on the SPEs](spu.md#video-decoding-on-the-spes)
(was IDEAS #24).

## Measured, waiting for a default decision

These ran since the docs were written; what is left is to change a default or decide not to.
The per-game ones are `package` items (the `gl2min_mippowf` repeat a `benchmark` item) in their games'
and engines' todo.toml ([games.md](games.md#every-games-open-items)): [ClassiCube](../../gamedb/games/classicube/) `CC_GL_CHUNKS=1`
(+4.7%, x3), [X-Moto](../../gamedb/games/xmoto/) `XMOTO_GL_STREAM=1` (+1%, the longest frame mixed), cube2 `vanormalshort 1`
(+17%), [ioquake3](../../gamedb/engines/ioquake3/) `r_md3PackedFrames 1` (-20 MB of VRAM) and `gl2min_mippowf` (needs a repeat). Mesa
0037's `NV30_SYSMEM_COPY=2` + `NV30_INVALIDATE_BUFFER=1` for games we don't patch is in
[games.md](games.md#a-mesa-default-for-the-games-we-dont-patch).

## Not worth it

Measured:
- **The game prefetch** on the USB disk (gamedb:batch/2026-10-18-batch16): no gain, [Luanti](../../gamedb/games/luanti/) worse (load +1
  to +4 s, two runs with 3 s hitches); off by default since b85d0b9, the code kept for a faster disk.
- **Mesa 0035 `NV30_QUERY_WAIT`**: no fps gain in the repeat (row 65: 23.0/33.4 against 26.0/37.9),
  CPU lower; off. **0034's fence pool**: neutral (row 58). **0036's staging pool**: same fps, CPU -4%
  (row 64); off.
- **`vm.watermark_boost_factor=0`** on Luanti: no clear gain (row 62).
- **The malloc presets** (`MALLOC=trim`, `mmap`) on [BananaBread](../../gamedb/games/bananabread/) (Debian's cube2) and [OpenArena](../../gamedb/games/openarena/): the rounds disagreed,
  read as noise (row 61). **Luanti `trim16`**: more stalls, 3.35 -> 8.6 s (row 57).
- **Luanti `cliunload`**: worse, 3.8 and 5.2 s of stalls with 2 s reload hitches against base's 1.5
  and 2.1 s (row 67). **Luanti `lean`**: a black void; `chunk3` is the cause, and `srvunload` and
  `active2` render but stall more (6.3 and 6.7 s against ~2.5, row 68).
- **BananaBread's skinning on the SPEs**: 23.6 fps with 21 stalls (4.3 s) against libspeq's PPE path
  at 28.1 with none; CPU skinning stays the default.
- **OpenArena's lightmap atlas**: any atlas page adds about 10 s of hitches (rows 51, 53); off by
  default. **OpenArena opengl1 batching**: neutral.
- **Mesa's GLSL disk cache as a load win**: no load gain (row 69); it stays on, it saves CPU.
- **dsda-doom's software renderer** (gamedb:dsda-doom): the GL renderer is the way (38 fps in software).

Reasoned:
- **An SPU mixer for OpenAL Soft**: 3.7-4.7% of a game's samples, under the 6% bar (it comes back
  if a game reaches 6%; the design is [research/OPENAL-SPU-MIXER.md](../research/OPENAL-SPU-MIXER.md)).
- **Vorbis or zlib on the SPEs** (OpenArena's effects are WAV; libdeflate on the PPE instead),
  **[OpenTTD](../../gamedb/games/openttd/)'s blitter on the SPEs** (its GL driver is built), **the validator, zram or VRAM swap
  copies on the SPEs** (the kernel can't drive them), **Mesa's per-draw paths on the SPEs**,
  **compressed swap through a userspace block device** (reclaim deadlocks), **software rendering on
  the SPEs**.
- **A cacheable PPE mapping of VRAM** (E8c): if lv1 refuses the HPTE the kernel BUG()s; ask the user
  before trying it.
- **`MALLOC_ARENA_MAX`** on games with 3-4 arenas: under 0.3 MB (the extra arenas are 0.1 MB).
- **Prelinking or less dynamic linking**: start-up linking is 0.03-0.15 s, at most 1-2% of a load.
- **Kernel memory options**: shrinking the CMA area (it costs no reserve), `PS3_HTAB_SIZE`, `MEMCG=n`
  (game mode needs `memory.low`), `FLATMEM`, 64K pages, `THREAD_SHIFT=13`, `RELOCATABLE=n`, built-in
  drivers made modules (a loaded module costs more than the same code built in).
- **Moving the console into VRAM through ps3rsx**, or T2's ps3fb-in-VRAM patch (0666): fbcon's
  scroll reads VRAM at 10 MB/s, DRM's generic fbdev keeps the same 8 MB shadow in XDR, and 0666
  takes 23 MB more VRAM and conflicts with how ps3rsx shares ps3fb's command area; local/0043 frees
  the frame instead.
- **ps3rsx retiring fences from the vsync interrupt**: not needed with the fixed kernel.
- **Freeing nouveau's buffer copies after N frames, or converting at upload**: BananaBread's world
  buffers are read every frame they are seen; a buffer's vertex layout isn't known at upload.
- **A newer CFW PUP** (no Linux gain, it outdates the NOR dump), **a custom lv1 in CoreOS**, **RSX
  overclocking** (nothing cools it under Linux).

## Postponed by the user's rules

**System trims** (CLAUDE.md "Memory trims", the user, 2026-10-17): nothing a desktop user expects is
disabled for good; the memory comes from game mode, which unloads, freezes or pauses the rest while a
game runs. So these wait: zram off once ps3vswap is up (its 1.5 MB table plus its pages), the seccomp
filters of resident services (126 BPF programs of 8K, 1.0 MB), `cfg80211` kept from loading (483K; a
USB Wi-Fi stick would stop working), `dhash_entries=8192 ihash_entries=4096` (288K), the wallpaper
out of its 8 MB shared segment for good (it leaves in game mode instead), a journal size limit.

**Side projects** (CLAUDE.md "Priorities": parked unless asked): video decoding on the SPEs (the
HEVC decoder), the power-control rig. The Core 2 + 7600 GT box serves only to test our nv30 patches
for upstream (the user, 2026-10-18); setting it up is the user's.

## Decisions (the user, 2026-10-17)

1. **System trims:** dropped. The permanent trims of the old plan go; memory comes from game mode
   (CLAUDE.md "Memory trims"), which already unloads the wallpaper and freezes the desktop.
2. **VRAM swap and game VRAM:** a measurement, not a decision: which path the heavy games take (the
   swap giving VRAM back, or GART spill) is measured before the reserve is resized
   ([memory.md, "VRAM budget in game mode"](memory.md#vram-budget-in-game-mode)).
3. **SPU queues:** one shared deadline pool of 2 SPEs for every latency-bound service (audio,
   skinning: jobs from all of them in one queue, by deadline); DXT keeps its 4 bulk SPEs, so a
   process never runs more contexts than SPEs.
4. **Same-boot baselines:** yes, the rule. Every measured change runs against its off-switch (or the
   previous build) in the same PS3 session; numbers from different boots are not compared.
5. **E8's write-combining result:** a measurement (probe E8b) before either doc is relied on.

## Decisions (the user, 2026-10-18)

1. **Smoothness above average fps.** Stalls, 1% low and longest frame decide; a lower steady fps is
   fine when it plays smoothly.
2. **Fan policy from Linux is allowed**, only through the System Manager's fan-policy service, SYSCON
   automatic at boot and on every exit path, a fixed floor of at least 60% only on request. Other
   SYSCON writes stay forbidden (CLAUDE.md).
3. **OtherOS++ patches:** dropped `/dev/ps3sysmngr` (0060), the BD vendor commands (0090), the
   Jupiter Wi-Fi driver with the eurus repository write (0170, 0160, 0316); `PS3_DISP_MANAGER` off.
4. **Open for the user** at the time: the stable publish of the READY set, [Luanti](../../gamedb/games/luanti/)'s default, the
   test-kernel boot session, the fan floor, the 7th SPE entry, [SuperTuxKart](../../gamedb/games/supertuxkart/)'s `lean` default, the
   720p console. Since then: Luanti's default is settled by the clean runs (base stays, row 67); the
   test kernels booted (.419, .433, now .472); the stable publish became "test first, then publish"
   ([archive/RELEASE-CANDIDATES-2026-10-19.md](../archive/RELEASE-CANDIDATES-2026-10-19.md), 2026-10-18 evening); the upstream drafts in docs/upstream/ wait for the
   user to send them. The current list of open decisions is PLAN.md's D1-D14.

## Where the old IDEAS.md numbers went

| Was | Now |
|---|---|
| IDEAS #1 (the idle-fix test kernel) | measured (rows 66, 68: stalls 0.4 s with the fix against 8-9 s without): [STALLS.md](../STALLS.md); left: [Retire the stopgap kicker](platform.md#retire-the-stopgap-kicker) |
| IDEAS #2 (Luanti's default) | settled, base stays (row 67); left: [Luanti: view40 as an option, and its memory](../../gamedb/games/luanti/todo.toml) |
| IDEAS #3 (prefetch, game mode's kicker) | prefetch measured, no gain (row 68): "Not worth it" above; the kicker: [Game mode at the TV](platform.md#game-mode-at-the-tv) |
| IDEAS #4 | [Texture compression at upload for every game](memory.md#texture-compression-at-upload-for-every-game) |
| IDEAS #5 | [VRAM budget in game mode](memory.md#vram-budget-in-game-mode) |
| IDEAS #6 | [Game-side batching in the remaining games](graphics.md#game-side-batching-in-the-remaining-games) |
| IDEAS #7 | [The 7th SPE](spu.md#the-7th-spe) |
| IDEAS #8 | [dsda-doom's chopping music](../../gamedb/games/dsda-doom/todo.toml) |
| IDEAS #9 | [The dcbz memcpy trap](graphics.md#the-dcbz-memcpy-trap) |
| IDEAS #10 | [Cheaper Mesa 0028 and 0031](graphics.md#cheaper-mesa-0028-and-0031) |
| IDEAS #11 | [Indirect constant addressing (ARL) in vertex programs](graphics.md#indirect-constant-addressing-arl-in-vertex-programs) |
| IDEAS #12 | [RSX and SPU probes: E8b, E16, E17](graphics.md#rsx-and-spu-probes-e8b-e16-e17) |
| IDEAS #13 | SPU meshing for [Luanti](../../gamedb/games/luanti/todo.toml) and [ClassiCube](../../gamedb/games/classicube/todo.toml) |
| IDEAS #14 | [Console frame release (local/0043) on the PS3](memory.md#console-frame-release-local0043-on-the-ps3), [The 720p console](memory.md#the-720p-console) |
| IDEAS #15 | [Read-only probes of lv1 and the firmware state](hardware.md#read-only-probes-of-lv1-and-the-firmware-state) |
| IDEAS #16 | [ADPCM sound effects in ioquake3](../../gamedb/engines/ioquake3/todo.toml) |
| IDEAS #17 | [Smaller vertex data](memory.md#smaller-vertex-data) |
| IDEAS #18 | [Shader IR dropped after linking](memory.md#shader-ir-dropped-after-linking) |
| IDEAS #19 | [Tiling and Z-cull](graphics.md#tiling-and-z-cull) |
| IDEAS #20 | [Validator checks for the vertex texture methods](graphics.md#validator-checks-for-the-vertex-texture-methods) |
| IDEAS #21 | [Vertex texture fetch, derivatives, instancing](graphics.md#vertex-texture-fetch-derivatives-instancing) |
| IDEAS #22 | [Audio libraries on the PPE](spu.md#audio-libraries-on-the-ppe) |
| IDEAS #23 | [Dev loop questions](platform.md#dev-loop-questions), [Remote Start by Wake-on-LAN, idle auto-off](platform.md#remote-start-by-wake-on-lan-idle-auto-off) |
| IDEAS #24 | parked by CLAUDE.md: [Video decoding on the SPEs](spu.md#video-decoding-on-the-spes) |
| IDEAS #25 (formats nv30 fetches) | measured in batch 17 (row 69): [MEMORY.md](../MEMORY.md); the defaults: "Measured, waiting for a default decision" above |
| IDEAS #26 | [SuperTuxKart's missing sound](../../gamedb/games/supertuxkart/todo.toml) |
| IDEAS #27 (the internal disk, load profiles) | the disk test and the start-up profiles are measured ([GAMEOS-HDD.md](../GAMEOS-HDD.md), [PROFILING.md](../PROFILING.md)); left: [SSD root against USB root](smoothness.md#ssd-root-against-usb-root), [Start-up and warm-up after the disk](smoothness.md#start-up-and-warm-up-after-the-disk) |

Older versions and the dated history: [archive/IDEAS.md](../archive/IDEAS.md),
[archive/PLAN.md](../archive/PLAN.md).
