+++
component = "libvorbis"
patches = ["patches/0002-vorbisfile-ov_read-s-16-bit-packing-in-AltiVec-on-th.patch"]
aliases = ["libvorbis 0002"]
category = "performance"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = ["2026-10-19-ssdroot5-load-libraries"]
+++
# ov_read's 16-bit packing in AltiVec

ov_read rounds every sample with floor() and converts it to an integer. On the PPE both go through memory
(it has no moves between its float and integer registers): two load-hit-stores of about 40 cycles a
sample, a third of a whole Vorbis decode's estimated cycles under qemu. Mono and stereo now pack eight
samples a step in vector registers with the scalar code's rounding and clamping bit for bit; blocks of 512
samples go through an aligned buffer, so neither the caller's buffer nor the PCM has to be aligned.

Byte-identical oggdec output for a stereo music track and 12 mono effects; 37% fewer estimated PPE cycles
than the unpatched build with the same flags, 41% with the Cell flags against Debian's build. On the PS3,
gamedb:batch/2026-10-19-ssdroot5-load-libraries compared load times against Debian's library on SuperTuxKart
and BananaBread ([SPU-AUDIO.md](../../../docs/SPU-AUDIO.md)).
