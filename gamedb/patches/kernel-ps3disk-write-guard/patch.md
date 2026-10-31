+++
component = "kernel"
patches = ["patches/local/0045-ps3disk-write-guard.patch"]
aliases = ["local/0045", "kernel 0045"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["ps3disk-guard", "ssdimg", "ssdimg-vm"]
batches = []
+++

# ps3disk: a write guard for GameOS's disk regions

The OtherOS++ multi-region driver ([kernel-ps3-storage-regions](../kernel-ps3-storage-regions/patch.md),
ps3/0010) exposes every region of the internal disk read-write: `ps3da` is the whole disk (VFLASH, the
region table and every OS), `ps3db` is GameOS's dev_hdd0, `ps3dc` its dev_hdd1. Each region now gets a
policy from its index, fixed at build time: `ps3da` read-only (`set_disk_ro`, BLKROSET 0 refused, and
every write failed in `queue_rq` itself, since the block layer only warns); `ps3db` writable only inside
the sector ranges root registers once per boot in `/sys/block/ps3db/write_guard/` and seals (the extents
of the Linux image file that dm-linear maps); the others writable as before.

The check runs in `ps3disk_queue_rq()` under the driver's lock, before any data is gathered or sent to
lv1, and fails closed: anything that isn't a read or a data-less flush counts as a write, and a request
only partly inside a range is refused whole. There is deliberately no switch to turn it off. The range
logic lives in `ps3disk_guard.h` without kernel APIs, so the host test compiles the same code.
`ps3-imgmap-boot` and `ps3-imgmap` register and seal the ranges (commit 1abb2a1,
[GAMEOS-HDD.md](../../../docs/GAMEOS-HDD.md#the-kernel-guard)).

## Investigation

- **What crashed:** nothing yet. The risk is data loss: a mistyped `mkfs` or `dd` one letter away from
  a Linux device wipes GameOS or the firmware's VFLASH.
- **Why:** ps3/0010 makes the GameOS regions ordinary writable block devices, and the SSD root lives in
  an image file inside dev_hdd0, so Linux must write `ps3db`, only there.
- **How it was found:** the design of the SSD root inside GameOS's disk (docs/GAMEOS-HDD.md), 2026-10-19.
- **Minimal repro:** `packages/patched/kernel/linux/tests/ps3disk-guard.sh` (`tools/run-tests.sh --only
  ps3disk-guard`): the range parsing, seal, merge and policy decisions on the patch's own header; the
  `ssdimg` and `ssdimg-vm` suites run the image tools against a model and a QEMU guest of the guard.
  On the PS3, kernel .648 refused a write-back outside the sealed ranges with EIO and accepted one inside
  (2026-10-21, docs/TESTS.md).

## Validation

Not yet validated on the PS3.
