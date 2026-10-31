+++
component = "kernel"
patches = ["patches/local/0051-ps3fb-exception-dump-around-get.patch"]
aliases = ["local/0051", "kernel 0051"]
category = "diagnostics"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++

# ps3fb: print the FIFO cache entries ending at GET in a graph exception dump

The graph exception dump printed entries 4i..4i+3 for i = 0, 4, ..., 28 of the 512-entry PFIFO cache,
whatever GET was, and its G and P markers compared GET and PUT with i, so they almost never matched. The
command that faulted sits just before or at GET and was usually not among the printed entries. Now the
dump prints the 32 entries ending at GET, wrapping at the end of the cache, each line starting with its
first entry's index, and marks GET (G), PUT (P) or both (B); the raw get and put words stay on their own
line. It is rate-limited to two dumps per 10 s and still only reads the driver info area and calls
printk from the interrupt handler.

Every dump in `docs/dumps/` so far has the old layout; reading both is described in
[RSX.md](../../../docs/RSX.md#errors-and-what-wedges-the-gpu). Added on 2026-10-29 (commit 0a96724, task
#29) after the mpv 0x105 analysis showed how much the old window hid. It is in kernel .1076, whose PS3
check is in [TESTS.md](../../../docs/TESTS.md) Next.
