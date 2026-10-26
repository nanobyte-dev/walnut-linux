# What the game logs say

`bench/logscan.py` reads the logs of every run in `build/bench/` (602 runs of 35 games when this
was written, 16 October to 19 October 10:41 UTC) and groups the errors and warnings by game and message.
This page is the triage of what it found, worst first. Each finding names the games, one run that shows
it, what causes it, whether the player would notice, and the fix. The fixes worth doing are in
[backlog/games.md](../backlog/games.md) and [backlog/graphics.md](../backlog/graphics.md); the PS3 tests
they need are in [TESTS.md](../TESTS.md).

Run directories are `build/bench/ps3-bench-<stamp>-<game>-<variant>/`; below only the stamp and game are
given. "Since ps3.544" means runs from 2026-10-19 09:47 UTC on (Mesa 26.2.4-1+ps3.544 installed).

## The tool

    bench/logscan.py                      every run, per game
    bench/logscan.py --new                only messages that appear after --since and not before
    bench/logscan.py -g sauerbraten -c audio,rsx -v
    bench/logscan.py --by-message         one block per message, across games
    bench/logscan.py --min-class gl       only gl and worse
    bench/logscan.py --show-ignored       what the ignore list swallowed
    bench/logscan.py RUNDIR...            some runs (or a directory of runs)

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
`MESA_DEBUG`, which no recipe sets, so "no such lines" means "not looked for". Item 6 proposes one run
per game with the debug output on.

## Catching failures

State (2026-10-20): ran on the PS3 on 2026-10-19 (the OpenAL captures of item 1); the capture's sound-card
files (the second ALSA read, interrupts.txt, snd_ps3.txt) are newer and have not run there yet. `DUMPON=1` (a run.sh variable or
ENVS token; off by default, never in measured runs) makes client.sh catch the first failure line:
`Wait timeout`, `mmap commit error`, `out of GART`, `out of VRAM` or `no memory to back the GART`
(`DUMPON_RE` replaces the list; as an ENVS token it can hold no space or `+`; `DUMPON_MAX`, default 1,
captures per run). The game is stopped (SIGSTOP) for the capture and let go after it. Into the run's `dump/`:
`trigger.txt` (the line and its second in the run), `proc.{status,maps,smaps_rollup,limits,nfd}`,
`alsa.txt` (read first: /proc/uptime, cards and every playback substream's status, hw_params,
sw_params, then the status again with the uptime 1 s later; the status tstamp is the last time the hw
pointer moved, on the uptime's clock), `interrupts.txt` (snd_ps3's line of /proc/interrupts, then 1 s
later with the whole file), `snd_ps3.txt` (its module parameters: `dma_stats` on a kernel with
local/0047), `threads.txt` (every thread's stat, wchan, syscall and kernel stack), `gpu.txt` (ps3rsx's debugfs `ps3rsx_mm` and
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

Analysis on the PC: `bench/dumpana.sh <run dir copied from the PS3>` builds a sysroot in
`build/tmp/dumpana/` (base libraries from `build/chroot/sid-ppc64.tar`, then the packages and `-dbgsym` debs
at the versions in the run's `versions` file, from build/debs, build/aptly/pool, build/apt-cache; libc6
symbols come from debuginfod.debian.net; the game's executable and any other mapped file the sysroot
lacks, libasound for one, from the PS3 image `build/rootfs/debian-ppc64.tar`), runs gdb on the core with
the executable (without it gdb loads no shared libraries) and writes `dump/analysis.txt`: the sound
card at the trigger (when its hw pointer last moved against the trigger and the read: "the DMA
stopped" or "was moving"), snd_ps3's interrupts and parameters, `info sharedlibrary`, `thread apply
all bt full`, and for runs with `oal_wedge>0` the threads sitting in ALSA/OpenAL. Packages it could not
find are listed in the file's header.

The GART crash (item 2): `ps3rsx_gart` and `ps3rsx_cma` are kernel command-line parameters, not runtime
ones, so a reduced GART needs a reboot into Petitboot by the user; the repro batch runs without it first.

## Findings, worst first

### 1. The sound card's DMA stops, and OpenAL's mixer waits for good

State (2026-10-20): cause found from two DUMPON captures; the fix is kernel patch local/0047 (a DMA
watchdog in snd_ps3), built as a test module for the booted kernel, not on the PS3 yet. The A/B is in
[TESTS.md](../TESTS.md) "Next".

Games: SuperTuxKart (6 of 21 runs from 10-18 to 10-19 morning), OpenArena (6 of 75), ClassiCube (3 of
31), Luanti (2 of 38): about 10% of the runs of the games that play through OpenAL Soft. 42 runs in
build/bench have more than 5 `Wait timeout` lines; in 14 of them the first OpenAL error is the timeout
itself, with no xrun before it.

Counts from the repro batches, stock snd_ps3 (`DUMPON=1`; "wedge" = `Wait timeout` lines, "xrun" = only
`mmap commit error` lines, the card still moving). Batch ssdroot7 (2026-10-19 20:53-21:40Z): SuperTuxKart
3 of 4, OpenArena 1 of 3, ClassiCube 1 of 3, BananaBread 0 of 3. Batch ssdroot8 (2026-10-20 00:39-01:25Z,
10 runs): SuperTuxKart 2 of 4 (291 lines with the card's pointer 1.0 s stopped before the first
timeout; 29 lines after a first xrun; one run xrun only, one clean), OpenArena 0 of 3 (an xrun dump
each, at 21, 20 and 33 s), ClassiCube 1 of 3 (12 lines after an xrun at 2.9 s; one xrun only, one
clean): 3 of 10, and 5 more dumps were recovered xruns. The same batch's 24 force-DXT runs (no
DUMPON) wedged in 9: Luanti 2 of 6, ClassiCube 2 of 6, OpenArena 3 of 8, SuperTuxKart 2 of 4, off
arms included. Over both batches' 34 runs of those games that is 12 wedges, about 35%, against the 10%
of the earlier runs: the timeouts come as early as 3 s into a run. A DUMPON run on SuperTuxKart can lose
its bench.txt and the tail of game.log (two of four in ssdroot8: a 164 MB core in /dev/shm on a 256 MB
console, not checked); report.py now skips the cut lines of such a run's `samples`.

What it looks like: game.log gets `[ALSOFT] (EE) Wait timeout... buffer size too low?` once a second
until the game exits, and the sound is gone. Picture and fps are fine; the mixer thread sleeps, so the
run even looks cheaper. bench.txt said `sound=RUNNING` and `underruns=0`. Often there are one or a few
`mmap commit error: Broken pipe` lines earlier: those are a different, harmless thing (below).

Cause: snd_ps3's DMA stops while the card stays RUNNING. snd_ps3 programs and kicks each 256-frame
block from the buffer-empty interrupt of the block before it, and moves its hw pointer only there; when
one of those interrupts does not come, or comes in a state its handler does not recover from, nothing
starts the chain again. ALSA never sees an xrun, because dmix sets the card's `stop_threshold` to the
boundary (alsa.txt's sw_params). dmix's clients wait on the card's period timer, which ticks from the
same missing interrupt. Evidence, `20261019T213651Z-classicube` (DUMPON, the trigger was the first
`Wait timeout` at 103.56 s, about 7264.6 s on the PS3's clock, the game having started at 7161.03):

- the card's status: `state: RUNNING`, `hw_ptr 4784128`, `tstamp 7263.60`. The kernel moves tstamp
  only when the hw pointer moves, so the pointer last moved 0.99 s before OpenAL's first timeout (its
  wait is 1 s) and at least 4 s before the capture read it;
- no xrun before it in that run;
- the core (dumpana.sh, with alsa-lib's structures from its source): OpenAL's mixer in `snd_pcm_wait`
  (alsa.cpp:505) on `default` = plug, softvol, `dmix:CARD=0,FORMAT=S16`. The dmix client is RUNNING,
  never recovered (`recoveries 0`), its buffer full (appl_ptr - hw_ptr = 1536 = buffer_size, avail 0),
  and its copy of the card's pointer equals the kernel's. Everything above the card is consistent and
  waiting for the card.

Why no recovery in user space helps: OpenAL's `snd_pcm_recover`/`prepare`/`start` act on the dmix
client, and dmix prepares and starts the card only when the card itself is in SETUP or XRUN, which
never happens here. `mmap=false` (`mixerNoMMapProc`) waits in the same `snd_pcm_wait`. Periods,
`slowptr` and `avail_min` change nothing while the pointer does not move. Closing and reopening the
device would restart the card only if no other client holds it. So the fix belongs in snd_ps3. Why the
interrupt goes missing is not known: the stalls come in heavy moments (loads, swapping), and the
handler's underflow-only branch reprograms the DMA without forcing channels that are stuck (it waits
7 ms per channel, gives up and overwrites). The watchdog records the registers when it fires.

The `mmap commit error: Broken pipe` (and `available update failed: Broken pipe`) lines are client
xruns: OpenAL's mixer thread starved past its 32 ms buffer (6 periods of 256 frames under dmix; no RT
priority, RTKit is absent). `20261019T212529Z-openarena` was dumped at one: the dmix client's
`avail_max` was 5120 frames (107 ms without a write), OpenAL recovered it at once (RUNNING again, buffer
refilled, its trigger time the trigger's second), and the card was still moving 4.5 s later. That run's
wedge came later and on its own. A larger buffer (`OAL=p1024`: 1024-frame periods, 64 ms) would make
these rarer, at the cost of latency.

Fix: kernel patch `packages/patched/kernel/linux/patches/local/0047-snd-ps3-dma-watchdog.patch`. While playing, a timer checks
every `snd_ps3.dma_watchdog_ms` (default 200, 0 off, read at stream start;
`/sys/module/snd_ps3/parameters/dma_watchdog_ms`) that buffer-empty interrupts still arrive. When none
came, a work item takes the interrupt and the stream lock, stops the DMA (forced, as trigger stop does)
and starts it again as trigger start does; the hw pointer goes on from where it stopped, so OpenAL only
sees a gap and never times out (200-400 ms against its 1 s wait). Nothing changes while the interrupts
come. While restarts do not bring them back, the checks slow down to 16 times the interval.
`/sys/module/snd_ps3/parameters/dma_stats` counts interrupts by kind (buffer empty, empty with
underflow, underflow alone, neither) and restarts, with the kick, interrupt and serial-out registers at
the last restart (also in a ratelimited dmesg warning). `snd_ps3.dma_test_stall=1`, written while
playing, makes the handler only acknowledge interrupts until the watchdog's restart or the stream's
end: the same stall on demand, so the watchdog can be tested in seconds instead of waiting for the 10%.

The patch is in the kernel series for the next kernel build. For a test without a new kernel and a
reboot, `make snd-ps3-ko REL=6.12.111+ps3.472` builds snd_ps3.ko with it for the booted kernel
(`build/debs/snd-ps3/6.12.111+ps3.472/`, with the kernel's own cross compiler and Module.symvers;
`tools/snd-ps3-ko.sh` has the install and revert steps). On the host it only compiles (clean with W=1):
nothing emulates the PS3's audio block, so the proof is the PS3 test.

The harness records it now (client.sh, every run): `snd_stall=` (seconds the card's hw pointer stood
still while RUNNING, sampled once a second) and `snd_stall_at=`, `oal_wait=` (Wait timeout lines) and
`oal_xrun=` (commit and avail errors) beside `oal_wedge=` (both), and on a kernel with the patch
`snd_restarts=`, `snd_wd=`, `snd_stats=`. report.py's `sstall` column shows it, logscan.py counts it.

### 2. A failed buffer allocation ends in a segfault

Games: BananaBread (5 runs on 10-18, 07:29 to 11:42 UTC), SuperTuxKart (1 run, 10-17 12:53), real Sauerbraten
(`20261025T013202Z-session-sauerbraten-edition`, the one run with the call site resolved). Fixed in Mesa 0047 and
0048, not yet confirmed on the PS3.

Evidence: `20261018T072934Z-sauerbraten-base` dmesg.log lines 1-11: 193 suppressed plus ten `ps3rsx: out of
GART for a 4096 KiB BO`, then `cube2_client[408321]: segfault (11) at 0 nip 3fff93a4e704 ... in
libc.so.6[ce704...]`, bench.txt `rc=139`. `20261017T125328Z-supertuxkart-lean`: 270 `out of VRAM for a 1024
KiB BO`, game.log `Irrlicht: FBO has one or several incomplete image attachments`, `GL1RenderTarget:
Cannot render to texture`, then `segfault ... in supertuxkart`, `rc=139`.

Cause: the kernel refuses the allocation (GART or VRAM full), libdrm returns an error, and the code
above it uses a NULL result: a write of 0 bytes at address 0 inside libc's memcpy/memset, called from a
driver library (the link register is in the game's mapped libraries, not libc). The next 4 MiB buffers
BananaBread (Debian's cube2) asks for are its vertex and texture streams. All five BananaBread crashes are in one four-hour stretch; none of the 40 BananaBread runs after it
crashed or logged a GART failure (what changed in between is not recorded in the logs).

Effect: the game dies mid-run. A player who fills GART or VRAM hits this, so it is a correctness bug even
though the benchmark no longer triggers it.

Found (2026-10-25, real Sauerbraten): the NULL write is the memcpy in Mesa's `st_setup_current()`
(st_atom_array.cpp:343), which copies the current vertex attributes into a `u_upload_alloc()` result it never
checks; the uploader's next 1 MiB buffer needs a 4 MiB nouveau_mm GART slab, hence "out of GART for a 4096 KiB BO"
just before. Mesa 0047 stops the write (and fixes `u_upload_alloc()`'s leak of the previous buffer at each failure),
0048 makes nouveau fail its other allocation paths cleanly and fall back from a slab to a buffer of the request's size
([DRIVER.md](../DRIVER.md#the-mesa-side), gamedb:sauerbraten/online-crashes). BananaBread's
crashes had the same signature (SuperTuxKart's segfault was in its own code after VRAM failures); their call sites
were not resolved.

### 3. Audio underruns in every SDL and direct-ALSA game

Games and counts per 2-minute window, runs since 10-19: dsda-doom 2793, OpenTTD 0-4020 (mean 3600 with
music on, 13 runs since ps3.544), DDNet 1765, X-Moto 200-218, BananaBread 60-181 (14 runs, mean 137),
Freeciv 87, OpenTyrian 71, Warmux 26, ScummVM 20, LinCity-NG 16, UQM 10, Flare 5. Earlier days: Neverball
780 over 13 runs, Armagetron 1223 over 29, Chocolate Doom 2701, Crispy Doom 1016, Red Eclipse 1280.

Evidence: `ALSA lib pcm.c:8787:(snd_pcm_recover) [error.pcm] underrun occurred` in game.log, repeated;
bench.txt `underruns=N`.

Cause: the SDL/ALSA callback thread misses its deadline because the PPE is busy or stalled; known and
measured for dsda-doom and OpenTTD ([SPU-AUDIO.md](../SPU-AUDIO.md), gamedb:dsda-doom/music-on-the-spes,
gamedb:openttd/savegame-blitters). BananaBread's 60-181 a run is a small number for a game the microphone found silent; the log has no sound error for it since 10-17 (the
nine early runs that said `failed to load sample` were missing data files, fixed).

Effect: audible chopping, strongest in dsda-doom and OpenTTD music. Nothing new since ps3.544.

Fix: already on the list ([backlog spu.md](../backlog/spu.md), games.md rank 32, memory.md "Sound
straight to ALSA"). The one thing logscan adds is a way to compare: `logscan.py -c audio --by-message`.

### 4. Texture and buffer pressure shows as "out of VRAM" lines

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

### 5. Input lag messages from libinput, and a few harness things

- `(EE) event1 - Logitech USB Receiver Mouse: client bug: event processing lagging behind by Nms, your
  system is too slow` and `scheduled expiry is in the past`: 12 runs from 10-17 to 10-18 (BananaBread,
  OpenArena, Luanti, ClassiCube). Xorg's input thread starved during stalls; the mouse pointer would hang.
  Not seen on 10-19. Nothing to fix beyond the stalls themselves.
- 16 runs (all with the `perfload` harness, 10-18 17:42 to 10-19 01:56) print `client.sh: line N:
  /dev/shm/ps3-bench-<stamp>-<game>/.pid: No such file or directory` 4 to 9 times. The runs completed
  and were not interfered, but the load profile may be missing its pid. Backlog "Gaps in the measurement
  tools" (S).
- ioquake3's `DOUBLE SIGNAL FAULT` at the harness's SIGTERM, after the window (OpenArena, 5 runs): harmless
  to numbers ([the engine's notes](../../gamedb/engines/ioquake3/technical.md#log-noise)).
- Kernel `os_area_queue_work_handler: Could not update FLASH ROM`: 2 runs (Extreme Tux Racer 10-18
  00:54, OpenArena 10-18 07:50). The kernel tried to write the PS3's OS area and failed. Probably the
  clock sync writing the RTC offset. Nothing was written, which is the safe outcome, but find out what
  triggers the write (S, reading code only), since the project rule is no flash writes beyond the
  install.
- `bench.txt: run ended=killed` (Quakespasm, dsda-doom, Armagetron, 10-16 and 10-17): the harness had to kill the X server (`xinit: unexpected signal`); not seen since.

### 6. Not in the logs, worth adding

Mesa nv30 shader and fallback messages (see "The tool"): run each light game once with `NV30_DEBUG` for
compile failures and the draw-module fallbacks, the log into game.log, then `logscan.py -c nouveau,gl`
(backlog "Mesa nv30 messages in the logs", S, PS3).

### 7. Old, not seen since (kept so a return is recognised)

| What | Game, run | Status |
|---|---|---|
| GPU wedge: `nouveau: kernel rejected pushbuf`, 89 lines and 35 000 dump lines, `ps3rsx: graph exception`, `ps3fb_vsync_interrupt: graphics exception` | Neverball smoke, 10-16 21:54 | no recurrence in the 12 Neverball runs since |
| validator `rejected push: swizzled render target 1 pixel wide` | gl-genmip probe, 10-16 | one probe run; not repeated |
| oom-killer | BananaBread 10-17 08:17, Red Eclipse 10-18 06:00 | not repeated in the BananaBread runs after it; Red Eclipse still needs its patches (gamedb:redeclipse/oom-the-memory-is-the-sound) |
| `Sys_LoadLibrary failed: libcurl.so.4` | Yamagi Quake II, 6 runs | optional download library, harmless |
| `QUAKE ERROR: You must have the registered version to use modified games` | Quakespasm, 10-16 (a launch-option error, one run) | one run; not repeated |
| `Unable to load driver 'sdl-opengl': Single element texture formats not supported` | OpenTTD 10-16 | worked around (openttd 0001: gamedb:openttd/gl-driver-on-gl21) |

### 8. Noise that is harmless

- `GL_ARB_framebuffer_object / texture_float / seamless_cube_map / texture_compression_bptc not found`
  and `SDL_GL_CreateContext() ... Core context failed` (OpenArena, 57 runs): ioquake3 probes for
  extensions nv30 does not have and carries on.
- No IPv6 in the kernel: OpenArena, DDNet, Teeworlds, SuperTuxKart print socket errors for it.
- First-run files: tyrian.cfg/.sav (OpenTyrian), widelands config, DDNet storage.cfg, Teeworlds settings,
  hedgewars "Settings file not found", UQM's voice add-on, `ALSA lib seq_hw.c ... /dev/snd/seq` (no
  sequencer device; fluidsynth games), `Could not query RTKit`, `Failed to create PipeWire event context`.
- speq (libspeq) statistics lines print "errors" in their text; only non-zero counts are kept by the tool.
- Each game's own noise (OpenTTD's luminance fallback, Luanti's lag lines, SuperTuxKart's fixed pipeline and
  config lines, BananaBread's and OpenArena's missing content, libspeq refusals in BananaBread) is in its
  technical notes (`gamedb/games/<id>/technical.md`, "Known problems").
