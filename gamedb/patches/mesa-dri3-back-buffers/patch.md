+++
component = "mesa"
patches = ["patches/0054-loader-dri3-keep-the-back-buffers-we-have-when-anoth.patch"]
aliases = ["Mesa 0054"]
category = "correctness"
switch = "none"
default = "always on"
gating = "none: the DRI3 loader of every driver; it acts only when a back buffer allocation fails with two in hand"
forwarded = "no"
tests = []
batches = []
+++

# The DRI3 loader keeps the back buffers it has when another can't be allocated

When X page-flips a fullscreen window, Mesa's DRI3 loader wants a third back buffer (3 with a swap
interval, 4 without). On the PS3 a game that has filled the VRAM can't get it: Trigger Rally logged
`ps3rsx: out of VRAM for a 8100 KiB BO` about 58 times a second for a whole session. `dri3_find_back`
then picked the empty slot every frame, the allocation failed, and the frontend kept drawing into the
back buffer it validated last while the swap presented whichever buffer was idle. One of them held an
old frame, so the menus flashed between the new picture and a stale one.

Once an allocation for an empty slot fails and the drawable has two back buffers, `max_num_back` is
capped at two for the drawable's life and the loader waits for one of them to go idle, both in
`getBuffers` and in the swap's `dri3_find_back_alloc`. One `dri3: no memory for back buffer 3,
keeping 2` line goes to the log. With fewer than two buffers nothing changes, since a single buffer
being scanned out never goes idle.

Trigger Rally's session of 2026-10-29 on Mesa ps3.1139~dev had steady menus, that log line once, and
10 `out of VRAM` lines instead of a stream ([technical notes](../../games/trigger-rally/technical.md)).
The missing textures of the same game are [mesa-gart-textures](../mesa-gart-textures/patch.md) (0055).
