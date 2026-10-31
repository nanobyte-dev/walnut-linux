+++
component = "mesa"
patches = ["patches/0053-nouveau-nv30-recover-from-a-submission-ps3rsx-cancel.patch"]
aliases = ["Mesa 0053"]
category = "stability"
switch = "NV30_PS3_RECOVER"
default = "on"
gating = "ps3rsx-only"
forwarded = "not-needed (ps3rsx only)"
tests = []
batches = []
+++

# Recover from a submission ps3rsx cancelled

ps3rsx validates every push because one bad command wedges the RSX until the console reboots. Since
ps3rsx 0.1036 a rejected push refused its channel for good, because Mesa sends state once and counts
on it. ps3rsx 0.1137 offers a recovery instead: a client that asks for it (private GETPARAM 0x1001,
`PS3_RECOVER`, before its channel exists) gets `-ECANCELED` for a submission with a rejected push, none
of which reached the GPU, and the channel takes further submissions. This patch is that client.

The winsys counts the cancellations. A push buffer built on state that never reached the GPU drops its
queued words at its next kick and submits only its fence, so fences keep landing. Before its next
validation, transfer or buffer copy, a context throws its view of the GPU away: vertex programs and
constants are uploaded again where used, fragment programs and the blit's program too, and the next
context to draw writes all of its state (the path of [mesa-ctx-state-reset](../mesa-ctx-state-reset/patch.md),
0029). A wait on a fence of the cancelled submission waits on a newer one, and a query whose report
won't come is taken as lost (occlusion "passed", times 0). What the cancelled submission uploaded or
rendered is lost, as after a GPU reset. `NV30_PS3_RECOVER=0` doesn't ask, and an older ps3rsx doesn't
know the getparam, so nothing changes there. The driver side is
[ps3rsx-rejected-submit-cancel](../ps3rsx-rejected-submit-cancel/patch.md);
[DRIVER.md](../../../docs/DRIVER.md#a-rejected-push-the-submit-is-cancelled-the-client-recovers) explains
why accepting later pushes is safe.

## Investigation

- **What crashed:** nothing crashed; the program died as a GL client. With the refuse rule, the first
  rejected push left the channel with -EIO, and the program drew nothing for the rest of its life. Before
  that rule, a rejection let Mesa's next draws run on whatever state the GPU held: mpv's second frame
  carries only 36 words (scissor, vertex buffer offsets, the draw) and would have drawn with another
  program's texture units and programs.
- **Why:** Mesa sends state once and only re-sends what changed, so any submission that doesn't reach
  the GPU makes Mesa's view of the GPU wrong.
- **How it was found:** the mpv 0x105 analysis of 2026-10-29
  ([exception-0x105-mpv-vo-gpu.md](../../../docs/dumps/exception-0x105-mpv-vo-gpu.md)), which looked at
  what a plain rejection would leave behind.
- **Minimal repro:** None yet in `tools/run-tests.sh`. The checks so far are manual runs of ps3rsx's
  validating mesa-shim on the PC (`packages/walnut/drivers/ps3rsx/test/mesa-shim/`, `PS3SHIM_REJECT=N`):
  mpv without 0052 keeps playing with every frame cancelled; with one submit rejected by hand the next
  frame re-sends the whole state (346 words against 36) and the final GPU state equals the run without
  the rejection; egl-gears, gl-mipread, gl-genmip and gl-texsub finish with 2-3 rejected submits each.
  Proposed: one of those shim runs as a host suite.

## Validation

Not yet validated on the PS3.
