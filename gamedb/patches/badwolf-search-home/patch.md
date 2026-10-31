+++
component = "badwolf"
patches = ["patches/0004-search-from-the-location-bar-a-start-page-Reload-and.patch"]
aliases = ["badwolf 0004"]
category = "feature"
switch = "WALNUT_BROWSER_HOME"
default = "the packaged start page"
forwarded = "no"
tests = []
batches = []
+++
# Search from the location bar, a start page, Reload and Home buttons

What a desktop browser is expected to have. Words typed in the location bar become a DuckDuckGo search (its
HTML results need no scripts). The start page is the one the package ships (`WALNUT_BROWSER_HOME` overrides
it), opened at start, in a new tab and by the new Home button. A Reload button sits next to back and forward,
and the window starts maximized, since on a TV the browser is the whole screen. `uri_test` covers the search
cases.
