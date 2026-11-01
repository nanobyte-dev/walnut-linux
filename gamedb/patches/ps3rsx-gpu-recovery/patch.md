+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx unwedge.c"]
category = "stability"
switch = "ps3rsx.recovery (module parameter, at load)"
default = "on"
forwarded = "no"
tests = ["ps3rsx-shadow", "gpu-recovered", "gpu-crash-restart", "ps3:rsxdrm-smoke quirks rt12"]
batches = []
+++

# ps3rsx recovers the RSX in place after a graph exception

A graph exception used to wedge the RSX until a reboot. ps3rsx now recovers from the first one in a
boot, in place: it finds the client whose commit trapped and refuses it, restarts the FIFO and the
graph engine with nouveau's documented registers on a new driver context from ps3fb
([kernel-ps3fb-gpu-recovery](../kernel-ps3fb-gpu-recovery/patch.md)), moves every client's fence and
query mapping to the new context, and tells the others once (`-ECANCELED`: Mesa sends its state
again). A second exception in the same boot, or a failed step, leaves display-only mode: KMS with
software rendering until reboot. Every exception gets a crash record and a uevent; ps3-system keeps
the report in `/var/log/walnut/gpu-crash/` and restarts the desktop in display-only mode, ps3-desktop
shows the user a dialog. The details are in [DRIVER.md](../../../docs/DRIVER.md#recovery-after-a-graph-exception-unwedgec)
and the recipe in [RSX.md](../../../docs/RSX.md#recovering-from-a-graph-exception).

## Investigation

- **What crashed:** a graph exception (cause 0x106 from RT_ENABLE 0x12 in the tests) stops the
  whole RSX. lv1 turns the FIFO's puller and pusher off and clears both interrupt enables; ps3fb's
  own channel stops too.
- **Why recovery works:** the registers lv1 leaves wrong are documented in nouveau; restoring them,
  dropping the stuck cache entry and running ps3fb's reset program on a fresh context restarts the
  GPU. lv1 handles the software-object methods that follow but leaves the puller off, so a watchdog
  does the second half of nouveau's cache-error path.
- **What the first in-driver tests found and fixed:** the last submitter is often not the trapping
  client (mpv was refused for rsxdrm-smoke's push): the blame now uses the reference register.
  Clients read their fences from the old context's reports page, never freed a buffer and ran the
  GART out 12 s after the recovery (then a client's out-of-GART path trapped again): the reports
  area is now seeded from the old one and every mapping of it faults in the new page. Refused
  clients kept allocating: they now get no new buffers or channels.
- **How it was found:** the unwedge steps of 2026-11-01 and four PS3 sessions of the in-driver
  version, each with a security review.
- **Minimal repro:** on the debug kernel (its module, `ps3rsx-modules-ps3-debug`, is the only one
  built with the switch), `echo 1 > /sys/module/ps3rsx/parameters/test_allow_rt_enable_12` (root),
  then `rsxdrm-smoke quirks rt12`: the validator lets RT_ENABLE 0x12 through and the GPU traps. The
  switch is cleared by every recovery.

## Validation

2026-11-01, session 4 (kernel 6.12.111+ps3.1220~dev20261101045143, the test build of this change,
Mesa ps3.1219), X, glxgears, mpv and Neverball running:

```
ps3rsx unwedge: the GPU trapped in commit 0x5e00984e (reference 0x5e00984d): a client's, channel 5, rsxdrm-smoke (pid 2288)
ps3rsx unwedge: reports area seeded from the old context (64 KiB): fence labels continue
ps3rsx unwedge: in-place recovery done: channel 2, context 0x55555557 (1 so far)
```

For 5 minutes after it glxgears held 59.9 fps, mpv played and Neverball drew, every client's handle
count stayed flat and the GART at 25 MiB, with no out-of-GART and no new exception; the dialog showed
about 20 s after the recovery. A second exception left display-only mode: the desktop restarted once
on `:0` without glamor (software GL), the failed dialog mapped 6 s after the new session, no polkit
error, the GART went back to 3 MiB. Session 3 had the same restart through the escape hatch.
The landed version drops the test build's logging delays (the recovery took 45 s with them), so
its timing is checked again on the PS3 before release.
