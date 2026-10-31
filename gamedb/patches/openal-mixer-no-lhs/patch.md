+++
component = "openal-soft"
patches = ["patches/0002-mixer-no-integer-to-float-round-trips-through-memory.patch"]
aliases = ["openal-soft 0002"]
category = "performance"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = ["2026-10-19-ssdroot5-openal", "2026-10-20-ssdroot10-openal-wedge"]
+++
# OpenAL's mixer without integer-to-float round trips through memory

The PPE has no move between its integer, floating-point and vector registers: a conversion stores the value
and loads it back, and the load waits about 40 cycles for the store. The mixer did that once per sample of
every voice twice over (the resampler's fraction, the 16-bit source sample) and once per output sample,
about half of its estimated PPE cycles under qemu. Now the fraction's float comes from two 256-entry tables
(exact), and 16-bit loads and stereo 16-bit output go through AltiVec eight samples at a time. The tables
apply on any 64-bit PowerPC before POWER8; the AltiVec parts need `-maltivec`.

Bit-identical output under qemu (mono and stereo sources, 22.05 and 44.1 kHz, float and 16-bit output,
spline and linear resampling), the mixer's estimated cycles down 55%. On the PS3,
gamedb:batch/2026-10-19-ssdroot5-openal compared it with Debian's library, and
gamedb:batch/2026-10-20-ssdroot10-openal-wedge checked that our build doesn't make the OpenAL wedge more likely
(the wedge is the kernel's, [kernel-snd-ps3-dma-watchdog](../kernel-snd-ps3-dma-watchdog/patch.md)).
