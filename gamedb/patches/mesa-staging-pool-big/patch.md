+++
component = "mesa"
patches = ["patches/0040-nv30-big-texture-uploads-reuse-staging-buffers-too-N.patch"]
aliases = ["Mesa 0040", "Mesa 0036"]
category = "performance"
switch = "NV30_STAGING_POOL_MB"
default = "0 (off)"
gating = "switch NV30_STAGING_POOL_MB"
forwarded = "no"
tests = ["mesa-staging-pool", "mesa-transfer-cpu"]
batches = ["2026-10-18-mesa-416-hot-spots", "2026-10-19-ssdroot4-staging-pool", "2026-10-19-ssdroot5-staging-pool", "2026-10-19-ssdroot5-openttd-views"]
+++

# Texture uploads reuse idle, mapped staging buffers

Every texture upload had a staging buffer of its own: a GEM create (which zeroes the memory), an mmap,
then a munmap and a GEM close once the fence after its copy signalled. Two kinds of game paid for it:

- ClassiCube updates its animated water and lava tiles with `glTexSubImage2D` every frame, and on the
  PS3 that was 8.6% of its CPU (mmap 3.0, munmap 2.4, GEM create 1.6, close 0.7); QuakeSpasm and Yamagi
  Quake II spent about 2%.
- OpenTTD's OpenGL driver draws the screen on the CPU into a pixel buffer and uploads the dirty
  rectangle every frame: the whole 1080p screen (8.3 MB) and its 2 MB palette animation buffer at the
  title screen. Every map made a new GART staging buffer of the box's size: the zeroing (memset 5.3% of
  the game's samples), an mmap, one hash page table fault per 4 KiB page on the first write (each an lv1
  insertion with interrupts soft-masked: 26% of the samples), and later a munmap with one lv1
  invalidation per page (8.2%). About 40% of the game's CPU, and each buffer over 4 MB also waited for
  the GPU.

With `NV30_STAGING_POOL_MB=M` (up to 64; 0, the default, is off) write-only uploads take an idle, still
mapped buffer from a pool, and the fence work that freed the buffer gives it back instead. Only buffers
whose fence signalled are in the pool, so none is in a batch the GPU still runs and each is written
without a wait. Uploads that read keep a buffer of their own, since their map waits for the GPU's copy
into it. Two kinds of class share the switch:

- uploads up to 256 KiB: their power-of-two size class (4 KiB at least), at most 32 buffers and 2 MiB of
  GART, oldest freed first;
- bigger ones: their MiB class, or a buffer up to twice its size, at most M MiB and 16 buffers, and every
  buffer unused for a second goes back to the kernel (checked at each flush), so a burst of uploads at a
  load leaves no GART behind. A reused buffer takes no new GART, so it never waits on the in-flight
  limit. These decisions are in `nv30_staging_policy.h` with no Mesa types, tested on the host
  (`mesa-staging-pool`); the small classes are tested through `nv30_miptree.c` itself
  (`mesa-transfer-cpu`).

The pool is emptied when the screen goes away, and
[mesa-out-of-gart](../mesa-out-of-gart/patch.md) (0049) drains it when GART runs out.

Measured: the small classes alone (then Mesa 0036, `NV30_STAGING_POOL=N`) on ClassiCube's `chunks1`:
50.9 fps both ways with 4% less CPU (gamedb:batch/2026-10-18-mesa-416-hot-spots). The big classes
(`NV30_STAGING_POOL_MB=24`): OpenTTD's 8bpp blitter 34 to 52 fps and 32bpp-anim 11 to 20 fps with its
stalls 43 to 4.3 s (gamedb:batch/2026-10-19-ssdroot4-staging-pool); its heavy views 7 to 23 and 31 fps
(gamedb:batch/2026-10-19-ssdroot5-openttd-views); no change on ClassiCube and OpenArena
(gamedb:batch/2026-10-19-ssdroot5-staging-pool). Still off by default, with no recorded decision to turn
it on. 0036 was folded into this patch on 2026-10-31, its switch into `NV30_STAGING_POOL_MB`: setting it
now turns on the small classes too, which no batch has measured together with the big ones. The pool
follows [mesa-fence-bo-pool](../mesa-fence-bo-pool/patch.md) (0034). The investigation is
[gamedb:openttd/staging-buffer-per-frame](../../games/openttd/investigations/2026-10-19-staging-buffer-per-frame.md);
the kernel side of the same profile is [kernel-htab-one-hcall](../kernel-htab-one-hcall/patch.md).
