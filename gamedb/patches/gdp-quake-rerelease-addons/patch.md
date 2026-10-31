+++
component = "game-data-packager"
patches = ["patches/0015-Add-the-2021-re-release-s-add-ons-Armagon-Dissolutio.patch"]
aliases = ["gdp 0015"]
category = "feature"
switch = "none"
default = "always on"
forwarded = "not yet (candidate for Debian)"
tests = ["game-data-packager"]
batches = []
+++
# game-data-packager: the 2021 re-release's add-ons

GOG's Quake Enhanced installer ships the re-release's own builds of the two mission packs and of Dimensions
of the Past, each with a progs.dat written for the re-release, and the mission packs' music as Ogg Vorbis.
QuakeSpasm plays them from the re-release's base directory with `-game`, so they are expansions of
[gdp-quake-rerelease](../gdp-quake-rerelease/patch.md): quake-rerelease-armagon, -dissolution, -dopa and
-dimension-of-the-machine. The mission pack `*-tryexec.sh` detector now writes only for packages installed in
`$assets/quake`.
