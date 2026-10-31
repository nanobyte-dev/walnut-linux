+++
component = "game-data-packager"
patches = ["patches/0010-Accept-GOG-s-Hexen-installer-setup_hexen_beyond_here.patch"]
aliases = ["gdp 0010"]
category = "feature"
switch = "none"
default = "always on"
forwarded = "not yet (candidate for Debian)"
tests = ["game-data-packager"]
batches = []
+++
# game-data-packager accepts GOG's Hexen installer

GOG's `setup_hexen_beyond_heretic_1.1_(42801).exe` holds HEXEN.WAD 1.1, the CD's data track and the CD music
as Ogg Vorbis, and no Deathkings. gdp didn't know it; it now unpacks it with innoextract for `hexen.wad`, as
it does GOG's Heretic installer.
