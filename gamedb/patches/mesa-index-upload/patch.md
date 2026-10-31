+++
component = "mesa"
patches = ["patches/0030-nv30-indices-the-CPU-holds-go-to-the-GPU-through-the.patch"]
aliases = ["Mesa 0030"]
category = "performance"
switch = "NV30_INDEX_UPLOAD"
default = "32 on the PS3 (indices per draw from which they are uploaded; 0 restores the inline pushes), 0 elsewhere"
gating = "is_ps3"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-299"]
+++

# nv30: indices the CPU holds go to the GPU through the upload buffer

Index data the GPU can't fetch from an index buffer was packed into the push buffer word by word:
`glDrawElements` from client memory (OpenArena's vanilla renderer, every surface: 32-bit indices) and
8-bit indices from a buffer object (the RSX's index fetch has 16- and 32-bit formats only). Every index
then crossed the CPU twice more: packed by Mesa and copied by ps3rsx into its ring. From
`NV30_INDEX_UPLOAD` indices up (32 on the PS3) they are now copied once into the stream upload buffer
(GART, persistently mapped, so `u_upload_alloc` is a pointer bump), 8-bit ones widened to 16 bits, and the
GPU fetches them like an index buffer: `IDXBUF_OFFSET`, `IDXBUF_FORMAT` with the GART location and
`VB_INDEX_BATCH`, a dozen words whatever the count. Shorter arrays stay inline. The upload buffer is
referenced before validation, fenced like [mesa-index-buffer-fence](../mesa-index-buffer-fence/patch.md)
(0028) does for index buffers. Restart indices are unchanged by the widening.

Part of Mesa ps3.299 (gamedb:batch/2026-10-17-mesa-299); switched off there, X-Moto didn't get back its
5% (45.0 against 44.5 fps).
