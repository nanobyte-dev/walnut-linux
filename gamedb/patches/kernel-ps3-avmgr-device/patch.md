+++
component = "kernel-ps3"
patches = ["patches/ps3/0070-ps3avmgr-char-device.patch", "patches/ps3/0420_ps3av_add.patch"]
aliases = ["ps3/0070", "ps3/0420"]
category = "feature"
switch = "none"
default = "always on"
authors = ["glevand", "psx-place patch set (author not recorded)", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# `/dev/ps3avmngr`: raw AV manager packets

0070 adds a misc device to ps3av that reads and writes the AV manager's vuart, so a userspace tool
can send AV manager packets itself. 0420, from the later psx-place fixes, adds the
`#include <linux/fs.h>` that 0070's file operations need on 6.12; the two are one change. 0070 is
glevand's (refreshed for 6.12 by Sombat Tragoongoson), 0420's author isn't recorded; both came through
Damian Dudycz's ps3 repository.

ps3av is built in, so the device exists, but nothing in Walnut uses it. [OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) rates it CAREFUL and
notes it is the one raw manager channel left built in.
