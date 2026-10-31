+++
component = "game-data-packager"
patches = ["patches/0011-Accept-the-Macintosh-Hexen-CD-s-Hexen.WAD-as-hexen.w.patch"]
aliases = ["gdp 0011"]
category = "feature"
switch = "none"
default = "always on"
forwarded = "not yet (candidate for Debian)"
tests = ["game-data-packager"]
batches = []
+++
# game-data-packager accepts the Macintosh Hexen CD's Hexen.WAD

The Macintosh CD's Hexen.WAD is the 1.1 game plus Macintosh fonts and menu art, without the GENMIDI and DMXGUS
lumps of the DOS sound driver. It is now an alternative of `hexen.wad?mac`. Engines that need GENMIDI for
their OPL player stop at start with this WAD (dsda-doom did before its own patch); those that play MIDI
through another device are fine.
