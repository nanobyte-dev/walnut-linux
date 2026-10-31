+++
component = "game-data-packager"
patches = ["patches/0019-Accept-a-second-1.09-build-of-Diablo-s-DIABDAT.MPQ.patch"]
aliases = ["gdp 0019"]
category = "feature"
switch = "none"
default = "always on"
forwarded = "not yet (candidate for Debian)"
tests = ["game-data-packager"]
batches = []
+++
# game-data-packager accepts a second 1.09 build of Diablo's DIABDAT.MPQ

gdp knew one DIABDAT.MPQ, GOG's. A player's own 1.09 copy is 40 bytes smaller and was refused. DIABDAT.MPQ is
now a placeholder for two alternatives, as `hexen.wad` is, so diablo-data is built from either.
