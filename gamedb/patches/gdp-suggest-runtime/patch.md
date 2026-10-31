+++
component = "game-data-packager"
patches = ["packaging/0005-Suggest-game-data-packager-runtime-instead-of-recomm.patch"]
aliases = ["gdp packaging/0005"]
category = "config"
switch = "none"
default = "always on"
forwarded = "not-needed"
tests = []
batches = []
+++
# game-data-packager suggests its runtime instead of recommending it

The runtime is the GTK launcher the generated game packages use, and it Depends on gir1.2-gtk-4.0 and
friends: far too heavy for the 256 MB PS3, and Walnut doesn't use it. The command-line tool imports nothing
from it. The runtime is still built.
