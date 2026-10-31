+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx 0.249", "ps3rsx 9e2a3b9"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["ps3rsx-shadow"]
batches = []
+++
# A rejected push leaves no state behind

Version 0.249 (commit 9e2a3b9, 2026-10-17) made the validator and the state shadow cheaper (one table
lookup per word, the shadow written a burst at a time with dirty bitmaps), and fixed what a rejected push
left behind. A submit's pushes are now all checked before any of them counts: the validator state and the
shadow follow the pushes only once the submit went to the GPU. Before, a rejected push in a multi-push
submit kept the object binds of the pushes before it in the validator's state although they never ran,
so later methods were checked against the wrong object, and it cleared the channel's shadow, after which
the GPU's shadow claimed state the channel had changed.

This is also what makes the later recovery safe: only accepted submits change what the validator knows
([ps3rsx-rejected-submit-cancel](../ps3rsx-rejected-submit-cancel/patch.md)). A host harness compared the
fast path with the old checks over 120 million random words: same verdicts, messages and words.

## Investigation

- **What crashed:** nothing on the console; the bug was found while reworking the validator.
- **Why:** a method checked against a bind that never reached the GPU could pass the allowlist for the
  wrong class, the route to a 0x109 wedge (M2MF methods on the surface-2D object,
  [RSX.md](../../../docs/RSX.md#errors-and-what-wedges-the-gpu)).
- **How it was found:** code reading during the 0.249 rework, which the profiles of 2026-10-16 asked for
  (the validator and shadow were 5 to 16% of each game's CPU).
- **Minimal repro:** `validate-test.c` cancels a submit with a bind in it and checks that methods through
  that subchannel are refused afterwards (`tools/run-tests.sh --only ps3rsx-shadow`).

## Validation

Not yet validated on the PS3.
