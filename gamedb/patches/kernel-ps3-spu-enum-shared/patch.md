+++
component = "kernel-ps3"
patches = ["patches/ps3/0100-spu-enum-shared-param.patch"]
aliases = ["ps3/0100"]
category = "feature"
switch = "spu_base.enum_shared"
default = "0 (off)"
authors = ["glevand", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# A switch to enumerate the shared SPE as well

Linux on the PS3 normally gets the six exclusive SPEs. With `spu_base.enum_shared=1` the PS3 SPU
setup also takes SPEs of the shared resource type, the console's seventh. It comes from glevand's OtherOS++ patch set as Sombat Tragoongoson refreshed it for 6.12 (the psx-place set), taken from Damian Dudycz's ps3 repository. The code is built in
and the parameter defaults to 0.

It has not been tried yet. psdevwiki warns that the 7th SPE serves GameOS-facing services, and
[research/FLASH-SYSCON.md](../../../docs/research/FLASH-SYSCON.md) and
[backlog/spu.md](../../../docs/backlog/spu.md) hold the plan: a second Petitboot entry with the
parameter and a same-boot benchmark of libspeq's work. Nothing persistent changes; a boot without the
parameter is the off switch.
