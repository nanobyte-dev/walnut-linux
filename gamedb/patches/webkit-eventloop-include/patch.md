+++
component = "webkit2gtk"
patches = ["patches/0005-DocumentLoader.cpp-include-EventLoop.h.patch"]
aliases = ["webkit2gtk 0005", "WebKit fix 7"]
category = "build"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# DocumentLoader.cpp includes EventLoop.h

DocumentLoader.cpp uses `EventLoopTaskGroup::queueTask` and got its definition from another file of its
unified-source bundle. With the features [webkit-gtk3-only](../webkit-gtk3-only/patch.md) leaves out, the
bundles are cut differently and the build failed (member access into an incomplete type).
