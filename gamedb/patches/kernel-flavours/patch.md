+++
component = "kernel"
patches = ["packaging/0001-ps3-flavours.patch"]
aliases = ["linux packaging/0001"]
category = "build"
switch = "none"
default = "always on"
forwarded = "not-needed"
tests = []
batches = []
+++

# PS3 kernels only: the ps3 and ps3-debug flavours

Debian's `linux` source builds dozens of kernels; on ppc64 we need two. This packaging patch cuts
`debian/config/ppc64/defines.toml` down to our flavours, `ps3` (the release kernel) and `ps3-debug`,
built with gcc-16 as a plain `vmlinux`, each from the full `.config` that `tools/linux-config.sh` writes
when the source package is made. It adds a `ps3` suite whose ABI suffix `+ps3.<N>` (and `~dev<stamp>`)
`tools/deb-prepare.sh` fills in, drops the initramfs dependency (Petitboot boots
`/boot/vmlinux-<release>` directly), the AppArmor recommendation, the rt featureset and the packages
that should stay Debian's (linux-libc-dev, docs, source, unversioned tools).

The one design point is the release name. A flavour may set `localversion`, which replaces Debian's
`-<flavour>` suffix, while the meta packages keep the flavour's name. So the release kernel is plain
`6.12.111+ps3.<N>` and the debug kernel `6.12.111+ps3.<N>-debug<stamp>`, so every debug build installs
next to the others ([PACKAGING.md](../../../docs/PACKAGING.md)).

It goes with [kernel-image-contents](../kernel-image-contents/patch.md) and
[kernel-meta-ps3rsx](../kernel-meta-ps3rsx/patch.md), the other two packaging patches.
