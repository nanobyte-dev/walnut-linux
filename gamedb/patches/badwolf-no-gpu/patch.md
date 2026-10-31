+++
component = "badwolf"
patches = ["patches/0007-no-GPU-in-the-browser-s-processes-no-DRM-device-no-E.patch"]
aliases = ["badwolf 0007"]
category = "performance"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# No GPU in the browser's processes

Acceleration NEVER and `WEBKIT_DISABLE_DMABUF_RENDERER=1`
([badwolf-memory-defaults](../badwolf-memory-defaults/patch.md)) were thought to keep GL out of WebKit, but on
the PS3 both the UI and the web process mapped Mesa's libgallium and held `/dev/dri` open: each was a Mesa
nv30 screen with a channel on the RSX, for a browser that draws with Cairo on the CPU (a gdb backtrace on the
PS3, 2026-10-29). The UI process asks EGL for the main DRM device unconditionally, and the web process makes a
surfaceless EGL display before the UI's preferences reach it.

Now `WEBKIT_WEB_RENDER_DEVICE_FILE` points at a device that doesn't exist and `LIBGL_ALWAYS_SOFTWARE=1` gives
the web process Mesa's software EGL, which opens no device; both only when the user hasn't set them. Programs
the browser starts (walnut:// links) get neither, so a game started from a GameDB page draws on the GPU as
usual. Measured on the PS3: no `/dev/dri` descriptor in any of the three processes, pages drawn as before.
