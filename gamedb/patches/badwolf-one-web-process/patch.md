+++
component = "badwolf"
patches = ["patches/0001-one-web-context-and-one-web-process-for-all-tabs-no-.patch"]
aliases = ["badwolf 0001"]
category = "performance"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# One web context and one web process for all tabs

Upstream Badwolf gives every new unrelated tab its own ephemeral web context: a network process and a web
process per tab, and nothing kept between runs. Each extra web process costs tens of MB on a 256 MB machine,
so every tab now shares one persistent context (cookies, local storage and the disk cache under the XDG
directories; third-party cookies refused) and is created related to an open tab's view, which puts all
tabs in one WebKitWebProcess. The cost: one tab's crash takes the other pages with it, and they reload.

The bubblewrap sandbox stays off: it needs user, PID and network namespaces, which the PS3's kernel lacks,
and WebKit has no fallback ([BROWSERS.md](../../../docs/research/BROWSERS.md)).
