+++
id = "sigterm-hang-and-openal-xruns"
games = ["openarena", "quake-3-arena"]
driver_patches = []
game_patches = ["ioquake3/0031"]
sessions = []
batches = []
+++

# SIGTERM hangs ioquake3; OpenAL's "mmap commit error" (2026-10-28)

Two problems seen in Quake III's first runs on the PS3 (2026-10-27): ioquake3 hung for good after a
SIGTERM, and OpenAL logged `[ALSOFT] (EE) mmap commit error: Broken pipe` during play. Tested on the
PS3 (kernel .472, Mesa ps3.924, openal-soft ps3.874) with OpenArena (opengl1 and opengl2) and Quake III
with its native game libraries, each started through `ps3-game` on the desktop's display with a map.

## SIGTERM: the shutdown ran inside the signal handler

**Symptom.** With ioquake3 ps3.931 a SIGTERM after the map had loaded hung the game in 4 of 6 tries
(OpenArena 2 of 3, Quake III 2 of 3); the log stopped at `RE_Shutdown( 1 )`, the last frame stayed on
the TV, and only SIGKILL ended it. A second SIGTERM did nothing.

**Cause.** gdb on a hung OpenArena (with our ioquake3-dbgsym):

```
#16 IN_DeactivateMouse ... sdl_input.c:403        <- XGrabPointer -> _XReply -> xcb_wait_for_reply64
#19 GLimp_Shutdown / #20 RE_Shutdown ... tr_init.c:1335
#24 CL_Shutdown / #25 Sys_SigHandler (signal=15) ... sys_main.c:768
#26 <signal handler called>
#32 xcb_wait_for_special_event                     <- Mesa's DRI3 loader, libxcb's lock held
#34 loader_dri3_swap_buffers_msc / #38 glXSwapBuffers / #44 GLimp_EndFrame / #53 Com_Frame
```

Upstream's `Sys_SigHandler` runs `CL_Shutdown` and `SV_Shutdown` inside the handler, on whatever the
signal interrupted. Here it interrupted the frame's buffer swap while libxcb held its connection lock,
and the shutdown's `XGrabPointer` waited for that same lock. glibc's `signal()` blocks the signal
inside its own handler, so another SIGTERM stays pending. The other threads (`speq-0..3`, libspeq's SPU
workers; `threaded-ml`, libpulse's mainloop thread; OpenAL's mixer in `snd_pcm_wait`; Mesa's
`ioquake:disk$0` shader cache queue) were idle in their own waits: bystanders, not part of the cycle.
The `DOUBLE SIGNAL FAULT: Received signal 11` after `Client Shutdown` in older harness logs is the same
shutdown crashing instead of hanging. A side effect of the SIGKILL: the PID file stays, and the next
start can ask (xmessage) whether to use "safe" video settings.

**Fix: ioquake3 0031, `sys_signalQuit 1`** (default; 0 is upstream's behaviour). SIGTERM, SIGINT,
SIGHUP and SIGQUIT only set a flag; the main loop sees it before its next frame and quits the way the
`quit` command does (`Com_Quit_f`'s shutdown with `Received signal N` as the reason, exit status 1 as
before, so the harness's log rule for `Client Shutdown (Received signal 15)` still matches and the PID
file is removed). A second quit signal exits at once with `_exit(1)`; `alarm(10)` ends the process with
SIGALRM if the main loop doesn't get there (a load that runs long, or a quit that hangs after all).
Crash signals keep upstream's handling.

**Verified** (ps3.959~dev, 2026-10-28 00:05-00:12): 10 of 10 SIGTERMs in play exited in 1.0-2.6 s
(OpenArena opengl1 4, Quake III 4, OpenArena opengl2 2), and 2 of 2 during OpenArena's map load (3 s and
8 s after the start) in 3.8 and 1.5 s; exit status 1 every time, no PID file left, game mode off a few
seconds after the exit. `tools/play-test.sh stop` keeps its SIGKILL fallback for other games.

## "mmap commit error: Broken pipe": real dropouts, from a starved mixer thread

**What the line is.** OpenAL Soft's ALSA mmap mixer (`alsa.cpp`, `mixerProc`) logs it when
`snd_pcm_mmap_commit` finds the stream already underrun: the game's dmix client ran dry because the
mixer thread didn't get to write in time. OpenAL recovers at once (the next loop's `snd_pcm_recover`),
so nothing stays broken, but the output did go silent for the gap. The device is `default` = plug,
softvol, dmix at 48 kHz, 256-frame periods, a 1536-frame buffer: 32 ms. Nothing of ioquake3's sound
setup is involved (`s_useOpenAL 1`, the default; our openal-soft patches touch the mixer's sample
conversion only).

**Measured** with ps3-xrunlog (the harness's dropout logger) and `ALSOFT_LOGLEVEL=3`, 150 s in a map
with 5 bots (`bot_minplayers 5`; Quake III q3dm7, OpenArena oa_dm4), nobody at the pad:

| Run | Mixer thread | Dropouts (audible) | Longest | commit errors |
|---|---|---|---|---|
| Quake III x2 | normal (`pthread_setschedparam failed`, no RTKit) | 1 (1), 2 (2) | 168, 141 ms | 1, 2 |
| OpenArena x2 | normal | 4 (1), 21 (17) | 103, 440 ms | 1, 20 |
| Quake III x3 | SCHED_RR (`LimitRTPRIO=10`) | 0, 0, 0 | - | 0, 0, 0 |
| OpenArena x1 | SCHED_RR | 3 (3) | 176 ms | 3 |

Every commit error was an audible dropout (80-440 ms, sound on both sides), so this is not log noise.
(A first Quake III run overlapped another agent's Quake test and is left out; it had 4 dropouts.) The
mixer runs at normal priority because the session has no RT limit (`ulimit -r` 0) and no RTKit, and on
the PPE's two hardware threads it competes with the game's main thread and Mesa. With a real-time
limit OpenAL puts it on SCHED_RR by itself, and Quake III's dropouts were gone in 3 of 3 runs;
OpenArena's heavier map still had 3, against 21 in the run after it, so priority is most of it, not all
(page faults and kernel work can't be outranked).

**Not changed here.** The lever is the session's RLIMIT_RTPRIO (a `LimitRTPRIO=` for the user manager,
or `/etc/security/limits.d`), which is system-wide: every OpenAL game would put its mixer on SCHED_RR,
and SDL's audio threads could ask for it too. That wants a decision and a suite measurement (the 5-10
game rule, played sessions judged by ear), so it is a proposal in [technical.md](../technical.md), not a
shipped change. OpenAL's 32 ms buffer is the other lever (`period_size`/`periods`); dmix fixes the
period and a starved thread misses 100-400 ms, more than a few extra periods would cover.
