+++
component = "spu-gcc"
patches = ["0001-spu-transplant-gcc9.5-backend-into-gcc16.2.patch", "0002-spu-port-to-gcc16.2-api.patch"]
aliases = ["gcc 0001", "gcc 0002"]
category = "port"
switch = "none"
default = "always on"
authors = ["GCC's SPU backend as of GCC 9.5 (Free Software Foundation)"]
forwarded = "no"
tests = []
batches = []
+++
# GCC's SPU backend in GCC 16.2

GCC removed the SPU backend in GCC 10. 0001 transplants GCC 9.5's backend (`gcc/config/spu/` and its common
hooks) into the GCC 16.2 tree unchanged, and 0002 ports it to GCC 16.2's internal API (params, hooks, types).
Splitting it this way keeps the transplant reviewable against GCC 9.5 and the port as a diff of its own. The
details and the test results are in [SPU-GCC.md](../../../docs/SPU-GCC.md).
