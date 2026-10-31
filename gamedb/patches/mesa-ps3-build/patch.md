+++
component = "mesa"
patches = ["packaging/0001-ps3-ppc64-drivers.patch"]
aliases = ["Mesa packaging/0001"]
category = "build"
switch = "none"
default = "always on"
gating = "none: a debian/ change that applies to ppc64 builds only"
forwarded = "not-needed"
tests = []
batches = []
+++

# PS3 (ppc64): only the nouveau and softpipe drivers, no LLVM

Debian builds every Gallium and Vulkan driver it can for ppc64, with LLVM behind llvmpipe, radeonsi and
zink. None of them can drive the RSX, and LLVM with z3 put about 200 MB into the image of a machine with
256 MB of RAM. On ppc64 `debian/rules` now builds nouveau (for the RSX) and softpipe (the reference
renderer the GL tests compare against), and nothing else: no LLVM, valgrind, VA/VDPAU, video codecs or
sensors. The code is compiled with `-mcpu=cell -mtune=cell`, and tests are skipped under `nocheck`.

`mesa-libgallium` also `Breaks` the old `ps3-mesa` build, which was installed next to Debian's Mesa in
`/usr/lib/ps3-mesa` before Mesa became a Debian source package (2026-10-16); its empty transitional
version (26.2.4+ps3.99999) replaces it. The VA driver move in the install step is guarded, since no VA
driver is built.

Every other architecture builds what Debian's package builds. The source patches are in the series of
[mesa-ps3-detect](../mesa-ps3-detect/patch.md) onwards.
