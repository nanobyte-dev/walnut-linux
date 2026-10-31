+++
component = "mesa"
patches = ["patches/0019-nouveau-winsys-no-relocations-on-the-PS3.patch"]
aliases = ["Mesa 0019"]
category = "performance"
switch = "NV30_PS3_RELOCS"
default = "off (no relocations on the PS3; 1 sends them as before)"
gating = "is_ps3"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-262", "2026-10-17-mesa-282-armagetron"]
+++

# nouveau/winsys: no relocations on the PS3

Every method that carries a buffer address (vertex buffer offsets, texture offsets and formats, the
fragment program, render targets) is written at the buffer's presumed offset, and validation then appends
two zero words with two relocations: if the kernel finds the buffer moved, it patches them into a
corrected method, otherwise they stay NOPs. Each relocation also counts against the kernel's 1024 per
submission, and a draw that rebinds its arrays adds two per attribute, so a stream of such draws was
flushed for want of relocations about every hundred draws, with all bound state relocated again after.

ps3rsx never moves a buffer object and skips every relocation whose presumed offset is marked valid,
which Mesa always marks, so on the PS3 the zeros always stayed NOPs. Push buffers on the PS3 now send no
relocations: the zero pairs aren't written, and `PUSH_RELOC` returns the presumed value without recording
one. Per relocated method that saves two push words, two 28-byte relocation records, the kernel's copy
and check of both, and the reloc-limit flushes. Every method the GPU sees is the same. Other kernels
(nouveau on the 7600 GT) keep relocations.

This was one of the patches of Mesa ps3.262, which sped up most of the suite by 11 to 24%
(gamedb:batch/2026-10-17-mesa-262). It also removed the mid-frame submissions the reloc limit had forced,
so Armagetron's frame reached the GPU in one piece and lost 13%: with `NV30_PS3_RELOCS=1` it was back at
60.5 fps (gamedb:batch/2026-10-17-mesa-282-armagetron,
[gamedb:armagetronad/submission-overlap](../../games/armagetronad/investigations/2026-10-17-submission-overlap.md)).
[mesa-kick-draws](../mesa-kick-draws/patch.md) (0026) and then
[mesa-kick-auto](../mesa-kick-auto/patch.md) (0027) gave the overlap back without the relocations.
