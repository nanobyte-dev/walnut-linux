+++
component = "mesa"
patches = ["patches/0044-vbo-index-array-min-max-in-AltiVec-NV30_MINMAX_VMX.patch"]
aliases = ["Mesa 0044"]
category = "performance"
switch = "NV30_MINMAX_VMX"
default = "off"
gating = "switch NV30_MINMAX_VMX"
forwarded = "no"
tests = ["mesa-minmax-vmx"]
batches = ["2026-10-19-ssdroot7-minmax-vmx", "2026-10-20-ssdroot9-minmax-vmx", "2026-10-20-ssdroot11-minmax-vmx"]
+++

# Index array min/max in AltiVec

A draw from client vertex arrays needs the range of vertices its indices use, so Mesa scans the index
array of every such `glDrawElements` (`vbo_get_minmax_index_mapped`). SuperTuxKart's legacy renderer
draws its meshes from client arrays, and the scan was 6.5% of its CPU in the profile of 2026-10-19; on
the in-order Cell PPE the scalar loop is a load, two compares and two branches per index.

With `NV30_MINMAX_VMX=1` (PowerPC with AltiVec only) arrays of 64 or more 16- or 32-bit indices are
scanned eight or four at a time with GCC's vector types (`vbo_minmax_vmx.h`, `target("altivec")`). The
unaligned head and the tail stay scalar, and short arrays stay scalar altogether because taking the
lanes apart at the end goes through memory. Primitive restart indices are masked out. The scan is
checked against the scalar loops on 2.7 million cases, on the host and as ppc64 code under qemu
(`mesa-minmax-vmx`).

Measured: SuperTuxKart 20 to 22 fps (+10%) at n=5 with fewer stalls and less CPU, a candidate default;
neutral on BananaBread and OpenArena, whose draws scan few client index arrays
(gamedb:batch/2026-10-19-ssdroot7-minmax-vmx, gamedb:batch/2026-10-20-ssdroot9-minmax-vmx,
gamedb:batch/2026-10-20-ssdroot11-minmax-vmx). Still off by default.
