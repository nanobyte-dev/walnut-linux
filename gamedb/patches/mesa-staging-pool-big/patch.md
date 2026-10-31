+++
component = "mesa"
patches = ["patches/0040-nv30-big-texture-uploads-reuse-staging-buffers-too-N.patch"]
aliases = ["Mesa 0040"]
category = "performance"
switch = "NV30_STAGING_POOL_MB"
default = "0 (off)"
gating = "switch NV30_STAGING_POOL_MB"
forwarded = "no"
tests = ["mesa-staging-pool"]
batches = ["2026-10-19-ssdroot4-staging-pool", "2026-10-19-ssdroot5-staging-pool", "2026-10-19-ssdroot5-openttd-views"]
+++

# Big texture uploads reuse staging buffers too

OpenTTD's OpenGL driver draws the screen on the CPU into a pixel buffer and uploads the dirty rectangle
every frame: the whole 1080p screen (8.3 MB) and its 2 MB palette animation buffer at the title screen.
Every map made a new GART staging buffer of the box's size. On the PS3 that meant a GEM create that
zeroes the memory (memset 5.3% of the game's samples), an mmap, one hash page table fault per 4 KiB
page on the first write (each an lv1 insertion with interrupts soft-masked: 26% of the samples), and
later a munmap with one lv1 invalidation per page (8.2%). About 40% of the game's CPU, and each buffer
over 4 MB also waited for the GPU.

With `NV30_STAGING_POOL_MB=M` (up to 64) write-only uploads over 256 KiB take an idle, still mapped
buffer of their MiB class, or one up to twice its size, and the fence work gives it back instead of
freeing it. The pool holds at most M MiB and 16 buffers, and every buffer unused for a second goes
back to the kernel (checked at each flush), so a burst of uploads at a load leaves no GART behind. A
reused buffer takes no new GART, so it never waits on the in-flight limit. The decisions are in
`nv30_staging_policy.h` with no Mesa types, tested on the host (`mesa-staging-pool`).

Measured: OpenTTD's 8bpp blitter 34 to 52 fps and 32bpp-anim 11 to 20 fps with its stalls 43 to 4.3 s
(gamedb:batch/2026-10-19-ssdroot4-staging-pool); its heavy views 7 to 23 and 31 fps
(gamedb:batch/2026-10-19-ssdroot5-openttd-views); no change on ClassiCube and OpenArena
(gamedb:batch/2026-10-19-ssdroot5-staging-pool). Still off by default, with no recorded decision to
turn it on. It extends [mesa-staging-pool](../mesa-staging-pool/patch.md) (0036) to big uploads. The
investigation is [gamedb:openttd/staging-buffer-per-frame](../../games/openttd/investigations/2026-10-19-staging-buffer-per-frame.md);
the kernel side of the same profile is [kernel-htab-one-hcall](../kernel-htab-one-hcall/patch.md).
