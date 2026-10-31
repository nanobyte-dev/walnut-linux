+++
component = "webkit2gtk"
patches = ["patches/0006-GTK-no-view-transitions-without-accelerated-composit.patch"]
aliases = ["webkit2gtk 0006", "WebKit fix 6"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# No view transitions without accelerated compositing

`document.startViewTransition()` puts the page in compositing mode whatever the settings say. With hardware
acceleration NEVER, or without the DMA-BUF renderer, the UI process has no accelerated backing store. Now the
web process doesn't offer view transitions while accelerated compositing is off (pages fall back as in
browsers without the API), and the UI process ignores compositing requests it can't show instead of
dereferencing a null backing store.

## Investigation

- **What crashed:** Badwolf's UI process, in `webkitWebViewBaseEnterAcceleratedCompositingMode`: YouTube took
  the browser down within seconds, on the PC too and with Debian's build as well.
- **Why:** a view transition enables the compositor, whose root layer makes the drawing area enter
  accelerated compositing, and the UI side had nothing to enter it with.
- **How it was found:** the page list of [BROWSERS.md](../../../docs/research/BROWSERS.md) on the PC
  (YouTube's watch page).
- **Minimal repro:** None yet: proposed a local page that calls `document.startViewTransition()`, loaded in
  Badwolf with acceleration NEVER under Xvfb, and the check that the browser is still running.

## Validation

Not yet validated on the PS3.
