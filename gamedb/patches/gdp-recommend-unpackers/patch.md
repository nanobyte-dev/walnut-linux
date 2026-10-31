+++
component = "game-data-packager"
patches = ["packaging/0002-Recommend-the-unpackers-for-the-formats-games-ship-i.patch"]
aliases = ["gdp packaging/0002"]
category = "config"
switch = "none"
default = "always on"
forwarded = "not-needed"
tests = []
batches = []
+++
# game-data-packager recommends the unpackers for the formats games ship in

Debian only Suggests them, so apt installs none and game-data-packager stops at the first CD cabinet, GOG
installer or archive. Walnut imports game data from discs, installers and archives, so the common ones come
along: innoextract (GOG's `setup_*.exe`), unshield (InstallShield cabinets, RollerCoaster Tycoon 2's CD),
cabextract, 7zip, lhasa (Quake 1.06's `resource.1`) and unzip. unar stays a Suggests (it pulls in GNUstep),
xorriso too.
