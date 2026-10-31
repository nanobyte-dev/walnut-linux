+++
component = "webkit2gtk"
patches = ["packaging/0001-Walnut-the-GTK-3-flavour-only-without-the-features-a.patch"]
aliases = ["webkit2gtk packaging/0001"]
category = "config"
switch = "pkg.webkit2gtk.novideo (build profile)"
default = "video on"
forwarded = "no"
tests = []
batches = []
+++
# WebKitGTK: the GTK 3 flavour only, without the features a PS3 browser leaves out

Only the 4.1 API (GTK 3) flavour is built, for Badwolf: no GTK 4 packages, documentation, introspection
data, WebDriver or MiniBrowser. Left out at build time: WebGL and WebGPU, WebRTC, media capture, WebAudio,
WebCodecs, speech synthesis, encrypted media, gamepads, geolocation, spellchecking, AVIF and JPEG XL, PDF.js,
the remote inspector, the Wayland target and the bubblewrap sandbox (no namespaces in the PS3's kernel).
`<video>` and Media Source stay, with GStreamer's libav plugins; the build profile
`pkg.webkit2gtk.novideo` also drops GStreamer, for measuring. On ppc64 the code is tuned for the Cell PPE
(`-mtune=cell`; the instruction set stays the baseline). The cuts save memory and attack surface
([BROWSERS.md](../../../docs/research/BROWSERS.md)).

Fewer features change how WebKit's unified sources are bundled, which exposed one missing include
([webkit-eventloop-include](../webkit-eventloop-include/patch.md)).
