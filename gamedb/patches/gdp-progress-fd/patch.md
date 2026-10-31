+++
component = "game-data-packager"
patches = ["patches/0003-Add-progress-fd-FD-machine-readable-progress-for-a-c.patch"]
aliases = ["gdp 0003"]
category = "feature"
switch = "--progress-fd FD"
default = "off"
forwarded = "no"
tests = ["game-data-packager"]
batches = []
+++
# game-data-packager --progress-fd: machine-readable progress for a calling program

Walnut Games runs game-data-packager for the player and shows what it is doing (PLAN 5.10). The only
progress was a percentage drawn on a terminal, and every failure was exit status 1 with English text. With
`--progress-fd FD`, one line per event goes to FD: `step`, `file`, `generated`, and once before a failed run
`error <kind>`, the kind decided where gdp detects the failure (`not-found`, `unknown-version`, `unsuitable`,
`needs-download`, `no-space`, `failed`). Without the option the output is unchanged
([PACKAGING.md](../../../docs/PACKAGING.md)). The test suite covers the writer, whole runs and every error kind
it can make without game data.
