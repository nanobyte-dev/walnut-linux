+++
component = "kernel"
patches = ["patches/local/0048-ps3disk-probe-unwind-and-remove-order.patch"]
aliases = ["local/0048", "kernel 0048"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no (the multi-region driver is the OtherOS++ patch set's, not upstream)"
tests = []
batches = []
+++

# ps3disk: the probe error unwind and the remove order of the multi-region driver

Two bugs of the OtherOS++ multi-region driver
([kernel-ps3-storage-regions](../kernel-ps3-storage-regions/patch.md), ps3/0010). The probe's error path
now counts the regions set up completely and unwinds from there; a failed region's disk gets only
`put_disk()` and its tag set is freed in place. Remove clears each `gendisk[]` pointer under the driver's
lock before `put_disk()`, and `ps3stor_teardown()` frees the interrupt before the DMA region, the reverse
of setup.

## Investigation

- **What crashed:** nothing seen; both are latent.
- **Why:** (1) the probe unwind looped `for (; devidx >= 0; devidx--)` with an unsigned index, always
  true, so a failure in the first regions wrapped it and walked past the end of `gendisk[]` and
  `tag_set[]`; a failed `device_add_disk()` also went to `del_gendisk()` on a disk never added. (2)
  remove freed the disks but left the pointers, and the interrupt handler walks that array to run the
  queues, so an interrupt during remove (the cache sync completes by interrupt) could use a freed
  gendisk.
- **How it was found:** code reading, in the report of the agent that wrote the write guard
  ([kernel-ps3disk-write-guard](../kernel-ps3disk-write-guard/patch.md)), 2026-10-18.
- **Minimal repro:** None yet: a probe failure can't be provoked cheaply on the PS3. Proposed: the
  unwind loop in a userspace model with injected failures, like `ps3disk-guard.sh` does for the guard's
  logic. Kernel .648 booted with it and its normal probe path was fine (2026-10-21).

## Validation

Not yet validated on the PS3.
