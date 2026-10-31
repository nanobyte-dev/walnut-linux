+++
component = "sdl12-compat"
patches = ["patches/0001-video-the-window-texture-in-the-application-s-format.patch"]
aliases = ["sdl12-compat 0001"]
category = "performance"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# sdl12-compat: the window texture in the application's format

The streaming texture behind the SDL 1.2 screen surface was always made in the renderer's first format. The
OpenGL renderer lists ARGB8888 first and a 32-bit SDL 1.2 screen is XRGB8888, so every update converted the
frame into the texture: 11% of Warmux's frame on the PS3, after the alpha blits. When the renderer lists the
application's own format, the texture is now made in it and an update is a plain copy.

With [sdl3-alpha-blit](../sdl3-alpha-blit/patch.md), Warmux went from 7.0 to 17.5 fps on 2026-10-29, with
Frozen Bubble, BomberClone and Pingus looking right too ([Warmux](../../games/warmux/technical.md)).
