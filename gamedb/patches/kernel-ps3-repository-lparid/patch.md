+++
component = "kernel-ps3"
patches = ["patches/ps3/0110-lv1call-repo-node-lparid-param.patch", "patches/ps3/0314-repositoy.patch"]
aliases = ["ps3/0110", "ps3/0314"]
category = "port"
switch = "none"
default = "always on"
authors = ["glevand", "psx-place patch set (author not recorded)", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# The repository write calls take an LPAR id

0110 declares lv1's `create_repository_node`, `write_repository_node` and `delete_repository_node`
with one more input each (an LPAR id), as glevand's set uses them. 0314, a later psx-place fix, passes
0 for it at the three call sites in `repository.c`, which 0110 alone left with the old argument count.
One logical change across two files. 0110 is glevand's (refreshed by Sombat Tragoongoson); 0314's author
isn't recorded; both came through Damian Dudycz's ps3 repository.

The calls that change are the write functions, which sit under `CONFIG_PS3_REPOSITORY_WRITE`. That is off
in our config on purpose, so in our kernel only the declarations change. Repository writes are what the
user dropped ps3/0170 for ([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md), decision 1).
