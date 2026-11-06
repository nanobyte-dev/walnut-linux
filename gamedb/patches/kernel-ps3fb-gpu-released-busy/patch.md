+++
component = "kernel"
patches = ["patches/local/0057-ps3fb-gpu-released-busy.patch"]
aliases = ["local/0057", "kernel 0057"]
category = "stability"
switch = "none"
default = "always on (ps3rsx sets the flag only when it keeps a busy or stalled GPU from ps3fb)"
forwarded = "no"
tests = ["ps3rsx-shadow"]
batches = []
+++

# ps3fb: remember that the RSX driver gave the GPU up not idle

ps3fb keeps the lv1 GPU context across loads of ps3rsx. When ps3rsx unloads with a job still running or
the GPU stalled, it keeps the GPU from ps3fb, and PGRAPH may run on. A module loaded afterwards would run
`FIFO_SETUP` on that context under the old job: untested hardware state (round 3 review, R4).

- `ps3fb_gpu_release_busy()`: ps3rsx calls it as it gives the GPU up not idle. Refused (`-EPERM`) unless someone holds the GPU. Sticky, only a reboot clears it, no lv1 call.
- `ps3fb_gpu_released_busy()`: a later ps3rsx asks it and refuses to load.
- `ps3fb_gpu_takeover()` answers `-EBUSY` from then on (true or false), so a driver that doesn't ask is stopped too, and ps3fb never restores its FIFO under the old job.
- ps3fb's shutdown leaves the contexts to lv1 when the flag is set, as after a recovery.

ps3rsx looks both calls up with `symbol_get()`, so a ps3rsx built for a kernel without them still loads
(it logs at every load that the check is absent). Both calls run at load and unload only, under `gpu_own_lock`.

## Investigation

- **What crashed:** nothing yet; the hazard was found in the round 3 review of ps3rsx's stall handling (R4):
  a reload after a busy unload ran `FIFO_SETUP` on the old context while PGRAPH might still run the old job.
- **Why it needs ps3fb:** ps3fb keeps the context across module loads, so the state has to live there.
- **Minimal repro:** unload ps3rsx while a long job runs (small `stall_hard_ms`), then load it again.
  `tools/rsx-ab`'s `rsx-arm.sh` refuses that reload from the log line; this is the driver-side refusal.

## Validation

Host test of ps3rsx's side (`busy-test`, with `fix-off` variants). Not yet run on the PS3.
