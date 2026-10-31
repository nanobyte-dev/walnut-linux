+++
component = "badwolf"
patches = ["patches/0006-links-in-other-schemes-go-to-the-desktop-s-handler-w.patch"]
aliases = ["badwolf 0006"]
category = "feature"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# Links in other schemes go to the desktop's handler

WebKit loads only the web's own schemes, so a link to `walnut://game/<id>` (the GameDB site's "Open in Walnut
Games") or `mailto:` failed with "unsupported URL". Such a navigation is now handed to the desktop's default
handler for `x-scheme-handler/<scheme>` through GIO, as xdg-open does, and the page stays where it is.
