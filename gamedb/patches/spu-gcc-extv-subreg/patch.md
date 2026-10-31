+++
component = "spu-gcc"
patches = ["0003-expmed-no-invalid-subregs-for-extv-insv.patch"]
aliases = ["gcc 0003"]
category = "port"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# GCC expmed: no invalid subregs for ext(z)v/insv on registers

GCC 16's `validate_subreg` rejects size-changing subregs of float modes. The SPU's ext(z)v/insv work on
128-bit TImode, so extracting or storing part of a `__complex__ float` register built `(subreg:TI (reg:SC))`
and ICEd in `gen_rtx_SUBREG`. When the register can't be viewed in the pattern's mode, the shift-based code
runs instead, as these functions already do for SUBREG operands. GCC 9 accepted the subreg.
