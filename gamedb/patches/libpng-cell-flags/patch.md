+++
component = "libpng"
patches = ["packaging/0002-ppc64-build-for-the-Cell-PPE-O3-mcpu-cell-mtune-cell.patch"]
aliases = ["libpng packaging/0002"]
category = "performance"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = ["2026-10-19-ssdroot6-libpng"]
+++
# libpng built for the Cell PPE

`-O3 -mcpu=cell -mtune=cell` on ppc64, as Mesa and libvorbis are built: GCC schedules for the PPE's
in-order pipeline, and AltiVec is there for the unfilters of
[libpng-altivec-unfilter](../libpng-altivec-unfilter/patch.md) and for the vectorizer.
