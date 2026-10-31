+++
component = "mesa"
patches = ["patches/0033-translate-8-bit-vertex-attributes-to-floats-through-.patch"]
aliases = ["Mesa 0033"]
category = "performance"
switch = "NV30_TRANSLATE_LUT"
default = "on"
gating = "none: core translate code (translate_generic), used by every driver; the output is bit-identical"
forwarded = "no"
tests = []
batches = ["2026-10-18-mesa-407-lut", "2026-10-18-mesa-407-switches"]
+++

# 8-bit vertex attributes to floats through a lookup table

nv30 has no signed 8-bit vertex type, so u_vbuf converts SNORM8 normals to 32-bit floats on every
draw, and `translate_generic` did that with the format's unpack function and an emit function per
vertex and attribute. Every byte went through an integer to float conversion, which on the Cell PPE
passes through memory: a load-hit-store stall per component. A PS3 profile had BananaBread (Debian's
cube2) at 45% of its CPU in that loop and QuakeSpasm at 6%.

Plain 8-bit array formats with identity swizzle (UNORM, SNORM, USCALED, SSCALED, 1 to 4 channels) that
go to floats with as many channels now go through a table per channel. The table holds, for each byte
value, the bits the format's own unpack function returns, so the output is bit-identical (checked on
the host for all 16 formats and every run variant). Measured on the PS3, BananaBread went from 25.9 to
39.6 fps (+53%, gamedb:batch/2026-10-18-mesa-407-lut); QuakeSpasm kept 91 fps with about 3% less CPU
(gamedb:batch/2026-10-18-mesa-407-switches). On by default since Mesa ps3.408.

The investigation is [gamedb:cube2/byte-normals](../../engines/cube2/investigations/2026-10-18-byte-normals.md).
The table made the conversion cheaper but left it on the CPU; [mesa-vtx-snorm8](../mesa-vtx-snorm8/patch.md)
(0043) later moved SNORM8 normalization into the vertex program, behind a switch.
