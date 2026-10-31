+++
component = "game-data-packager"
patches = ["patches/0017-Add-loom-ega-es-data-the-Spanish-EGA-floppy-release-.patch"]
aliases = ["gdp 0017"]
category = "feature"
switch = "none"
default = "always on"
forwarded = "not yet (candidate for Debian)"
tests = ["game-data-packager"]
batches = []
+++
# game-data-packager: the Spanish EGA floppy release of Loom

ScummVM knows it; gdp didn't. Of its 74 room files, 28 are byte for byte the English, German and French EGA
releases' and keep their names; the 46 with text are new, with sizes and checksums from an owner's copy
([engines/scummvm](../../engines/scummvm/technical.md)).
