+++
component = "game-data-packager"
patches = ["patches/0008-lha-also-ask-lhasa-for-the-lower-case-names-Quake-s-.patch"]
aliases = ["gdp 0008"]
category = "correctness"
switch = "none"
default = "always on"
forwarded = "not yet (candidate for Debian)"
tests = []
batches = []
+++
# game-data-packager asks lhasa for the lower-case names too

The Quake 1.06 CD's `resource.1` gave neither `id1/pak0.pak` nor `id1/pak1.pak`, so quake-registered couldn't
be made from the CD. gdp asks lha for the names as its data spells them (`ID1/PAK0.PAK`); lhasa, Debian's
lha, lists an MS-DOS archive's names in lower case and extracts only exact matches, so it extracted nothing
and exited 0. Each name is now asked for as it is and in lower case. Tested in the games builder with the
CD's `resource.1`; not in the host tests.
