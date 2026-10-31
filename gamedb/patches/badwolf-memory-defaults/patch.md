+++
component = "badwolf"
patches = ["patches/0002-Walnut-memory-defaults-no-hardware-acceleration-no-p.patch"]
aliases = ["badwolf 0002"]
category = "performance"
switch = "WALNUT_BROWSER_ACCEL, WALNUT_BROWSER_CACHE, WALNUT_BROWSER_MEMORY_LIMIT, WALNUT_BROWSER_DEVTOOLS"
default = "acceleration never, no page cache, WebGL or WebRTC"
forwarded = "no"
tests = []
batches = []
+++
# Walnut's memory defaults for the browser

Hardware acceleration policy NEVER (on a PC it took the web process from 218 to 90 MB PSS on the GameDB home
page), `WEBKIT_DISABLE_DMABUF_RENDERER=1` unless already set (24 MB in the UI process on the PC), no
back/forward page cache, no WebGL, WebRTC or media stream, no smooth scrolling (one repaint per scroll step
on a slow CPU) and no developer extras unless `WALNUT_BROWSER_DEVTOOLS=1`. Run-time overrides for
measuring: `WALNUT_BROWSER_ACCEL`, `WALNUT_BROWSER_CACHE` and `WALNUT_BROWSER_MEMORY_LIMIT`.

On the PS3 the acceleration policy turned out not to keep the RSX out of the browser's processes; that took
[badwolf-no-gpu](../badwolf-no-gpu/patch.md).
