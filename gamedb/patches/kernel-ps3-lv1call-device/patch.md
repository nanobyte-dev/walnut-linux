+++
component = "kernel-ps3"
patches = ["patches/ps3/0250-ps3lv1call.patch"]
aliases = ["ps3/0250"]
category = "feature"
switch = "none"
default = "always on"
authors = ["glevand", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# ps3lv1call: hypercalls from userspace (manual load)

A misc device that makes arbitrary lv1 hypercalls on behalf of root, with a table of each call's
input and output counts. It was the tool of the OtherOS++ research. It comes from glevand's OtherOS++ patch set as Sombat Tragoongoson refreshed it for 6.12 (the psx-place set), taken from Damian Dudycz's ps3 repository.

[OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) classes it FORBIDDEN-capable (any lv1 call, flash included). `PS3_LV1CALL=m`: built as a module
nothing loads automatically, and blacklisted while ps3rsx runs, since it allows arbitrary GPU calls
([DRIVER.md](../../../docs/DRIVER.md#build-and-configuration)). The ps3disk write guard cannot stop
storage calls made through it ([kernel-ps3disk-write-guard](../kernel-ps3disk-write-guard/patch.md)).
