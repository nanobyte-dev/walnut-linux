+++
component = "kernel"
patches = ["patches/local/0022-ps3-spu-cpu-relax.patch"]
aliases = ["local/0022", "kernel 0022"]
category = "performance"
switch = "none"
default = "always on"
authors = ["Thorsten Blum"]
forwarded = "no"
tests = []
batches = []
+++

# powerpc/ps3: cpu_relax() in ps3_create_spu()

Thorsten Blum's upstream patch (mainline 6050e3196d97): the boot-time wait for an SPE's execution status
to reach `SPE_EX_STATE_EXECUTED` spins with `cpu_relax()`, which lowers the spinning thread's SMT priority
so the core's other hardware thread gets the issue slots. A small boot-time change, taken in the survey of
2026-10-18 ([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md)).
