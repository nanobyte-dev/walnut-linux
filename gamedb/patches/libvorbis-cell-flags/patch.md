+++
component = "libvorbis"
patches = ["packaging/0001-ppc64-build-for-the-Cell-PPE-O3-mcpu-cell-mtune-cell.patch"]
aliases = ["libvorbis packaging/0001"]
category = "performance"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = ["2026-10-19-ssdroot5-load-libraries"]
+++
# libvorbis built for the Cell PPE

`-O3 -mcpu=cell -mtune=cell` on ppc64: GCC schedules for the PPE's in-order pipeline, and AltiVec is there
for ov_read's conversion ([libvorbis-ov-read-altivec](../libvorbis-ov-read-altivec/patch.md)) and for the
vectorizer. `-O3` is what configure asks for; Debian's `-O2` overrode it. Under qemu: 7% fewer
instructions per decoded second. GCC 16.2's vectorizer at `-O3 -mcpu=cell` has one known miscompile
([PACKAGING.md](../../../docs/PACKAGING.md)); libvorbis's output was checked byte for byte.
