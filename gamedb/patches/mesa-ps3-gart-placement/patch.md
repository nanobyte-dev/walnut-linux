+++
component = "mesa"
patches = ["patches/0059-nv30-on-the-PS3-optional-placement-of-index-buffers-and-fragment-programs-in-GART.patch"]
aliases = ["Mesa 0059"]
category = "performance"
switch = "NV30_PS3_IDX_GART"
default = "off; on on the PS3 since 0069 (programs) and 0070 (index buffers)"
gating = "is_ps3"
forwarded = "no"
tests = ["mesa-placement"]
batches = []
+++

# Index buffers and fragment programs in GART

Two switches, PS3 only, added off by default for a measurement session; since
[mesa-fp-gart-default](../mesa-fp-gart-default/patch.md) (0069) and
[mesa-idx-gart-default](../mesa-idx-gart-default/patch.md) (0070) both are on by default on the PS3,
where ps3rsx takes fragment programs and indices only from GART:

- `NV30_PS3_IDX_GART=1`: a buffer bound as an index buffer (and not as a vertex buffer) is created in GART
  instead of VRAM. The CPU writes it directly; a buffer the GPU still reads is waited for when the CPU maps
  it, never staged and copied into by the GPU (M2MF), so a GART index buffer only ever holds
  CPU-written data (the G3b review's rule D4).
- `NV30_PS3_FP_GART=1`: a fragment program's buffer is a stream-usage (GART) buffer and is not migrated to
  VRAM after its upload. The stream copy used for constant changes is unchanged.

Arms of the Mesa A/B: the switches as environment, `IDX=NV30_PS3_IDX_GART=1`, `FP=NV30_PS3_FP_GART=1` and
both, run through the bench harness's `ENVS` tokens (`NV30_*` tokens go to the game: `ENVS="- NV30_PS3_IDX_GART=1
NV30_PS3_FP_GART=1 NV30_PS3_IDX_GART=1+NV30_PS3_FP_GART=1"`), so one Mesa build serves all four arms of one boot.

## Where the memory comes from (from the code)

- Both kinds of buffer are small and go through nouveau's buffer manager (`nouveau_mm`), power-of-two chunks of
  128 B and up, in slabs: 128/256 B chunks in 4 KiB slabs, 512 B in 8 KiB, 1-2 KiB in 16 KiB, 4-32 KiB in
  128 KiB, 64-128 KiB in 512 KiB, 256 KiB in 1 MiB, 512 KiB in 2 MiB, 1-2 MiB in 4 MiB; over 2 MiB a buffer
  has its own BO. A fragment program is 16 bytes an instruction (64 B to about 2 KiB: slabs of 4-16 KiB), so
  hundreds of programs are a few hundred KiB at most. An index buffer is the game's index data (2 bytes an
  index) rounded up to a power of two, up to 2x: static meshes of tens to hundreds of KiB each, sometimes MiB.
- In VRAM they take the 256 MB of GDDR3 the VRAM swap also uses; in GART they take pages of the RSX's GART
  area. With `ps3rsx_cma=64M` (the boot arguments) that area is a CMA pool: ps3rsx backs each 1 MiB IO page with a
  chunk of it when a buffer object first needs the page and gives the chunk back once the page is idle (kernel
  local/0007, ps3rsx gart.c), so a GART buffer costs ordinary RAM while it is alive, from that 64 MiB, shared
  with textures that went to GART, staging buffers and the push buffers. The IO page granularity means a
  small slab keeps a whole 1 MiB chunk backed while any buffer in it lives.

## Validation

Session 4 (2026-11-06): the Mesa A/B with both switches on against both off, 70 runs (BananaBread, Luanti view120,
OpenArena, Neverball, Trigger Rally, ClassiCube, the X loop): no frame-time regression; memory readings moved a few MB
between VRAM and GART per game with no clear win either way.
