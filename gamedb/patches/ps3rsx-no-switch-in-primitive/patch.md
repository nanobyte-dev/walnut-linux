+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx 7bd5891"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# No channel switch while a primitive is open

Many Mesa contexts share the one hardware context, and ps3rsx restores a channel's state when the GPU moves
from one channel to another. Mesa splits long inline draws across pushes, so a primitive can still be open
at the end of a submit. Commit 7bd5891 (2026-10-14) makes the driver wait for the open primitive to close
before it switches channels and restores state; ps3fb's console handover waits the same way.

Later work leans on the same rule: since version 0.1137 the validator also checks the primitive order
within a channel, and a cancelled submit's open primitive is closed by ps3rsx itself
([ps3rsx-rejected-submit-cancel](../ps3rsx-rejected-submit-cancel/patch.md)). A channel whose open
primitive ps3rsx had to close after a second of waiting is refused unless its client recovers.

## Investigation

- **What crashed:** a graph exception when two GL processes rendered at once, the state restore landing
  between BEGIN and END of the other channel's draw.
- **Why:** the restore writes state methods, and the RSX doesn't take state changes inside a primitive.
- **How it was found:** the software context switch's bring-up (Phase 4 on 2026-10-14: interleaved GL
  processes, `rsx-suite.sh multi`), [DRIVER.md](../../../docs/DRIVER.md#the-software-context-switch-the-state-shadow).
- **Minimal repro:** None yet: proposed a host case in `validate-test.c` or `shadow-equiv` with two
  channels, one draw split across two submits of the first, a submit of the second queued in between, and
  the check that nothing of the second reaches the stream before the first one's END.

## Validation

Not yet validated on the PS3.
