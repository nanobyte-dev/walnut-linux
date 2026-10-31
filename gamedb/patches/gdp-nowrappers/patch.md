+++
component = "game-data-packager"
patches = ["packaging/0001-Build-profile-pkg.game-data-packager.nowrappers-the-.patch"]
aliases = ["gdp packaging/0001"]
category = "build"
switch = "pkg.game-data-packager.nowrappers (build profile)"
default = "on in package.conf (NATIVE_PROFILES)"
forwarded = "not-needed"
tests = []
batches = []
+++
# game-data-packager: the tool without the engine wrappers

Walnut ships `game-data-packager` and `game-data-packager-runtime` only. The quake, quake2 and quake3 wrapper
packages and their dedicated servers come from the same source; built here they would carry our +ps3 version
into Walnut's suites and sit beside our own launchers for those engines (QuakeSpasm, Yamagi Quake II). The
build profile leaves them out; without it the source builds what Debian's does.
