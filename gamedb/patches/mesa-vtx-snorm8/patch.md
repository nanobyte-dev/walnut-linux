+++
component = "mesa"
patches = ["patches/0043-nv30-signed-8-bit-vertex-attributes-normalized-in-th.patch"]
aliases = ["Mesa 0043"]
category = "performance"
switch = "NV30_VTX_SNORM8"
default = "off"
gating = "switch NV30_VTX_SNORM8"
forwarded = "no"
tests = ["mesa-vtx-snorm8"]
batches = []
+++

# Signed 8-bit vertex attributes normalized in the vertex program

NV3x, NV4x and the RSX have no signed 8-bit vertex type, so u_vbuf converts `GL_BYTE` normalized
attributes to floats on the CPU at every draw, over the draw's whole vertex range. Even after
[mesa-translate-lut](../mesa-translate-lut/patch.md) (0033) that was 18.1% of Sauerbraten's CPU (its
world's normals and tangents), and the CPU reads also gave every VRAM buffer a system memory copy.

With `NV30_VTX_SNORM8=1` nv30 reports R8_SNORM to R8G8B8A8_SNORM as vertex formats. The GPU fetches the
bytes as U8_USCALED (libgcm's UB256: each byte as a float 0..255), and four instructions at the start of
the vertex program give what util_format's unpack gives, `MAX2(-1, c / 127)`. The design point is the
fetch size: it is always four bytes, and for a format of fewer components a fifth instruction puts
(0, 0, 0, 1) in the rest, so the bytes after the attribute never reach the program. An attribute with
less than four bytes of room in its vertex is converted on the CPU as before. The inputs it applies to
are a key in the vertex elements state, part of [mesa-shader-cache](../mesa-shader-cache/patch.md)'s key.
Checked on the host bit for bit (`mesa-vtx-snorm8`: the arithmetic against util_format, and
`nvfx_vertprog.c`'s output on an NV40 vertex program interpreter).

History: the first version (Mesa ps3.545~dev) fetched three-component normals as UB256 with 3
components, and its first run with the switch on wedged the RSX with graph exception 0x106 2.2 s into
BananaBread's demo on 2026-10-19 (`docs/dumps/exception-0x106-ub256-snorm8.txt`,
[RSX.md](../../../docs/RSX.md#errors-and-what-wedges-the-gpu)). Only UB256 x4 is safe; the patch was
reworked in place to always fetch four bytes (ps3.570~dev), and ps3rsx refuses other sizes since then
([ps3rsx-validator-ub256](../ps3rsx-validator-ub256/patch.md)). The fixed version has not run on the PS3
yet (the re-test is in [TESTS.md](../../../docs/TESTS.md)). Background:
[gamedb:cube2/byte-normals](../../engines/cube2/investigations/2026-10-18-byte-normals.md) and
[HOTPATHS.md](../../../docs/HOTPATHS.md#snorm8-attributes-in-the-vertex-program-mesa-0043).
