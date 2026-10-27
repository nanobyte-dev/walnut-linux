# X-Moto: technical notes

X-Moto 0.6.3 runs at 1920x1080 with the right picture and sound. Our package batches the game's OpenGL
drawing (a frame went from 18,500 GL calls to 165), which took the benchmark replay from 44.5 to about 160
fps; its played sessions ran at the 60 fps vsync cap with clean sound ("flawless", the player). It is one
of the games where game-side batching paid most.

## Packages and patches

`xmoto`, `xmoto-data` (`0.6.3+repack-1+ps3.N`, [packages/xmoto](packages/xmoto/)), replacing Debian's.

| Patch | What | Switch |
|---|---|---|
| [0001](packages/xmoto/patches/0001-drawlib-GL-state-set-only-when-drawing-DrawLib-s-pri.patch) | DrawLib's GL state set only when drawing; its primitives batched per state run | `XMOTO_GL_BATCH` |
| [0002](packages/xmoto/patches/0002-renderer-the-level-s-blocks-and-edges-drawn-from-one.patch) | the level's blocks and edges drawn from one buffer | `XMOTO_GL_BLOCKS` |
| [0003](packages/xmoto/patches/0003-Benchmark-replay-XMOTO_BENCH_LOOPS-N-plays-the-repla.patch) | `XMOTO_BENCH_LOOPS=N` plays the benchmark replay N times | |
| [0004](packages/xmoto/patches/0004-drawlib-batches-written-straight-into-a-mapped-strea.patch) | batches written straight into a mapped 2 MiB GL_STREAM_DRAW buffer instead of client memory Mesa copies per draw | `XMOTO_GL_STREAM=1` (off by default); `XMOTO_GL_STATS` |
| [0005](packages/xmoto/patches/0005-Game-controllers-a-fixed-built-in-scheme-next-to-the.patch) | game controllers: a fixed scheme next to the keyboard | |

**Controllers (0005).** Controller N (the N-th SDL game controller) plays player N, no binding needed,
at the same time as the keys: right trigger or R1 accelerate, left trigger or L1 brake, d-pad or left
stick lean, Cross turns around, Triangle restarts the level, Square restarts from the checkpoint, Start
pauses. The table is `padBindings` in `Input.cpp`; Options > Controls > Controller lists it. Pads
plugged in later take the first free player; controller events are always on, whatever "Enable
joysticks" says. Menus take the d-pad, left stick, Cross and Circle.

`packages/xmoto/test/glbatch-test.sh` checks the GL layer on the PC's llvmpipe: 400 frames of a random
scene, the same pixels with and without the stream buffer, no GL errors (`make test ONLY=xmoto-glbatch`,
needs `make xmoto-work`).

## How it performs

- Bench (gamedb:batch/2026-10-18-batch16, kernel .433, x3): base 160.7 steady, 1% low 106.7, no gameplay
  stalls, longest 43 ms, 51 MB free, 37 MB of VRAM, 148 underruns; `stream` (0004) 162.1 (+1%), 106.3,
  0.4 s, 380 ms. In gamedb:batch/2026-10-18-mesa-416-hot-spots (one run each) base 160.4 against `stream`
  163.1, CPU 298 against 282 s. So the stream buffer gives about 1% and less CPU, and the hitches move
  from run to run: no default change yet ([the investigation](investigations/2026-10-18-client-array-copies.md)).
- On the SSD root with ps3rsx's `shadow_fast` (gamedb:batch/2026-10-19-ssdroot2-shadow-fast): 159.0 against
  158.4, no effect. The microphone hears the game's music and engine in all 8 clips, with 200-218 ALSA
  underruns a run.
- Memory: 49 MB Rss, nothing swapped, 0.3 MB free in its heap (nothing to cut).
- Played (2026-10-23 and 10-24, three sessions up to 26 minutes): 60 fps median, 1% low 34-44, 0.3-1.3 s of
  stutter a minute.

The benchmark ([recipe](benchmark/recipe.sh)) plays the "credits" replay, which ships inside `xmoto.bin`
where `--replay` doesn't read it, so `assets/xmoto-credits.rpl` is it extracted. `--timedemo --benchmark`
draws every frame, prints `Average framerate` and exits; it sets no swap interval itself, so
`vblank_mode=0`. The replay runs in real time (about 144 s), so two passes: the first is the warm-up.
Since the steady state, X-Moto's `fps` value is the game's average over the whole replay, not the HUD
median (`--hud xmoto` keeps the old meaning).

## Known problems

- The engine sound is quiet under the music, not missing.

## Investigations

- [2026-10-17 game-side batching](investigations/2026-10-17-game-side-batching.md): 44.5 to 166.5 fps (0001-0002).
- [2026-10-18 the batches copied at every draw](investigations/2026-10-18-client-array-copies.md) (0004).
