+++
component = "kernel"
patches = ["patches/local/0047-snd-ps3-dma-watchdog.patch"]
aliases = ["local/0047", "kernel 0047"]
category = "stability"
switch = "snd_ps3.dma_watchdog_ms"
default = "200 (ms; 0 off)"
forwarded = "not yet (test on the PS3 first: docs/TESTS.md, the OpenAL wedge A/B)"
tests = ["ps3:speaker-test with snd_ps3.dma_test_stall"]
batches = ["2026-10-20-ssdroot12-dma-watchdog"]
+++

# snd_ps3: restart a DMA chain that stopped, and interrupt stats

While playing, a timer checks every `snd_ps3.dma_watchdog_ms` that buffer-empty interrupts still
arrive. When none came, a work item takes the interrupt and stream locks, stops the DMA (forced, as
trigger stop does) and starts it again as trigger start does. The hw pointer goes on from where it
stopped, so a client only sees a gap of 200 to 400 ms, well inside OpenAL's 1 s wait. While restarts
don't bring the interrupts back the checks slow down to 16 times the interval. `dma_stats` counts the
interrupts by kind and the restarts, with the DMA registers at the last restart, and
`snd_ps3.dma_test_stall=1` stops the chain on demand so the watchdog can be tested in seconds.

The design point is where the fix lives. Nothing in user space can restart the card: dmix prepares and
starts it only when it is in SETUP or XRUN, which never happens here.

## Investigation

- **What crashed:** the sound stopped for good in about 10% of the runs of games that play through
  OpenAL Soft (SuperTuxKart, OpenArena, ClassiCube, Luanti): `[ALSOFT] (EE) Wait timeout... buffer size
  too low?` once a second until the game exited, picture and fps fine.
- **Why:** snd_ps3 programs and kicks each block from the buffer-empty interrupt of the block before,
  and moves its hw pointer only there. When one interrupt doesn't come, or comes in a state the handler
  doesn't recover from, nothing restarts the chain; the PCM stays RUNNING and no xrun is raised, because
  dmix sets the card's stop_threshold to the boundary. Why the interrupt goes missing is not known: the
  stalls come in heavy moments (loads, swapping), and the handler's underflow-only branch reprograms the
  DMA without forcing stuck channels.
- **How it was found:** failure dumps (`DUMPON=1`) at the first `Wait timeout` in batches
  gamedb:batch/2026-10-19-ssdroot7-openal-wedge and gamedb:batch/2026-10-20-ssdroot8-openal-wedge: in
  ClassiCube's capture the card was RUNNING with its pointer last moved 0.99 s before OpenAL's first
  timeout, and everything above the card consistent and waiting for it.
  gamedb:batch/2026-10-20-ssdroot10-openal-wedge ruled out our openal-soft build
  ([LOG-FINDINGS.md item 1](../../../docs/games/LOG-FINDINGS.md#1-the-sound-cards-dma-stops-and-openals-mixer-waits-for-good)).
- **Minimal repro:** `snd_ps3.dma_test_stall=1` written while `speaker-test` plays, with
  `dma_watchdog_ms=0`: the hw pointer freezes, the wedge on demand. In
  gamedb:batch/2026-10-20-ssdroot12-dma-watchdog the watchdog restarted every forced stall within 200 ms
  (10 in a row, 11 restarts, no oops), and the game arms wedged 2 of 9 runs off against 0 of 9 on, with
  2 restarts caught and fps unchanged.

## Validation

Not yet validated on the PS3.
