+++
component = "webkit2gtk"
patches = ["patches/0011-AcceleratedSurface-shared-memory-frames-in-Cairo-s-b.patch"]
aliases = ["webkit2gtk 0011"]
category = "correctness"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# AcceleratedSurface: shared-memory frames in Cairo's byte order on big-endian

In the shared-memory mode the web process reads each composited frame back with
`glReadPixels(GL_BGRA, GL_UNSIGNED_BYTE)` (bytes B, G, R, A), and the UI process wraps the buffer as
CAIRO_FORMAT_ARGB32, a native-endian word: A, R, G, B in memory on big-endian. Each pixel's bytes are now
reversed after the read on big-endian. Part of the GPU compositing set with
[webkit-compositing-without-webgl](../webkit-compositing-without-webgl/patch.md).
