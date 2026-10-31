+++
component = "mesa"
patches = ["patches/0029-nv30-assume-nothing-of-the-GPU-state-once-the-contex.patch"]
aliases = ["Mesa 0029"]
category = "stability"
switch = "NV30_CTX_STATE_KEEP"
default = "off (1 restores the old behaviour)"
gating = "none: every nv30 (stock nv30 keeps a stale view of the GPU state)"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-299"]
+++

# nv30: assume nothing of the GPU state once the context that drew last is gone

A context that draws after another one copies that one's view of the GPU state. When the context that drew
last was destroyed, the next one to draw kept its own view from whenever it last drew (or a new context's
zeroes), though the GPU still held the destroyed context's state. Now the whole cached state is reset: the
fragment program is set again, all 16 vertex formats are written, and the base index and restart state go
out with the next indexed draw. The command stream changes only on that first draw, with methods Mesa
already sends. [mesa-vtxfmt-cache](../mesa-vtxfmt-cache/patch.md) (0022) had already forgotten the vertex
formats in that case.

[mesa-cancel-recover](../mesa-cancel-recover/patch.md) (0053) reuses this path: after ps3rsx cancels a
submission, the next context to draw writes all of its state, also with `NV30_CTX_STATE_KEEP=1`.

## Investigation

- **What crashed:** nothing traced on the PS3. The failure the old code allows: the GPU runs a freed
  fragment program's code, and fetches attributes from freed vertex buffers.
- **Why:** if the destroyed context's fragment program was freed and a new one of ours was created at the
  same address and already translated, `FP_ACTIVE_PROGRAM` was skipped and the GPU kept running the old
  code from freed memory. Attributes the destroyed context had enabled beyond ours stayed enabled, and its
  base index and primitive restart setting stayed. What the RSX does with a garbage fragment program is
  unknown; a graph exception would wedge it.
- **How it was found:** reading nv30's state caching during the per-draw work of Mesa ps3.299 (commit
  a9dc99a, 2026-10-17).
- **Minimal repro:** None yet: proposed a mesa-shim run of a program that draws in context A, destroys A,
  creates a fragment program in context B (`--gpu` dump), and checks with `validate-capture --state` that
  B's first draw rewrites `FP_ACTIVE_PROGRAM` and all 16 VTXFMT words.

## Validation

Not yet validated on the PS3.
