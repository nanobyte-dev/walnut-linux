+++
component = "badwolf"
patches = ["patches/0003-JavaScript-on-by-default-a-Walnut-token-in-the-User-.patch"]
aliases = ["badwolf 0003"]
category = "config"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# JavaScript on, a Walnut token in the User-Agent, no autoplay

Walnut's browser is the console's modern browser, so scripts run from the start (upstream starts with them
off; the JS button still turns them off per tab). WebKit's User-Agent gets "Walnut" at the end, so the
GameDB site can tell a Walnut console and show its "Open in Walnut Games" link. Media never autoplay: video
costs the PPE most of its time and starts only on a click.
