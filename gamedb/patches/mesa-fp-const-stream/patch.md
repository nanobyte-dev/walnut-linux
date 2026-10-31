+++
component = "mesa"
patches = ["patches/0031-nv30-fragment-program-constant-changes-run-from-a-st.patch"]
aliases = ["Mesa 0031"]
category = "performance"
switch = "NV30_FP_CONST_VRAM"
default = "off on the PS3 (1 restores the VRAM re-upload); on elsewhere"
gating = "is_ps3"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-299"]
+++

# nv30: fragment program constant changes run from a stream copy, not a VRAM re-upload

NV4x fragment programs carry their constants in their code. Every constant change (GLSL uniforms in
OpenArena's opengl2 renderer, Sauerbraten, Luanti; fixed-function fog and texenv colours) uploaded the
whole program again into its VRAM buffer: a transfer, a GART staging allocation, the halfword-swapped
copy, an M2MF copy that the GPU starts only once the 3D work before it is done, and a fence work item to
free the staging. Programs that alternate with different uniforms paid it at every bind. Now a constant
change writes the program, with its new constants, into the stream upload buffer (GART, 256-byte aligned)
and points `FP_ACTIVE_PROGRAM` there, as libgcm's `CELL_GCM_LOCATION_MAIN` programs do. The program runs
from that copy while it stays bound; bound again with unchanged constants it settles back into VRAM with
one upload, so programs with stable constants run from VRAM.

The RSX had never fetched a fragment program from GART before this patch, so the patch header names it as
the first suspect for wrong colours or a graph exception in GLSL games. The check in
gamedb:batch/2026-10-17-mesa-299 showed OpenArena's OpenGL 2 renderer with the right picture and no GPU
hang ([RSX.md](../../../docs/RSX.md#submission-and-fetch-2026-10-17-mesa-ps3262-to-ps3299)); X-Moto ran
about a frame faster with `NV30_FP_CONST_VRAM=1` (46 against 44.5 fps). Out of memory, the upload falls
back as [mesa-out-of-gart](../mesa-out-of-gart/patch.md) (0049) describes.
