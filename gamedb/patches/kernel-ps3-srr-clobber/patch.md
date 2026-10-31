+++
component = "kernel-ps3"
patches = ["patches/ps3/0440_lv1call_h_cobber.patch", "patches/ps3/0450_interrupt_h_cobber.patch"]
aliases = ["ps3/0440", "ps3/0450"]
category = "stability"
switch = "none"
default = "always on"
authors = ["René Rebe", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# Every lv1 call marks SRR0/SRR1 as clobbered

64-bit powerpc keeps a per-CPU `srr_valid` flag so the interrupt exit can skip reloading SRR0 and
SRR1 when nothing has touched them. 0440 makes every lv1 call wrapper in `lv1call.h` call
`srr_regs_clobbered()` before the hypercall, and 0450 marks that helper `always_inline` for the
wrappers. René Rebe posted the pair to linuxppc-dev as V2 on 2025-11-18 (not merged); the psx-place set
carries it (Sombat Tragoongoson), and we took it from Damian Dudycz's ps3 repository. The copy in our series is identical
to V2. Compiled in.

## Investigation

- **What crashed:** [OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) records it as a stability fix against bogus SRR warnings. We have no log
  of our own that shows them.
- **Why:** the patch's premise is that a hypercall changes SRR0/SRR1 while the kernel's flag still
  says they are valid, so an interrupt exit could return with stale values; marking them clobbered
  forces the reload.
- **How it was found:** upstream, by its author; we found it in the 2026-10-18 survey of PS3 kernel
  patches ([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md)).
- **Minimal repro:** None yet: proposed a debug kernel with `CONFIG_PPC_RFI_SRR_DEBUG` and the two
  patches left out, under a load heavy in hypercalls (hash faults, a game), checking dmesg for SRR
  warnings, then the same with them in.

## Validation

Not yet validated on the PS3.
