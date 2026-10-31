+++
component = "boost-context"
patches = ["patches/0001-context-ppc64-save-fprs-vrs.patch"]
aliases = ["boost-context 0001"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# Boost.Context ppc64: keep F14-F31, FPSCR and V20-V31 across a switch

`jump_fcontext` and `ontop_fcontext` for ppc64 (ELFv1 and ELFv2) saved only the non-volatile general
registers, CR and (since late 2024) V31. The ABI also has F14-F31 non-volatile, V20-V31 with AltiVec, and
FPSCR must not change across a call. The context data grows from 200 to 552 bytes (F14-F31, FPSCR, V20-V31,
16-byte aligned), and `make_fcontext` gives a new context its creator's FPSCR. The ppc32 code saves F14-F31
and FPSCR the same way.

## Investigation

- **What crashed:** no crash on the PS3; wrong values. A double the compiler kept in F14 across
  `fiber::resume()` came back with the other context's value.
- **Why:** the switch didn't save the non-volatile floating-point and vector registers.
- **How it was found:** writing a test for the fcontext build of
  [boost-context-ppc64-fcontext](../boost-context-ppc64-fcontext/patch.md): twelve doubles live across
  `resume()` in both contexts failed on ppc64 (qemu, Debian sid, 1.92.0), and so did AltiVec vectors at
  `-maltivec`.
- **Minimal repro:** `packages/patched/libs/boost-context/test/ctxtest.cpp`, run by `test/run-test.sh` in a
  ppc64 chroot under qemu (with Boost's own Context, Coroutine and Fiber tests); not a `tools/run-tests.sh`
  suite yet.

## Validation

Not yet validated on the PS3.
