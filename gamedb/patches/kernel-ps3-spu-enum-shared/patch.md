+++
component = "kernel-ps3"
patches = ["patches/ps3/0100-spu-enum-shared-param.patch"]
aliases = ["ps3/0100"]
category = "feature"
switch = "spu_base.enum_shared"
default = "1 (on) in our kernels' boot arguments from 6.12.115+ps3.1554; the parameter's own default is 0"
authors = ["glevand", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# A switch to enumerate the shared SPE as well

Linux on the PS3 normally gets the six exclusive SPEs. With `spu_base.enum_shared=1` the PS3 SPU
setup also takes SPEs of the shared resource type, the console's seventh. It comes from glevand's OtherOS++ patch set as Sombat Tragoongoson refreshed it for 6.12 (the psx-place set), taken from Damian Dudycz's ps3 repository. The code is built in
and the parameter defaults to 0.

Our kernels boot with `spu_base.enum_shared=1` from 6.12.115+ps3.1554 (the kernel package's boot
arguments): seven SPEs, and with local/0061 the VRAM swap's SPE is an exclusive one. psdevwiki warns
that the 7th SPE serves GameOS-facing services, which Linux doesn't use; on the PS3 (2026-11-15, kernel
.1545 booted with the parameter) the SPEs, the VRAM swap, libspeq's benchmark at full load, games,
sound and the reboot back to GameOS all worked ([SPU.md](../../../docs/SPU.md#the-7th-spe)). Nothing
persistent changes. The `walnut-safe` Petitboot entry boots without it, and `PS3_SPE_SHARED=0` in
`/etc/default/ps3-boot` turns it off on every entry.
