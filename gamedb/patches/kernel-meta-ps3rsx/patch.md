+++
component = "kernel"
patches = ["packaging/0003-ps3-meta-depends-on-ps3rsx.patch"]
aliases = ["linux packaging/0003"]
category = "config"
switch = "none"
default = "always on"
forwarded = "not-needed"
tests = []
batches = []
+++

# The kernel meta package depends on that kernel's ps3rsx modules

ps3rsx, the RSX's DRM driver, is built out of tree for each kernel release after the kernel itself, so
for a while the repository's testing suite has a kernel and no GPU module for it. On 2026-10-27 a console
upgraded in that window: `linux-image-ps3` pointed at the new kernel, Petitboot booted it, and X had no
GPU driver. With `ps3rsx-modules-<release>` in the meta package's Depends, apt holds the kernel upgrade
back until the matching module is published and installs the two together.

The other half of the fix is outside the kernel: `ps3-update-kboot` lists kernels without their module
after the others, so Petitboot never boots one first ([PACKAGING.md](../../../docs/PACKAGING.md)).
