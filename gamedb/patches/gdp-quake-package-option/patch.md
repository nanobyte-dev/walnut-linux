+++
component = "game-data-packager"
patches = ["patches/0013-quake-keep-package-given-before-the-game-name.patch"]
aliases = ["gdp 0013"]
category = "correctness"
switch = "none"
default = "always on"
forwarded = "not yet (candidate for Debian)"
tests = ["game-data-packager"]
batches = []
+++
# game-data-packager quake: keep --package given before the game name

`game-data-packager -p quake-rerelease quake SOURCE` built every Quake package it could from SOURCE. The quake
subparser's shortcut options store into the same `packages` as `--package`, and with argparse's default of
None each of them reset what `-p` had put there. They now default to `argparse.SUPPRESS`, as the shared
options do.
