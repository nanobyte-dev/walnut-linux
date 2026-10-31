+++
component = "openal-soft"
patches = ["packaging/0001-ppc64-build-for-the-Cell-PPE-mcpu-cell-mtune-cell.patch"]
aliases = ["openal-soft packaging/0001"]
category = "performance"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = ["2026-10-19-ssdroot5-openal"]
+++
# OpenAL Soft built for the Cell PPE

`-mcpu=cell -mtune=cell` on ppc64, the way our Mesa is built: GCC schedules for the PPE's in-order pipeline,
and AltiVec is available to the mixer paths of [openal-mixer-no-lhs](../openal-mixer-no-lhs/patch.md).
