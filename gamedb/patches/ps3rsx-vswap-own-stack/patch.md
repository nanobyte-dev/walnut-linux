+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx af99cb6", "ps3vswap"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# The VRAM swap serves requests on its own work queue

Commit af99cb6 (2026-10-15) moved `ps3vswap0`'s request handling to the driver's own work queue, which runs
it on a fresh stack. blk-mq can call `queue_rq` inline from a plug flush, and in direct reclaim that flush
comes deep in an already deep stack. The same commit added the background demotion (the pool keeps 8 MiB
free by moving its oldest chunk's pages to the disk, so recent swap-outs land in VRAM) and page-by-page
write-backs. The kernel-submitted GPU copies that came next (`kcopy.c`) run in the same context and never
wait for a lock a submitter might hold ([DRIVER.md](../../../docs/DRIVER.md#vram-swap-ps3vswap0)).

## Investigation

- **What crashed:** the kernel stack nearly ran out: an interrupt found less than 4 KiB of stack left while
  the VRAM swap served a request from direct reclaim.
- **Why:** blk-mq ran the device's `queue_rq` inline from a plug flush inside direct reclaim, so the
  swap-out, its VRAM copy and the interrupt all ran on top of the reclaiming task's stack.
- **How it was found:** the VRAM swap's bring-up on 2026-10-15.
- **Minimal repro:** None yet: the PS3 memory-pressure test of
  [ps3rsx-vswap-bio-reserve](../ps3rsx-vswap-bio-reserve/patch.md), with a game drawing at the same time so
  interrupts land during swap-outs.

## Validation

Not yet validated on the PS3.
