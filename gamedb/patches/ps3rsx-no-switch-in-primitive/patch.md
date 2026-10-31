+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx 7bd5891"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["ps3rsx-shadow"]
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
- **Minimal repro:** `validate-test.c`, cases 7 to 9 of `recover_cases()` (the per-submit model of
  `submit.c` with two channels, the state restore and `prim_owner`): channel 0's draw split across two
  submits, a submit of channel 1 queued in between waits, and nothing of it reaches the "GPU" before channel
  0's END; after a second the kernel ends a stale primitive (the channel refused unless its client
  recovers), and a freed channel's primitive is ended; inside a primitive the "GPU" gets only vertex data
  and the END (`tools/run-tests.sh --only ps3rsx-shadow`). The decision itself is inline in
  `ps3rsx_ioctl_gem_pushbuf()`, so the model, not `submit.c`, is what runs; with the rule off (`fix-off.sh`,
  variant `no-switch-in-primitive`, `make -C packages/walnut/drivers/ps3rsx/test fix-off`) case 7 fails.

## Validation

Not yet validated on the PS3.
