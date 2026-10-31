+++
component = "kernel"
patches = ["patches/local/0046-ps3-htab-one-hcall-per-insert-and-stats.patch"]
aliases = ["local/0046", "kernel 0046"]
category = "performance"
switch = "ps3_htab.insert_readback"
default = "1 (the old read-back; 0 one hcall, 2 both and compare)"
forwarded = "not yet (test on the PS3 first: insert_readback=2 for a gaming session, the mismatch count must stay 0)"
tests = []
batches = []
+++

# powerpc/ps3: one hcall per hash table insertion, and htab hcall stats

`ps3_hpte_insert()` made two hcalls per hash page table entry: `lv1_insert_htab_entry()`, then
`lv1_read_htab_entries()` of the group only to learn whether the entry went into the secondary group.
The inserted index lv1 returns already says which, so with `ps3_htab.insert_readback=0` the read-back
goes; 2 does both and counts mismatches, and 1, the default, is the old way. These calls run with
interrupts soft-masked, and a profile booked that time as `__replay_soft_interrupts`: OpenTTD's OpenGL
driver, mapping a new 8 MB staging buffer every frame, spent 26% of its samples there and 8% at the
unlock in `ps3_hpte_invalidate` ([HOTPATHS.md](../../../docs/HOTPATHS.md)).

`/sys/module/ps3_htab/parameters/stats` counts the htab hcalls by kind with the time spent in them,
evictions and mismatches. The same profile led to Mesa's
[mesa-staging-pool-big](../mesa-staging-pool-big/patch.md) (0040), which removed most of those faults at
the source. The same-boot A/B of the switch ("irqoff-B" in [TESTS.md](../../../docs/TESTS.md)) has not
run yet.
