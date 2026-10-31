+++
component = "mesa"
patches = ["patches/0003-nv30-context-pushbuf.patch"]
aliases = ["Mesa 0003"]
category = "stability"
switch = "none"
default = "always on"
gating = "none: generic push buffer fixes; the staging counter it adds is used only by 0007's PS3 limit"
forwarded = "no"
tests = ["mesa-alloc-fail"]
batches = []
+++

# nv30: push buffer robustness in the context

Three small fixes to how an nv30 context handles its push buffer, and one counter:

- the push buffer reserves room (`rsvd_kick = 16`) for the fence that `kick_notify` emits at every kick;
- a context that never queued anything has no push buffer space yet, so its flush gets space first,
  instead of writing the fence through a NULL pointer;
- `nv30_context_destroy` survives a context whose `nouveau_context_init` failed (out of GART), when there
  is no push buffer to kick;
- `staging_inflight`, the bytes of texture-upload staging whose copies may still be pending, which
  [mesa-texture-uploads-gart](../mesa-texture-uploads-gart/patch.md) (0007) uses to cap a context at 4 MB.

The empty-context flush was "Mesa 0006" in the numbering of the time (commit 97ffb04); the series was
renumbered when it moved into Debian's source package.

## Investigation

- **What crashed:** a flush of a GL context that had never queued a command wrote the fence through a
  NULL push buffer pointer, and destroying a context after a failed init dereferenced its missing push
  buffer.
- **Why:** stock nv30 assumes a context always has push buffer space by the time it is flushed, and that
  init succeeded. On the PS3 the GART was small and shared, so a context's init could fail, and kmscube's
  first frames flushed contexts with nothing queued.
- **How it was found:** bringing up kmscube and the first GL clients on ps3rsx (commit 97ffb04,
  2026-10-14: "flush of a context that never queued anything"), and the out-of-GART init failure on
  2026-10-15 (commit 7d11f9d).
- **Minimal repro:** `packages/patched/graphics/mesa/tests/context-pushbuf-test.c`
  (`tools/run-tests.sh --only mesa-alloc-fail`): `nv30_context.c` compiled whole against a fake winsys.
  `flush`: a context that never queued anything is flushed, and the kick must come with push buffer
  space; `destroy`: a context without a push buffer (its init failed) is destroyed. Against
  `nv30_context.c` as it was before this patch both must fail, and do: the kick with `cur` NULL, and a
  SEGV in `PUSH_KICK` from `nv30_context_destroy`. The `rsvd_kick` part is not covered.

## Validation

Not yet validated on the PS3.
