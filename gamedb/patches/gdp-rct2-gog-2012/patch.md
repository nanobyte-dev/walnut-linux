+++
component = "game-data-packager"
patches = ["patches/0009-Accept-the-2012-build-of-GOG-s-setup_rollercoaster_t.patch"]
aliases = ["gdp 0009"]
category = "feature"
switch = "none"
default = "always on"
forwarded = "not yet (candidate for Debian)"
tests = ["game-data-packager"]
batches = []
+++
# game-data-packager accepts the 2012 build of GOG's RollerCoaster Tycoon 2 installer

GOG has shipped two installers named `setup_rollercoaster_tycoon2_2.0.0.6.exe`, of different sizes. Both hold
the same Triple Thrill Pack game files (all 2454 match), so the second one is an alternative with the same
provides.
