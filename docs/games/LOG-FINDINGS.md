# What the game logs say

`games/bench/logscan.py` reads the logs of every run in `build/bench/` (602 runs of 35 games when this
was written, 16 October to 19 October 10:41 UTC) and groups the errors and warnings by game and message.
This page is the triage of what it found, worst first. Each finding names the games, one run that shows
it, what causes it, whether the player would notice, and the fix. The fixes worth doing are in
[backlog/games.md](../backlog/games.md) and [backlog/graphics.md](../backlog/graphics.md); the PS3 tests
they need are in [TESTS.md](../TESTS.md).

Run directories are `build/bench/ps3-bench-<stamp>-<game>-<variant>/`; below only the stamp and game are
given. "Since ps3.544" means runs from 2026-10-19 09:47 UTC on (Mesa 26.2.4-1+ps3.544 installed).

## The tool

    games/bench/logscan.py                      every run, per game
    games/bench/logscan.py --new                only messages that appear after --since and not before
    games/bench/logscan.py -g sauerbraten -c audio,rsx -v
    games/bench/logscan.py --by-message         one block per message, across games
    games/bench/logscan.py --min-class gl       only gl and worse
    games/bench/logscan.py --show-ignored       what the ignore list swallowed
    games/bench/logscan.py RUNDIR...            some runs (or a directory of runs)

It scans game.log, out.log, dmesg.log, Xorg.0.log and wm.log plus the facts in bench.txt (rc, ended,
sound, underruns). Messages are normalised (timestamps, numbers, addresses, the run's own path), a
line found in both game.log and out.log counts once. Classes, worst first: crash, oom, rsx, nouveau,
gl, audio, sdl, script, x, missing, game. The trend column compares against `--since` (default
20261019T09): NEW only after it, GONE only before, always, some, and `-` when the game has runs on only
one side. The ignore list (Xorg keymap warnings, the missing pipewire client.conf, X font directories,
and a few more) is at the top of the file with a reason for each; `selftest.py` checks the tool on
made-up runs.

What the logs cannot show: nothing in them reports a Mesa nv30 shader compile or link failure, an "out
of temps", a TGSI fallback or the draw module. Mesa prints those only with `NV30_DEBUG` or
`MESA_DEBUG`, which no recipe sets, so "no such lines" means "not looked for". Item 9 proposes one run
per game with the debug output on.

## Catching failures

State (2026-10-19): built and selftested on the PC, not yet run on the PS3. `DUMPON=1` (a run.sh variable or
ENVS token; off by default, never in measured runs) makes client.sh catch the first failure line:
`Wait timeout`, `mmap commit error`, `out of GART`, `out of VRAM` or `no memory to back the GART`
(`DUMPON_RE` replaces the list; as an ENVS token it can hold no space or `+`; `DUMPON_MAX`, default 1,
captures per run). The game is stopped (SIGSTOP) for the capture and let go after it. Into the run's `dump/`:
`trigger.txt` (the line and its second in the run), `proc.{status,maps,smaps_rollup,limits,nfd}`,
`threads.txt` (every thread's stat, wchan, syscall and kernel stack), `alsa.txt` (cards and every
playback substream's status, hw_params, sw_params), `gpu.txt` (ps3rsx's debugfs `ps3rsx_mm` and
`ps3rsx_hw_state`, its module parameters), `meminfo`, `vmstat`, `swaps`, `dmesg.tail` (200 lines),
`gdb.log` (the backtraces gdb can name on the PS3) and the `gcore` as `core.zst` (xz when there is no
zstd). bench.txt gets `dump=` (trigger, crash, trigger+crash, none), `dump_at=` (seconds into the run),
`dump_core=` (raw:compressed bytes); report.py shows `dump` and `wedge`. `oal_wedge=` (the Wait timeout
and mmap commit error lines) is written in every run, and logscan.py counts it as an audio finding.

PS3 prerequisites: `sudo apt install gdb` (without it the capture still writes everything but the core and
says so in `dump/setup.txt`; also `zstd` or `xz`); passwordless `sudo -n` for the kernel stacks, debugfs,
dmesg and ptrace of the game (without it those files hold the error and gdb may be refused by Yama).
Room: a game's core is some hundreds of MB raw, in `/var/tmp/ps3-bench` until copied off.

Crashes: with DUMPON the game runs under `ulimit -c unlimited` and after a death by signal client.sh
collects the core as `dump/core.crash.zst`: through `coredumpctl dump` when `/proc/sys/kernel/core_pattern`
is systemd-coredump, else the core file in the pattern's directory or the game's working directory. Which
one applies is in `dump/setup.txt` (`core_pattern=`); the system's pattern is never changed. A pipe pattern
of another handler (apport) is only noted.

Analysis on the PC: `games/bench/dumpana.sh <run dir copied from the PS3>` builds a sysroot in
`build/tmp/dumpana/` (base libraries from `build/chroot/sid-ppc64.tar`, then the packages and `-dbgsym` debs
at the versions in the run's `versions` file, from build/debs, build/aptly/pool, build/apt-cache; libc6
symbols come from debuginfod.debian.net), runs gdb on the core and writes `dump/analysis.txt`:
`info sharedlibrary`, `thread apply all bt full`, and for runs with `oal_wedge>0` the threads sitting in
ALSA/OpenAL. Packages it could not find are listed in the file's header.

The GART crash (item 2): `ps3rsx_gart` and `ps3rsx_cma` are kernel command-line parameters, not runtime
ones, so a reduced GART needs a reboot into Petitboot by the user; the repro batch runs without it first.

## Findings, worst first

### 1. OpenAL's mixer stops and the harness does not notice

Games: SuperTuxKart (6 of 21 runs since 10-18), OpenArena (6 of 75), ClassiCube (3 of 31), Luanti (2 of
38); every game that plays through OpenAL Soft's ALSA backend. SDL_mixer games never show it.

Evidence: `20261019T011809Z-supertuxkart-lean`, game.log line 85 `[ALSOFT] (EE) mmap commit error: Broken
pipe` at 11 s, then from line 95 `(EE) Wait timeout... buffer size too low?` once a second, 244 times,
from 61 s to the end at 304 s. The same shape in all 17 runs: an EPIPE 6-46 s in (the load's stall
starves the device), later one timeout a second until the run ends (first timeout 60-225 s in). bench.txt
of every one of these runs says `sound=RUNNING` and `underruns=0`.

Cause: the log line is OpenAL's `snd_pcm_wait(1000 ms)` returning with nothing consumed: the ALSA stream
is not advancing, and OpenAL's mixer thread keeps waiting. Our harness counts xruns from ALSA's
`snd_pcm_recover` message, which OpenAL's own backend never prints, so these runs read as clean. The
stream stopped after the first EPIPE; whether OpenAL's recovery leaves it prepared but not restarted, or
dmix's slave stops, is open. It fits the webcam microphone hearing only the room in 14 of 17 SuperTuxKart
clips ([supertuxkart.md](supertuxkart.md)).

Effect: sound is gone for the rest of the session in the affected runs; picture and fps are fine (the
mixer thread sleeps, so it even saves CPU, which makes OpenAL games look cheaper than they are).

Fix (backlog "OpenAL's stuck mixer", S-M): count `Wait timeout` and `mmap commit error` lines into
bench.txt (`oal_timeouts=`, `oal_epipe=`) and show them in report.py's xrun column, so a wedged run is
visible; then on the PS3 read `/proc/asound/card0/pcm0p/sub0/status` and the dmix process during a
wedge, and try `[alsa] mmap=false` (the write path recovers with `snd_pcm_prepare`) and a bigger
period against the same game in the same boot.

### 2. A failed buffer allocation ends in a segfault

Games: Sauerbraten (5 runs on 10-18, 07:29 to 11:42 UTC), SuperTuxKart (1 run, 10-17 12:53). Not seen
since.

Evidence: `20261018T072934Z-sauerbraten-base` dmesg.log lines 1-11: 193 suppressed plus ten `ps3rsx: out of
GART for a 4096 KiB BO`, then `cube2_client[408321]: segfault (11) at 0 nip 3fff93a4e704 ... in
libc.so.6[ce704...]`, bench.txt `rc=139`. `20261017T125328Z-supertuxkart-lean`: 270 `out of VRAM for a 1024
KiB BO`, game.log `Irrlicht: FBO has one or several incomplete image attachments`, `GL1RenderTarget:
Cannot render to texture`, then `segfault ... in supertuxkart`, `rc=139`.

Cause: the kernel refuses the allocation (GART or VRAM full), libdrm returns an error, and the code
above it uses a NULL result: a write of 0 bytes at address 0 inside libc's memcpy/memset, called from a
driver library (the link register is in the game's mapped libraries, not libc). The next 4 MiB buffers
Sauerbraten asks for are its vertex and texture streams. All five Sauerbraten crashes are in one four-hour stretch; none of the 40 Sauerbraten runs after it
crashed or logged a GART failure (what changed in between is not recorded in the logs).

Effect: the game dies mid-run. A player who fills GART or VRAM hits this, so it is a correctness bug even
though the benchmark no longer triggers it.

Fix (backlog "A failed buffer allocation must not crash", M): find the caller with a debug-symbol
backtrace (a core from `ulimit -c`, or gdb, on a forced failure: `ps3rsx_gart` on the kernel command line sets the GART size; lower it and run Sauerbraten), make nv30's buffer and transfer creation return
GL_OUT_OF_MEMORY or fall back to a smaller/CPU path, and send it upstream with the test.

### 3. GART nearly full in OpenTTD since ps3.544 (new)

Games: OpenTTD (1 of 13 runs since ps3.544, flagged NEW).

Evidence: `20261019T102932Z-openttd-base` dmesg.log: five `ps3rsx: no memory to back the GART (56 of 63
MiB in use)` within a second at 37683 s; genv `NV30_STAGING_POOL_MB=24`. The run still finished
(`ended=window`) and the other twelve runs are clean.

Cause: OpenTTD uploads an 8 MB staging buffer per frame; the 24 MB pool (Mesa 0040) and the rest of
the process's buffers reach the 63 MiB GART when something else (a second 4 MB buffer, the title
screen's textures) arrives at the same moment. The failure is a different path from item 2: the kernel
cannot get pages to back the GART (CMA chunks), not a full aperture.

Effect: none seen in the picture; the draw that asked for the buffer may have been dropped. One run in
13, so rare, but it is the only new kernel-side message since the Mesa change.

Fix: none needed yet. Repeat OpenTTD `blit8` with `NV30_STAGING_POOL_MB=24` x6 and watch dmesg (TESTS
Next); if it repeats, cap the pool at 16 MB or have the pool release on a failed allocation.

### 4. Audio underruns in every SDL and direct-ALSA game

Games and counts per 2-minute window, runs since 10-19: dsda-doom 2793, OpenTTD 0-4020 (mean 3600 with
music on, 13 runs since ps3.544), DDNet 1765, X-Moto 200-218, Sauerbraten 60-181 (14 runs, mean 137),
Freeciv 87, OpenTyrian 71, Warmux 26, ScummVM 20, LinCity-NG 16, UQM 10, Flare 5. Earlier days: Neverball
780 over 13 runs, Armagetron 1223 over 29, Chocolate Doom 2701, Crispy Doom 1016, Red Eclipse 1280.

Evidence: `ALSA lib pcm.c:8787:(snd_pcm_recover) [error.pcm] underrun occurred` in game.log, repeated;
bench.txt `underruns=N`.

Cause: the SDL/ALSA callback thread misses its deadline because the PPE is busy or stalled; known and
measured for dsda-doom and OpenTTD ([SPU-AUDIO.md](../SPU-AUDIO.md), [doom.md](doom.md),
[openttd.md](openttd.md)). Sauerbraten's 60-181 a run is a small number for a game the microphone found silent; the log has no sound error for it since 10-17 (the
nine early runs that said `failed to load sample` were missing data files, fixed).

Effect: audible chopping, strongest in dsda-doom and OpenTTD music. Nothing new since ps3.544.

Fix: already on the list ([backlog spu.md](../backlog/spu.md), games.md rank 32, memory.md "Sound
straight to ALSA"). The one thing logscan adds is a way to compare: `logscan.py -c audio --by-message`.

### 5. Texture and buffer pressure shows as "out of VRAM" lines

Games: DDNet (95 lines in its first run, 10-19 09:53, since ps3.544), SuperTuxKart (270 in one run). The
other games have none.

Evidence: `20261019T095332Z-ddnet-base` dmesg.log: `ps3rsx: out of VRAM for a 4096 KiB BO` and 4196 KiB
ones, `underruns=1765`.

Cause: DDNet asks for 4 MB buffers while VRAM is already held by other clients and swapped-in pages
(not looked into further); the driver falls back to GART or swap and keeps going, which is its designed path
([memory.md](../MEMORY.md), VRAM swap). The line is the fall-back being taken, not a failure by itself;
it turns into item 2 when GART is full too.

Effect: possible stalls while pages move; not measured for DDNet (one smoke run, no stalls recorder).

Fix: put DDNet in the next VRAM-pressure batch (TESTS Next: stalls recorder on, NV30 force-DXT).

### 6. SuperTuxKart runs on the fixed pipeline, with config noise

Evidence: every run: `irr_driver: Using the fixed pipeline (old GPU, or shaders disabled)` and `OpenGL:
OpenGL version is too old!` (26 of 27); 25 runs print 30 `User Config: Unknown value for X; expected true
or false` and `Couldn't find parameter group ...`; `SFXBuffer: Positional audio is not supported with
stereo files` for WilhelmScream.ogg and a car sound.

Cause: STK 1.5's modern renderer needs GL 3.3; nv30 reports 2.1, so the legacy renderer is chosen
(designed, [supertuxkart.md](supertuxkart.md)). The config errors come from STK reading the v8
`config.xml` our recipe writes with only the attributes it sets: the groups it expects (address-history,
server-bookmarks, ...) are missing and the booleans are read from absent attributes; all keep their
defaults. The two stereo sounds play without positional audio.

Effect: none on the picture. The fixed pipeline is the reason for the 13-21 fps; the config noise is
cosmetic.

Fix: none for the first. Write a full default config once (let STK write it, keep it) so its log is
clean and the next error in it shows (S, host).

### 7. Sauerbraten and OpenArena: missing content

Sauerbraten, 65-74 of 74 runs: `could not read "data/blendbrush.cfg"`, `"data/heightmap.cfg"` (editor
files), `could not load texture packages/gk/fantasy/skyfantasy/skyfantasy` (73 runs) and `could not load
envmap skyboxes/morning` (65 runs, 910 lines). If the benchmark map uses them, its sky and reflections use a fallback and the picture differs from a PC's (check on a photo). Check the
package for the files (backlog "Sauerbraten data files", S, host). The `could not load model` and `failed to load sample` (8) lines appear only in nine runs of 10-17 and are gone since.

OpenArena, 143 of 147 runs: six `Failed to load sound ...wav` / `Using default sound` pairs
(vulcan impacts, nailgun, yousuck, cl_ammoregen). Probably upstream data gaps (not compared with a PC run yet); the engine plays a default sound. Also `Couldn't read q3history`, a first-run file.

### 8. Input lag messages from libinput, and a few harness things

- `(EE) event1 - Logitech USB Receiver Mouse: client bug: event processing lagging behind by Nms, your
  system is too slow` and `scheduled expiry is in the past`: 12 runs from 10-17 to 10-18 (Sauerbraten,
  OpenArena, Luanti, ClassiCube). Xorg's input thread starved during stalls; the mouse pointer would hang.
  Not seen on 10-19. Nothing to fix beyond the stalls themselves.
- 16 runs (all with the `perfload` harness, 10-18 17:42 to 10-19 01:56) print `client.sh: line N:
  /dev/shm/ps3-bench-<stamp>-<game>/.pid: No such file or directory` 4 to 9 times. The runs completed
  and were not interfered, but the load profile may be missing its pid. Backlog "Gaps in the measurement
  tools" (S).
- OpenArena, 5 runs: `DOUBLE SIGNAL FAULT: Received signal 11` right after `Client Shutdown (Received
  signal 15)`: ioquake3 crashes while shutting down on the harness's SIGTERM, after the window. Harmless
  to numbers.
- Kernel `os_area_queue_work_handler: Could not update FLASH ROM`: 2 runs (Extreme Tux Racer 10-18
  00:54, OpenArena 10-18 07:50). The kernel tried to write the PS3's OS area and failed. Probably the
  clock sync writing the RTC offset. Nothing was written, which is the safe outcome, but find out what
  triggers the write (S, reading code only), since the project rule is no flash writes beyond the
  install.
- `bench.txt: run ended=killed` (Quakespasm, dsda-doom, Armagetron, 10-16 and 10-17): the harness had to kill the X server (`xinit: unexpected signal`); not seen since.

### 9. Not in the logs, worth adding

Mesa nv30 shader and fallback messages (see "The tool"): run each light game once with `NV30_DEBUG` for
compile failures and the draw-module fallbacks, the log into game.log, then `logscan.py -c nouveau,gl`
(backlog "Mesa nv30 messages in the logs", S, PS3).

### 10. Old, not seen since (kept so a return is recognised)

| What | Game, run | Status |
|---|---|---|
| GPU wedge: `nouveau: kernel rejected pushbuf`, 89 lines and 35 000 dump lines, `ps3rsx: graph exception`, `ps3fb_vsync_interrupt: graphics exception` | Neverball smoke, 10-16 21:54 | no recurrence in the 12 Neverball runs since |
| validator `rejected push: swizzled render target 1 pixel wide` | gl-genmip probe, 10-16 | one probe run; not repeated |
| oom-killer | Sauerbraten 10-17 08:17, Red Eclipse 10-18 06:00 | not repeated in the Sauerbraten runs after it; Red Eclipse still needs its patches ([backlog games.md](../backlog/games.md#red-eclipse-engine-patches)) |
| `Sys_LoadLibrary failed: libcurl.so.4` | Yamagi Quake II, 6 runs | optional download library, harmless |
| `QUAKE ERROR: You must have the registered version to use modified games` | Quakespasm, 10-16 (a launch-option error, one run) | one run; not repeated |
| `Unable to load driver 'sdl-opengl': Single element texture formats not supported` | OpenTTD 10-16 | worked around (item 11) |

### 11. Noise that is harmless

- `GL_ARB_framebuffer_object / texture_float / seamless_cube_map / texture_compression_bptc not found`
  and `SDL_GL_CreateContext() ... Core context failed` (OpenArena, 57 runs): ioquake3 probes for
  extensions nv30 does not have and carries on.
- `OpenGL: Single element texture formats not supported, using luminance textures` (OpenTTD, 56 runs):
  nv30 lacks `GL_ARB_texture_rg`; OpenTTD's own fallback works ([backlog graphics.md](../backlog/graphics.md#gl-extensions-the-games-worked-around)).
  `GL_ARB_shader_texture_lod unsupported` (OpenTTD, 3 runs) is gone since 10-19 02:25.
- No IPv6 in the kernel: OpenArena, DDNet, Teeworlds, SuperTuxKart print socket errors for it.
- First-run files: tyrian.cfg/.sav (OpenTyrian), widelands config, DDNet storage.cfg, Teeworlds settings,
  hedgewars "Settings file not found", UQM's voice add-on, `ALSA lib seq_hw.c ... /dev/snd/seq` (no
  sequencer device; fluidsynth games), `Could not query RTKit`, `Failed to create PipeWire event context`.
- Luanti: `Maximum lag peaked`, `active block modifiers took Nms`, `Field "image" on TileDef is
  deprecated` (minetest_game's own).
- speq (libspeq) statistics lines print "errors" in their text; only non-zero counts are kept by the tool.
  Sauerbraten 10-17 and 10-18: 7 runs with `speq: s3tc dxt1/dxt5 ... Operation not supported` (the SPE
  compressor refused those job sizes; the CPU path took over) and one `N batches in a row failed on the
  SPEs, the game skins for N ms` (10-18 05:13); none since.

## Games that failed to start (smoke runs awaiting a re-run)

- Hedgewars (`20261019T095215Z`): `hwdrive: the engine never connected`, `Error opening file 1`, rc 1.
- Wesnoth (`20261019T095158Z`): killed after 1.7 s (rc 143), no game.log: its own log redirect
  ([smoke-tested.md](smoke-tested.md)).
- Both are already in TESTS Next ("Smoke re-runs").
