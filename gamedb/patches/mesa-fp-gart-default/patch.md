+++
component = "mesa"
patches = ["patches/0069-nv30-on-the-PS3-fragment-programs-are-in-GART-by-default-the-blit-s-program-too.patch"]
aliases = ["Mesa 0069"]
category = "correctness"
switch = "NV30_PS3_FP_GART"
default = "on on the PS3 (NV30_PS3_FP_GART=0 turns it off); the switch does nothing elsewhere"
gating = "is_ps3"
forwarded = "no"
tests = ["mesa-placement:default", "mesa-placement:blit", "mesa-placement:fix-off"]
batches = []
+++

# Fragment programs in GART by default on the PS3

ps3rsx (from the release with the fragment program cache, slice c2) runs fragment programs only from its own checked copies of
programs in GART and refuses a program in VRAM. [mesa-ps3-gart-placement](../mesa-ps3-gart-placement/patch.md) (0059) put
programs in GART behind `NV30_PS3_FP_GART=1`; this makes that the PS3's default. The 3D blit's own program
(`nv30_transfer_rect_fragprog`), which was always moved to VRAM after its upload, follows the same switch. Off the PS3 nothing
changes.

It ships with that ps3rsx: the ps3rsx-modules package `Breaks:` the Mesa before it (a VRAM program would be refused), and
`NV30_PS3_FP_GART=0` is only for a measurement against an older ps3rsx.

## Validation

Host: the default for both machines (`placement-test default`), both moves of a program's buffer to VRAM (the program's and the
blit's) under the switch (`blit`), and a run with the defaults as before that must fail (`fix-off`). The nv30 blit program passes
the kernel's program check (fpcache.h, checked on the host). On the PS3 the session 4 Mesa A/B (2026-11-06) measured the GART
placement with no frame-time or memory cost; the next session's ABBA runs this build as the default.
