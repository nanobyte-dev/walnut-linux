+++
component = "kernel"
patches = ["patches/local/0009-ps3-idle-no-pause-with-irq-pending.patch"]
aliases = ["local/0009", "kernel 0009", "linux/local-0009"]
category = "stability"
switch = "ps3_idle_prep"
default = "on (ps3_idle_prep=0 on the command line, or 0 in /sys/module/kernel/parameters/ps3_idle_prep, brings back the old pause)"
forwarded = "not yet (test on the PS3 first)"
tests = ["ps3:ps3-waker -m probe"]
batches = ["2026-10-18-kernel-419-idle", "2026-10-18-batch16"]
+++

# powerpc/ps3: don't pause a thread with an interrupt already pending

`ps3_power_save()` called `lv1_pause(0)` soft-disabled, with MSR[EE] on. A decrementer that fired
between the idle loop's `local_irq_disable()` and the hcall was masked: the masked-interrupt handler sets
DEC to 0x7fffffff (27 s at the 79.8 MHz timebase) and records it for the replay, and the thread then
paused with nothing to wake it. The patch uses `prep_irq_for_idle()` as `pseries_lpar_idle()` and the
native Cell's `cbe_power_save()` do: hard-disable, return without pausing when something was masked,
otherwise mark the thread soft-enabled, then hard-enable and pause with MSR[EE] on.

The design point is what it does not do. Pausing with MSR[EE] off (powernv's
`prep_irq_for_idle_irqsoff()`) would close the last window too, but it relies on `lv1_pause()` ending on
a decrementer pending under EE=0 and on the hypervisor leaving that exception pending, and neither is
documented; if either fails every idle thread loses its tick. The first draft did that and was changed
before it was enabled (commit b303e08). With EE on, the race left costs at most one tick.
The switch came a day later (commit 5de5764) for same-boot A/B runs. An upstream draft is in
`docs/upstream/linux-ps3-idle.md`.

## Investigation

- **What crashed:** no crash: games froze for 0.3 to 4 s with the CPU idle. Any game waiting on a GPU
  fence from the stuck thread froze for as long, since ps3rsx's fence poll is an hrtimer on the waiter's
  CPU.
- **Why:** the masked decrementer left PPE thread 1 asleep until its next external interrupt. Thread 0
  gets the device interrupts and recovered within milliseconds; thread 1 gets only IPIs and could stay
  asleep for seconds ([STALLS.md](../../../docs/STALLS.md#why-frames-stalled-the-lost-decrementer)).
- **How it was found:** OpenArena's stalls with no cause in the game. The stall recorder removed them
  (it kept a thread busy): gameplay stall time 28.9 / 7.1 / 20.7 / 7.3 s with it off / on / off / on in
  one boot. A 10 ms cross-CPU kicker halved them (gamedb:batch/2026-10-18-lost-decrementer) and was
  the stopgap in game mode until this patch
  ([gamedb:openarena/reload-stalls](../../games/openarena/investigations/2026-10-17-reload-stalls.md)).
- **Minimal repro:** `ps3-waker -m probe -i 1.37 -t 120` on the PS3 with the switch at 0 and 1 in one
  boot (ps3-gpu-tools). On kernel .419 with the switch at 0, thread 1 woke up to 1103 ms late (11 wakeups
  of 100 ms or more); with the fix both threads' worst was 3.6 / 4.0 ms. In OpenArena, gameplay stalls
  fell from 9.0 and 7.9 s to 0.4 and 0.4 s (gamedb:batch/2026-10-18-batch16).

## Validation

Not yet validated on the PS3.
