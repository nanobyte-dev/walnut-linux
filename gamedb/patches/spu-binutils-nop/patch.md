+++
component = "spu-binutils"
patches = ["0001-gas-spu-nop-is-an-instruction.patch"]
aliases = ["binutils 0001"]
category = "port"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# gas/spu: "nop" is the SPU instruction, not the .nop directive

The SPU assembler looks pseudo-ops up without a leading dot first. Since binutils 2.36 a generic "nop"
pseudo-op shadows the SPU `nop` instruction, so GCC's `nop 127` (padding for dual issue) became `.nop 127`:
32 nop instructions instead of one. The bloat moved branches out of the 9-bit range of GCC's branch hints
("Relocation doesn't fit" on `hbrr`), or silently grew the code. The generic pseudo-op is masked as "set" and
"eqv" already are ([PACKAGING.md](../../../docs/PACKAGING.md), [SPU-GCC.md](../../../docs/SPU-GCC.md)).
