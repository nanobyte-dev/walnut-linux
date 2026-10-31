+++
component = "kernel-ps3"
patches = ["patches/ps3/0180-gelic-wireless-print-cmd-status.patch"]
aliases = ["ps3/0180"]
category = "diagnostics"
switch = "none"
default = "always on"
authors = ["glevand", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# gelic wireless names the command that failed

When a wireless command fails to issue, the log line now carries the command number and its status
instead of a bare "cmd issue failed". It comes from glevand's OtherOS++ patch set as Sombat Tragoongoson refreshed it for 6.12 (the psx-place set), taken from Damian Dudycz's ps3 repository. The stock wireless driver is built in (`GELIC_WIRELESS=y`).
[OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) rates it SAFE; it once served the eurus repository patch (ps3/0170), which was dropped on
2026-10-18, and only prints since.
