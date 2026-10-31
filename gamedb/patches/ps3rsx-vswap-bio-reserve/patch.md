+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx 7d1e118", "ps3vswap"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# The VRAM swap's own bio reserve, requests capped at 32 pages

The VRAM swap (`ps3vswap0`, [DRIVER.md](../../../docs/DRIVER.md#vram-swap-ps3vswap0)) keeps each swap page in
VRAM or at the same place on the backing disk partition, so writing to the disk is part of serving a
request. Commit 7d1e118 (2026-10-15) gave the device a bio reserve of its own and capped requests at 32
pages, so a request can always get the bios it needs to finish. The same commit made `ps3-vswap.service`
put the disk swap partition behind the VRAM swap when ps3rsx provides it.

## Investigation

- **What crashed:** the swap path deadlocked under memory pressure while swapping through `ps3vswap0`.
- **Why:** the requests drew on the shared 2-bio reserve and held their bios until they dropped their lock,
  so under memory pressure the reserve ran dry with every bio held by a request waiting for another one: a
  deadlock in the swap path.
- **How it was found:** the VRAM swap's bring-up on 2026-10-15, under memory pressure.
- **Minimal repro:** None yet: proposed a PS3 test that fills RAM (a memory hog with the desktop running)
  until the VRAM tier is full and pages go to the backing disk, for several minutes, watching for hung
  tasks in dmesg.

## Validation

Not yet validated on the PS3.
