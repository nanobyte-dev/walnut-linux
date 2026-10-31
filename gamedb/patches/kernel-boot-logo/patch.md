+++
component = "kernel"
patches = ["patches/local/0008-walnut-boot-logo.patch"]
aliases = ["local/0008", "kernel 0008"]
category = "feature"
switch = "none"
default = "always on"
forwarded = "not-needed"
tests = []
batches = []
+++

# The Walnut boot logo instead of Tux

The console's boot logo (`CONFIG_LOGO_LINUX_CLUT224`) is the Walnut mark from
`ps3-splash/art/walnut-mark.svg`, 110x110 in 224 colours. The Cell's row of SPE logos stays. Our boot
arguments show one logo, centred (`fbcon=logo-pos:center,logo-count:1`).
