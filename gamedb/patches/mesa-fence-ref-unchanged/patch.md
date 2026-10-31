+++
component = "mesa"
patches = ["patches/0015-nouveau-fence-ref-unchanged.patch"]
aliases = ["Mesa 0015"]
category = "performance"
switch = "none"
default = "always on"
gating = "none: generic nouveau, same reference counts on every driver"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-246"]
+++

# nouveau: no fence reference work when the fence doesn't change

`nv30_state_validate` re-references the context's current fence on every buffer bound to a draw, once
per draw, and `nouveau_transfer_write` and `nouveau_copy_buffer` do the same per buffer write. Between two
kicks the fence is the same, so nearly all of these calls find `*ref` already pointing at it, yet
`nouveau_fence_ref` still took the screen's fence lock and did an increment and a decrement that cancel
out: four atomic read-modify-writes, two of them with a full barrier. On the PS3's in-order PPE, where
`lwarx/stwcx.` and `sync` are slow, `nouveau_fence_ref` and `_nouveau_fence_ref` took 2 to 11% of the CPU
in games, most in immediate-mode draws (X-Moto, ClassiCube). Both now return at once when `*ref` already
holds the fence; the counts end the same.

It is the first of three patches from the profiles of Mesa ps3.223 (gamedb:batch/2026-10-16-mesa-223);
[mesa-validate-fence-unchanged](../mesa-validate-fence-unchanged/patch.md) (0016) skips the call itself
in nv30's validation and [mesa-winsys-kref-once](../mesa-winsys-kref-once/patch.md) (0017) trims the
winsys side. Together they gave X-Moto 37 against 35 fps and ClassiCube 44.9 against 43.2
(gamedb:batch/2026-10-17-mesa-246, single runs).
