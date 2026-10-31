+++
component = "webkit2gtk"
patches = ["patches/0007-Cairo-canvas-pixel-conversions-on-big-endian.patch"]
aliases = ["webkit2gtk 0007", "WebKit fix 3"]
category = "correctness"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# Cairo canvas pixel conversions on big-endian

Cairo's ARGB32 surfaces hold native-endian 32-bit pixels: B, G, R, A in memory on little-endian, A, R, G, B on
big-endian. The non-Skia conversions read them as BGRA bytes everywhere, so on big-endian `getImageData`
returned opaque red as transparent black, and every readback, `putImageData` and filter through a pixel
buffer had its channels moved. With Cairo on big-endian they now convert through byte-swapped rows. The
canvas lines of the [self-test page](../../../docs/research/webkit/be-test.html) failed until this fix.
