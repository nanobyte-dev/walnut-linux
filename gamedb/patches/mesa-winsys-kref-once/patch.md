+++
component = "mesa"
patches = ["patches/0017-nouveau-winsys-validate-kref-once.patch"]
aliases = ["Mesa 0017"]
category = "performance"
switch = "none"
default = "always on"
gating = "none: the generic nouveau winsys; the relocations and buffer list are unchanged"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-246"]
+++

# nouveau/winsys: look a buffer up once per run of relocations in pushbuf_validate

`pushbuf_validate` adds the buffers a bufctx gained since the last validation to the kernel's buffer list
and writes their relocations. For each one it called `pushbuf_kref`, then `pushbuf_krel` twice, and every
`pushbuf_krel` looked up both the push buffer's kref and the buffer's kref again. nv30 adds one bufref
per vertex attribute (all in the same buffer) and two per texture, so a typical draw repeated the same
`pushbuf_kref` two to four times and did twice as many kref lookups as relocations. A call for the same
buffer with the same domain and access flags as the call just before changes nothing, so it is now
skipped, and the push buffer's kref is looked up once.

On the PS3 most of this work went away a day later with
[mesa-no-relocations](../mesa-no-relocations/patch.md) (0019), which sends no relocations at all; the
patch still applies to the buffer list and to other kernels. Measured with 0015 and 0016 in
gamedb:batch/2026-10-17-mesa-246.
