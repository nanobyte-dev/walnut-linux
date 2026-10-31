+++
component = "game-data-packager"
patches = ["patches/0012-Add-quake-rerelease-the-2021-re-release-and-GOG-s-Qu.patch"]
aliases = ["gdp 0012"]
category = "feature"
switch = "none"
default = "always on"
forwarded = "not yet (candidate for Debian)"
tests = ["game-data-packager"]
batches = []
+++
# game-data-packager: quake-rerelease, the 2021 re-release

Nightdive's 2021 re-release ships an `id1/pak0.pak` of its own (the 1.06 paks together with a new progs.dat,
coloured lighting and md5 models) whose QuakeC prints its strings from `localization/loc_english.txt`.
QuakeSpasm 0.94 and later play it from a base directory of its own; the classic engines don't, so it can't
replace quake-registered's files. quake-rerelease installs the pak and the strings into
`$assets/quake-rerelease`, the music as optional files, and GOG's Quake Enhanced installer is unpacked for
them. The add-ons came next: [gdp-quake-rerelease-addons](../gdp-quake-rerelease-addons/patch.md).
