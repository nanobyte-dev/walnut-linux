+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx 0.1137", "ps3rsx 1aeaf88", "PS3_RECOVER"]
category = "stability"
switch = "refuse_rejected (ps3rsx module parameter), PS3_RECOVER (asked by the client)"
default = "off (clients that ask recover)"
forwarded = "no"
tests = ["ps3rsx-shadow"]
batches = []
+++
# A rejected push cancels its submit, and the client recovers

When the validator rejects a push, none of the submit reaches the GPU. What happens to the channel next
went through two versions:

- ps3rsx 0.1036 (commit 0a43c01, 2026-10-29): the channel is refused from then on (-EINVAL, then -EIO).
  Mesa sends state once and counts on it, so its next draws would have run on the GPU's older state
  (another program's vertex formats and textures with this one's buffers), which can fault as well. The
  program then drew nothing for the rest of its life.
- ps3rsx 0.1137 (commit 1aeaf88, 2026-10-29): a client that asks for it before its channel submits
  (private GETPARAM 0x1001, `PS3_RECOVER`; Mesa asks since [mesa-cancel-recover](../mesa-cancel-recover/patch.md),
  0053) gets -ECANCELED for that submit, and the channel takes further submits, each validated as always.
  If the channel's last accepted submit left a primitive open, ps3rsx ends it first. A client that didn't
  ask, or every client with `refuse_rejected=1`, keeps the old rule.

The key design point: every check reads only the push's own words and the channel's validator state
(which object each subchannel has bound, whether a primitive is open), and only accepted submits change
that state. So what reaches the GPU, the accepted pushes laid end to end with ps3rsx's own ENDs, passes
the validator as one stream. New order checks close the gap a cancel opens: BEGIN only with no primitive
open, END and vertex data only inside one. Mesa throws its view of the GPU away after a cancel instead of
drawing on it ([DRIVER.md](../../../docs/DRIVER.md#a-rejected-push-the-submit-is-cancelled-the-client-recovers)).

## Investigation

- **What crashed:** after the X16 refusal of [ps3rsx-validator-x16](../ps3rsx-validator-x16/patch.md),
  mpv `--vo=gpu` on a Mesa without 0052 lost every frame; with the refuse rule it drew nothing at all.
  Without either rule, a client drawing on after a dropped submit would run on state it never set.
- **Why:** a rejected submit takes state with it that the client believes the GPU holds.
- **How it was found:** the mpv wedge of 2026-10-28
  ([analysis](../../../docs/dumps/exception-0x105-mpv-vo-gpu.md)), then the mesa-shim on the PC, which runs
  ps3rsx's validator over every submit and answers like ps3rsx (`PS3SHIM_REJECT=N` rejects submits by hand).
- **Minimal repro:** `validate-test.c` with a model of `submit.c` per submit: a rejected submit then one
  that sends the state again, a rejected state push followed by a draw counting on it, a bind in a
  cancelled submit, a draw split across a rejected submit, 1000 submits with 900 rejected, a client that
  doesn't recover, mpv's X16 frame between its 0052 frames; each time the "GPU" stream must pass whole
  (`tools/run-tests.sh --only ps3rsx-shadow`; 13 of these fail against the old validator).

## Validation

Not yet validated on the PS3.
