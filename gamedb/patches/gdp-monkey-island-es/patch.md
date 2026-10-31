+++
component = "game-data-packager"
patches = ["patches/0016-Add-secret-of-monkey-island-floppy-es-data-the-Spani.patch"]
aliases = ["gdp 0016"]
category = "feature"
switch = "none"
default = "always on"
forwarded = "not yet (candidate for Debian)"
tests = ["game-data-packager"]
batches = []
+++
# game-data-packager: the Spanish VGA floppy release of The Secret of Monkey Island

ScummVM knows the release; gdp didn't, so a Spanish owner got no package from it. Sizes and checksums come
from an owner's copy; its 901-904.lfl are byte for byte the German floppy's
([engines/scummvm](../../engines/scummvm/technical.md)).
