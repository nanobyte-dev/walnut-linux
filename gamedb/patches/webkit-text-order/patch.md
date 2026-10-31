+++
component = "webkit2gtk"
patches = ["packaging/0002-Walnut-the-code-browsing-runs-first-in-.text-packed-.patch"]
aliases = ["webkit2gtk packaging/0002"]
category = "performance"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# WebKitGTK: the code browsing runs first in .text, packed relocations

On ppc64 the libraries are linked with a section ordering file (`debian/walnut-text-order.ld`): the 21,897
functions of libwebkit2gtk and 4,478 of libjavascriptcoregtk that ran while Badwolf loaded and scrolled
pages under qemu-user come first in `.text`. With 128 KB of readahead per page fault, a browsing session read
most of the 71 MB of libwebkit2gtk's code before; now about half. And DT_RELR (`-z pack-relative-relocs`)
shrinks `.rela.dyn` from 13.2 MB to 1.3 MB. The code itself is the same. On the PC this halved what the
libraries read into memory (76 to 35 MB on Wikipedia, [BROWSERS.md](../../../docs/research/BROWSERS.md)).
