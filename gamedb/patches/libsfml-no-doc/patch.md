+++
component = "libsfml"
patches = ["packaging/0001-Don-t-build-libsfml-doc-its-doxygen-can-t-be-install.patch"]
aliases = ["libsfml packaging/0001"]
category = "build"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# libsfml without its documentation package

doxygen depends on libfmt10, which the debian-ports snapshot doesn't have, so the build dependencies
couldn't be satisfied. `libsfml-doc` is Architecture: all, so Debian's own copy stays; the libraries are
built unchanged.
