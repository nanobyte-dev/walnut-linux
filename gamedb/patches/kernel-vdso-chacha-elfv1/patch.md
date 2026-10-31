+++
component = "kernel"
patches = ["patches/local/0001-powerpc-vdso-elfv1-chacha-dot-symbol.patch"]
aliases = ["local/0001", "kernel 0001"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++

# vDSO getrandom on big-endian ELFv1: define the dot symbol

On a big-endian ELFv1 kernel the C part of the vDSO's getrandom calls
`.__arch_chacha20_blocks_nostack`, the ELFv1 name of the function's code, which the assembly never
defines. The vDSO linker then resolves the call to address 0. The patch defines the dot symbol next to
the assembly function, so the call lands on the ChaCha20 code. Only ELFv1 (our kernel, built ELFv1 for
Petitboot's kexec) needs it; ELFv2 has no dot symbols.

It was the first patch of the series, part of getting the kernel to boot on 2026-10-14.

## Investigation

- **What crashed:** every userspace process that used glibc's vDSO getrandom (glibc 2.41 and later)
  died with SIGILL, executing the vDSO's ELF header at address 0 of the vDSO mapping.
- **Why:** the undefined `.__arch_chacha20_blocks_nostack` linked to 0, so the call from the C
  getrandom jumped to the start of the vDSO image.
- **How it was found:** the first boots of our 6.12 kernel with a Debian ppc64 userspace (commit 809db6b,
  "Boot works: ... vDSO ELFv1 fix").
- **Minimal repro:** None yet: proposed: a few-line C program calling `getrandom()` through glibc 2.41+,
  run on the PS3 (or any big-endian ELFv1 powerpc kernel); without the patch it gets SIGILL.

## Validation

Not yet validated on the PS3.
