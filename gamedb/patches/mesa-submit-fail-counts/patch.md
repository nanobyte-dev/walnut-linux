+++
component = "mesa"
patches = ["patches/0061-nouveau-a-submission-the-kernel-fails-counts-as-cancelled.patch"]
aliases = ["Mesa 0061"]
category = "stability"
switch = "none"
default = "always on"
gating = "none: any machine; the count only moves when a submission fails"
forwarded = "no"
tests = ["mesa-submit-fail:notify", "mesa-submit-fail:ratelimit", "mesa-submit-fail:ecanceled", "mesa-submit-fail:failed", "mesa-submit-fail:ok", "mesa-submit-fail:threads", "mesa-submit-fail:fix-off", "mesa-query-lost:failed"]
batches = []
+++

# A submission the kernel fails counts as cancelled

The winsys counted only the submissions ps3rsx cancelled (`-ECANCELED`) in `nouveau_device::cancelled`. A push
buffer the kernel failed another way (the pushbuf ioctl returning `-EINVAL` or `-ENOMEM`) was not sent either, but nothing
said so: the reports of its queries never came, and a wait for one ended nowhere except on the PS3, where nv30's
one-second deadline caught it. It now counts too (`NOUVEAU_CANCEL`): the rest of the push buffer's words are dropped, the
driver sends its state again before it draws, and the reports it held are taken as lost.

The count is read and written atomically (`NOUVEAU_CANCELLED`, `NOUVEAU_CANCEL`): the contexts of one screen submit from
their own threads. nv30's query wait gives up on a report whose submission did not go on every machine; the second-long
deadline stays the PS3's.

## Investigation

- **How it was found:** the round 3 review of the query deadline (a failing submit left its queries' reports pending on every machine without the PS3's deadline).
- **Minimal repro:** `packages/patched/graphics/mesa/tests/submit-fail-test.c` (`tools/run-tests.sh --only mesa-submit-fail`) and `query-lost:failed`.

## Validation

Host: `submit-fail-test.c` compiles the winsys's `nouveau.c` whole against a stub `xf86drm.h` and a fake ioctl: a cancelled
and a failed submission both count once, an accepted one does not, and four threads of cancelled submissions lose no count.
a failed fence counts too and the message comes at the 1st, 2nd, 4th... failure (`notify`, `ratelimit`). `mesa-query-lost:failed` is the query side on a machine without the deadline. Both fail on the file before the patch.
The PS3 stream does not change (no failing submit, no new word). Nothing to measure on the PS3 for a run without a failure.
