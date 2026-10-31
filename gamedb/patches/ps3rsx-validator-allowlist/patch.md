+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx f4084fc"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["ps3rsx-shadow"]
batches = []
+++
# The validator's per-class method allowlist

ps3rsx decodes every push before the GPU sees it, because one bad command wedges the RSX until the console
reboots ([RSX.md](../../../docs/RSX.md#errors-and-what-wedges-the-gpu)). The first validator refused only
JMP, CALL and RETURN, incomplete bursts, object binds with handles that aren't the hypervisor's, and
unknown context DMAs. Commit f4084fc (2026-10-14) added the part that does most of the work: a list of the
methods each object class may receive (3D, both M2MF objects, surface 2D, swizzled surface and scaled image),
taken from what Mesa's nv30 driver can emit
([MESA-METHODS.md](../../../docs/MESA-METHODS.md#4-per-class-method-list)) plus what our raw tests use, all of
which had run on the RSX. Anything else is logged and refused. The same commit made the image-from-CPU
object refuse everything (Mesa doesn't use it), allowed only SET_REFERENCE and the semaphore release among
the host methods (not the FIFO semaphore acquire, which can block for good), checked that SIFM's surface
is a surface object, and kept a push's validator state only when the whole push passes.

The commit's subject leads with a kernel fix: the gelic netpoll change that became
[kernel-gelic-netpoll](../kernel-gelic-netpoll/patch.md) (local/0006) went in with it; the ps3rsx part is
the allowlist in `validate.c`. Version 0.249 later turned the list into a table with one lookup per word
(commit 9e2a3b9, [ps3rsx-validator-submit-state](../ps3rsx-validator-submit-state/patch.md)), and the
indexed ranges are expanded to every unit ([DRIVER.md](../../../docs/DRIVER.md#the-validator)).

## Investigation

- **What crashed:** graph exception 0x109, the RSX wedged: M2MF methods sent to the surface-2D object.
- **Why:** the RSX raises an exception for a method its object class doesn't have; nothing but a console
  reboot recovers from one.
- **How it was found:** the RSX hardware probe of 2026-10-14 (commit 588face) recorded 0x109 among the
  fault behaviours, before any driver existed; the allowlist went in during the Phase 2 bring-up the same
  day ([DRIVER.md](../../../docs/DRIVER.md#phase-results-2026-10-14)), before Mesa was allowed to submit.
- **Minimal repro:** `packages/walnut/drivers/ps3rsx/test/validate-test.c` sends M2MF methods to the
  surface-2D object and expects a refusal (`tools/run-tests.sh --only ps3rsx-shadow`).

## Validation

Not yet validated on the PS3.
