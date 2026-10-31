+++
component = "badwolf"
patches = ["patches/0005-the-common-browser-shortcuts-Ctrl-w-Alt-Left-Right-C.patch"]
aliases = ["badwolf 0005"]
category = "feature"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# The common browser shortcuts

Someone at a Walnut console has used Firefox or Chrome, not upstream's own scheme: Ctrl-w closes the tab,
Alt-Left and Alt-Right go back and forward (upstream: previous and next tab), Ctrl-Tab changes tabs, Alt-d
and F6 go to the location bar (upstream's Alt-d closed the tab), Alt-Home opens the start page, Ctrl-+ and
Ctrl-- zoom. The man page says so.
