+++
component = "game-data-packager"
patches = ["patches/0006-Accept-Doom-II-1.666-and-Heretic-1.0-as-doom2-wad-an.patch"]
aliases = ["gdp 0006"]
category = "feature"
switch = "none"
default = "always on"
forwarded = "not yet (candidate for Debian)"
tests = ["game-data-packager"]
batches = []
+++
# game-data-packager accepts Doom II 1.666 and Heretic 1.0

Players who own the original boxed games often have these early versions, and gdp marked both unsuitable,
so their owners couldn't package the games at all. dsda-doom loads both IWADs and runs their title screens
and demos (tested on the PS3 with dsda-doom 0.30.0). Both are now the last alternative of the same file, so
the same doom2-wad / heretic-wad package is built and newer releases are still preferred. Hexen 1.0 stays
unsuitable: dsda-hexen refuses it.
