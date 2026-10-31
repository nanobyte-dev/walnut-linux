+++
component = "webkit2gtk"
patches = ["patches/0009-GTK-accelerated-compositing-without-WebGL.patch"]
aliases = ["webkit2gtk 0009"]
category = "feature"
switch = "WEBKIT_DISABLE_COMPOSITING_MODE, Badwolf's WALNUT_BROWSER_ACCEL"
default = "off (Badwolf: acceleration never)"
forwarded = "no"
tests = []
batches = []
+++
# GTK: accelerated compositing without WebGL

HardwareAccelerationManager turned hardware acceleration off for good when WebGL was left out of the
build, so a build without WebGL could never composite, whatever the policy said. TextureMapper and the
threaded compositor are built on GTK either way, on GLES 2 through EGL, and need nothing of WebGL. Without
WebGL the same checks now decide as with it.

This and the two next patches ([webkit-texmapper-matrix-be](../webkit-texmapper-matrix-be/patch.md),
[webkit-shm-frames-be](../webkit-shm-frames-be/patch.md)) are for GPU compositing, which stays off by
default. With them, the RSX streams of four composited pages pass ps3rsx's validator on the PC
([BROWSERS.md](../../../docs/research/BROWSERS.md#gpu-compositing)).
