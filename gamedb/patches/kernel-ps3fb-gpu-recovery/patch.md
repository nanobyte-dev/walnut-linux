+++
component = "kernel"
patches = ["patches/local/0056-ps3fb-gpu-recovery.patch"]
aliases = ["local/0056", "kernel 0056"]
category = "stability"
switch = "none"
default = "always on (used only by ps3rsx's recovery, ps3rsx.recovery=0 turns that off)"
forwarded = "no"
tests = ["ps3:rsxdrm-smoke quirks rt12"]
batches = []
+++

# ps3fb: let the RSX driver replace its context after a graph exception

A graph exception wedges the RSX until a reboot, and the context ps3rsx works in keeps its broken
state. ps3rsx can restart the FIFO and the graph engine itself
([ps3rsx-gpu-recovery](../ps3rsx-gpu-recovery/patch.md)), but it needs a new driver context, and only
ps3fb can give it one: ps3fb allocated the context, records its IO maps and frees it at shutdown, and
lv1 hands out new addresses with every allocation. Three calls, for the driver that is hooked in only
(anyone else gets `-EBUSY`):

- `ps3fb_gpu_recover_begin()`: from now on ps3fb makes no lv1 context, memory or device teardown and
  never takes the GPU back (`ps3fb_gpu_takeover(false)` gets `-EBUSY`, its channel 0 is not run
  again), not even at shutdown; lv1 cleans up the partition at reboot. Once per boot.
- `ps3fb_gpu_ctx_leak()`: the faulted driver context is forgotten, not freed (its IO maps undone);
  the next `ps3fb_gpu_ctx_get()` allocates a new one.
- `ps3fb_gpu_ctx_realloc()`: frees and allocates again only the first new context (the one
  allocated after the leak, handed out exactly once, so it ran nothing but the recovery's own bind),
  once per boot, never the leaked context or ps3fb's own.

`ps3fb_gpu_recovered()` tells a reloaded ps3rsx that a recovery ran.

## Investigation

- **What crashed:** nothing in this patch's absence; without it a graph exception meant a reboot.
- **Why the context can't be freed or reused:** after a trap the faulted context's objects no longer
  bind, and freeing a context that had run graph work after a trap shut the console down (SYSCON
  fatal shutdown, 3 beeps, 2026-11-01). A context that ran only its first bind could be freed once,
  which is what the realloc allows. With leaked contexts, lv1's own teardown at shutdown was never
  needed: the console powered off and rebooted normally after every recovery.
- **How it was found:** the unwedge steps of 2026-11-01 ([RSX.md](../../../docs/RSX.md#lv1-gpu-calls-and-their-effects-on-a-wedged-rsx)).
- **Minimal repro:** `rsxdrm-smoke quirks rt12` with ps3rsx's `test_allow_rt_enable_12=1` (root
  only) raises graph exception 0x106; ps3rsx's recovery calls the three functions in order.

## Validation

2026-11-01, kernel 6.12.111+ps3.1220~dev with this patch's test version (sessions 1 to 4, see
[TESTS.md](../../../docs/TESTS.md)): every recovery called `recover_begin`, the leak and one realloc;
the console shut down and rebooted normally afterwards each time. The landed version is the same
logic without the experiment-only calls and their logging delays, on top of local/0055.
