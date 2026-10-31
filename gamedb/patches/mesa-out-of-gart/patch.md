+++
component = "mesa"
patches = ["patches/0049-nouveau-nv30-out-of-GART-a-context-keeps-its-fence-a.patch"]
aliases = ["Mesa 0049"]
category = "stability"
switch = "none"
default = "always on"
gating = "none: nouveau's fence code and nv30's transfers, for every nouveau driver and nv30 card"
forwarded = "no"
tests = ["mesa-alloc-fail", "mesa-transfer-cpu"]
batches = []
+++

# Out of GART, a context keeps its fence and texture uploads go through the CPU

Every nouveau fence has a 4 KiB GART buffer to wait on. When GART was full, a kick dropped the
context's fence and failed to make the next one, leaving `nv->fence` NULL, and the failed kick also
made `pushbuf_submit()` drop the whole batch. Texture uploads in the same state got no staging buffer
and were lost. The patch makes "out of GART" cost time instead of a crash or lost data:

- `_nouveau_fence_next()` makes the next fence before letting the current one go. A fence whose wait
  object can't be had first waits for the GPU's submitted work (signalled fences free their objects
  and their works free staging buffers), then goes without one and is polled. Only a failed malloc of
  the fence itself fails, and then the old fence stays current.
- A texture upload with no staging buffer waits for the GPU, gives the staging pools' idle buffers
  back to the kernel and tries again; failing that, the data goes through malloc memory and the CPU
  copies it into the texture, which on the PS3 is CPU-mapped VRAM.
- Buffer maps wait and retry for their staging area; a fragment program or the blit's program with no
  buffer keeps the bound one instead of writing through a NULL map.

It follows [mesa-upload-null](../mesa-upload-null/patch.md) (0047) and
[mesa-alloc-fail](../mesa-alloc-fail/patch.md) (0048), and drains the pools of
[mesa-staging-pool](../mesa-staging-pool/patch.md) (0036) and [mesa-staging-pool-big](../mesa-staging-pool-big/patch.md)
(0040).

## Investigation

- **What crashed:** SuperTuxKart on 2026-10-26 (package ps3.833, Mesa ps3.827) segfaulted at 0x20 in
  `_nouveau_fence_next()` (`nv->fence->state` with `nv->fence` NULL), from `nv30_context_kick_notify()`
  under a texture upload's throttle, after 414 `ps3rsx: out of GART` lines, while loading Ravenbridge
  Mansion's sky. Before it: a 21.7 s freeze, objects popping in and out, and in an earlier session
  every texture garbage after a restart.
- **Why:** an earlier kick had dropped the fence and couldn't allocate the next one's wait buffer; the
  same full GART made `nv30_miptree_transfer_map()` return NULL (`GL_OUT_OF_MEMORY`, which games don't
  check), so textures kept old pixels.
- **How it was found:** the played sessions of 2026-10-25 and 2026-10-26
  ([gamedb:supertuxkart/fence-segfault](../../games/supertuxkart/investigations/2026-10-26-fence-segfault.md)).
  After that crash the RSX also stopped drawing for a fresh X until a power cycle; that is a separate
  driver question in [TESTS.md](../../../docs/TESTS.md), not fixed here.
- **Minimal repro:** `packages/patched/graphics/mesa/tests/alloc-fail-test.c` (fences with a full and
  with no GART, against a fake GPU) and `transfer-cpu-test.c` (texture transfers with a full GART), in
  `tools/run-tests.sh --only mesa-alloc-fail` and `--only mesa-transfer-cpu`.

## Validation

Not yet validated on the PS3.
