+++
component = "plymouth"
patches = ["patches/0001-script-no-console-viewer.patch"]
aliases = ["plymouth 0001"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# Plymouth's script theme without a console viewer

A display has no console viewer when text rendering is unavailable (no label plugin) or with
`plymouth.prefer-fbcon`, but the sprite library's first full redraw called `ply_console_viewer_show/hide`
on it regardless. The two-step plugin already checks for NULL there; the script plugin now does too. With
the fix the boot splash (ps3-splash's script theme) needs no text rendering, so `plymouth-label` isn't
installed, which saved the 10 to 11 s that loading Pango, Cairo and fonts took at boot from the USB disk
([PACKAGING.md](../../../docs/PACKAGING.md)).

## Investigation

- **What crashed:** the plymouth daemon died with SIGSEGV as the script theme started.
- **Why:** `ply_console_viewer_hide(NULL)` on a display without a console viewer.
- **How it was found:** bringing up the boot splash without the label plugin (2026-10-16).
- **Minimal repro:** None yet: proposed `plymouthd --no-daemon` with the script theme, no label plugin and
  `plymouth.prefer-fbcon` in a container on a DRM-less framebuffer, `plymouth show-splash`, and the check
  that the daemon is still alive a few seconds later.

## Validation

Not yet validated on the PS3.
