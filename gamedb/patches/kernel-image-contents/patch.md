+++
component = "kernel"
patches = ["packaging/0002-ps3-image-contents.patch"]
aliases = ["linux packaging/0002"]
category = "build"
switch = "none"
default = "always on"
forwarded = "not-needed"
tests = []
batches = []
+++

# The image package: a stripped vmlinux, the real System.map, the boot arguments

Petitboot kexecs `/boot/vmlinux-<release>` on a machine with 256 MB of RAM, so the image is stripped
fully (the symbol table too, as our kernels always were) instead of Debian's `--strip-debug`. Since no
`-dbg` package is built, the image ships the real `System.map` rather than a placeholder. The flavour's
boot arguments (`packages/patched/kernel/linux/flavours/release.args` and `debug.args`) are installed
as `/usr/lib/ps3/boot/<release>.args`, which `ps3-update-kboot` (ps3-boot) puts into the kernel's
Petitboot entry.

Builds on [kernel-flavours](../kernel-flavours/patch.md).
