+++
component = "libpng"
patches = ["patches/0001-ppc64-unfilter-rows-in-AltiVec-on-the-Cell-PPE-Up-an.patch"]
aliases = ["libpng 0001"]
category = "performance"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = ["2026-10-19-ssdroot6-libpng"]
+++
# libpng: row unfilters in AltiVec on the Cell PPE

The generic C unfilters read back `row[i-bpp]`, a byte stored a few instructions before: on the PPE every
such load waits about 40 cycles for the store, 97% of a PNG decode's load-hit-stores with zlib-ng in.
These keep the left pixel in registers and work 16 bytes at a time in AltiVec (Up, and Sub, Avg and Paeth
for 3- and 4-byte pixels), with the same output: tested against the generic C on synthetic and real images
and by libpng's own tests. Only compiled for AltiVec without VSX (`-mcpu=cell`).

SuperTuxKart's load profile had 4 s of PNG decoding; gamedb:batch/2026-10-19-ssdroot6-libpng measured load
times against Debian's library ([PROFILING.md](../../../docs/PROFILING.md)).
