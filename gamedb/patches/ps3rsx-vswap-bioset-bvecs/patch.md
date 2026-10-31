+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx fd1b6e7", "ps3vswap"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# The VRAM swap's bio reserve with BIOSET_NEED_BVECS

A one-word fix to [ps3rsx-vswap-bio-reserve](../ps3rsx-vswap-bio-reserve/patch.md), the same day (commit
fd1b6e7, 2026-10-15): the device's own bio set is created with `BIOSET_NEED_BVECS`. Without it the set had
no bio_vec pool, so every allocation from it that needed one returned NULL.

## Investigation

- **What crashed:** a kernel oops in kswapd at the VRAM swap's first write to the backing disk.
- **Why:** the bio set was made without a bvec pool, so every allocation from it returned NULL.
- **How it was found:** the first boot with the new bio reserve; the oops came as soon as pages went to the
  disk.
- **Minimal repro:** None yet: the same PS3 memory-pressure test as
  [ps3rsx-vswap-bio-reserve](../ps3rsx-vswap-bio-reserve/patch.md) covers it (the first write to the
  disk tier is enough).

## Validation

Not yet validated on the PS3.
