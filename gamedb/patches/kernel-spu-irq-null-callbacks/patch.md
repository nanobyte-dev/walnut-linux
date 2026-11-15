+++
component = "kernel"
patches = ["patches/local/0059-spu-irq-null-callbacks.patch"]
aliases = ["local/0059", "kernel 0059"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["kernel-spu-reserve"]
batches = []
+++

# powerpc/cell: no SPE interrupt callback is called while it is NULL

spufs's `spu_unbind_context` sets an SPE's four interrupt callbacks to NULL, and `spu_base.c`'s class 0
handler called `stop_callback` without a test, even with no status pending. lv1 can deliver an SPE
interrupt after the SPU was seen stopped (local/0058 saw such late deliveries), so a class 0 interrupt
between an unbind and the next bind was a NULL call in hard interrupt context. The class 1 path and the
class 2 handler call theirs only for a set, unmasked status bit, but nothing else kept them from NULL either.

Each of the seven calls now runs only when its callback is set, tested under `register_lock`, which
spufs's bind and unbind hold when they change the callbacks. With no context bound there is nobody to
tell (`spufs_stop_callback` itself does nothing without `spu->ctx`), and the status is cleared as before.
A handler that finds its callback set runs as it did.

## Investigation

Found while reading spufs and spu_base for the SPE-sharing design (2026-11-14): `spu_unbind_context`
(spufs/sched.c) clears the callbacks under `register_lock`, and `spu_irq_class_0` calls `stop_callback` with
whatever status is pending, none included. local/0058's own work had seen lv1 deliver SPE interrupts after
the SPU was stopped and after `synchronize_irq()`. No oops was seen on the PS3; the window is between an
unbind and the next bind, so more context switches (more processes than SPEs) make it wider. All seven
call sites in `spu_base.c` are the only callers of the four callbacks in the tree.

## Validation

Host test `kernel-spu-reserve` checks the patch: only `spu_base.c`, no lv1 call, every callback call in
the new handlers behind a NULL test, nothing removed but the unguarded calls. The kernel objects compile
with the release config. On the PS3: not yet (a late class 0 delivery can't be forced; it shows only as the
absence of an oops).
