+++
id = "fence-segfault"
games = ["supertuxkart"]
driver_patches = ["mesa/0049"]
game_patches = []
sessions = ["2026-10-25-session", "2026-10-26-session", "2026-10-26-session-0223"]
batches = []
+++

# Garbage textures and a crash in Mesa after GART ran out (2026-10-26)

The driver mechanism of the fix, Mesa 0049, is in
[docs/DRIVER.md](../../../../docs/DRIVER.md#the-mesa-side). It follows the out-of-GART paths Mesa 0047
and 0048 closed for Sauerbraten ([online-crashes](../../sauerbraten/investigations/2026-10-24-online-crashes.md)).

## Symptom

The first played session (2026-10-25, Scrap) ran at 7 fps on the old default graphics, and every
texture was garbage after a restart. The first rules-2.0 session (2026-10-26, package ps3.833, Mesa
ps3.827) ended in a crash on Ravenbridge Mansion, with a 21.7 s freeze in play before it, and objects
popping in and out on that track (the player, side by side with the PC); the last shot is black. The
segfault came while loading the track's sky, after 414 "out of GART" lines in dmesg.

After the segfault the RSX stopped drawing: X kept running but nothing drew, a fresh X couldn't draw
either (the 3D state still the old X's target, 0x900000; the TV showed the game's last frame), the ssh
reboot hung, and the user power-cycled the console. That driver question stays in docs/TESTS.md "Next".

## Cause

A segfault at 0x20: `nv->fence` was NULL in `_nouveau_fence_next()` after a 4 KiB fence wait object
could not be had with GART exhausted; the failed kick (`kick_notify` false) also made `pushbuf_submit`
drop that whole batch. In the same state the texture transfer map returned NULL (GL_OUT_OF_MEMORY, which
games don't check), so textures kept their old pixels: the garbage after a restart and the popping
objects.

## Fix

Mesa 0049 (commit 712dcf6): out of GART, nothing crashes or is lost silently. A context always has a
current fence: the next one is made before the old one goes, its wait object comes after waiting for the
GPU's submitted work, else the fence goes without one and is polled. A texture upload with no staging
buffer waits for the GPU, drains the staging pools and tries again, else goes through malloc memory and
a CPU copy into the CPU-mapped VRAM. Buffer maps wait and retry for their staging area; a fragment
program or the blit's program with no buffer keeps the bound one. Host tests `alloc-fail-test.c` and
`transfer-cpu-test.c`, in `mesa-query-steal`.

The next session of the same day, on Mesa ps3.850 (the GART fix), had every track right: the desert
tutorial, Ravenbridge Mansion and its restart, the mines, the castle, snow, subsea; kart shadows and
textures as on the PC, no crash. It also had the package's lean defaults (supertuxkart 0006) and pad
scheme (0007), pushed at the same time.
