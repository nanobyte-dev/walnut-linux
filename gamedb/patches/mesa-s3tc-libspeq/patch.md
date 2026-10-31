+++
component = "mesa"
patches = ["patches/0011-s3tc-libspeq.patch", "patches/0032-s3tc-dxt5-libspeq.patch"]
aliases = ["Mesa 0011", "Mesa 0032"]
category = "performance"
switch = "SPEQ_S3TC, SPEQ_S3TC_DXT5 (libspeq's)"
default = "on when libspeq0 is installed"
gating = "none: ppc64 builds only, and only when libspeq.so.0 loads at run time"
forwarded = "no"
tests = ["ps3:gl-dxt"]
batches = ["2026-10-16-speq-ppe-dxt1", "2026-10-16-speq-spu-dxt1"]
+++

# mesa: S3TC (DXT1, then DXT5) compression through libspeq on the PS3

Compressed textures take a quarter or an eighth of the VRAM, which on the PS3 is also the swap tier, but
Mesa's own S3TC encoder on the PPE ran at 2.6 Mpixel/s and added 1.1 to 1.5 s to an OpenArena level load,
with hitches in play. On ppc64 Mesa's texstore now hands RGB(A) uploads to `speq_s3tc_compress_v1` in
libspeq (ps3-spu), which encodes them on the SPEs, or with its own PPE encoder for small images. libspeq
is opened with `dlopen` at the first compression, so Mesa doesn't depend on it; when it is missing or
returns an error, Mesa's encoder runs as before. All policy (image sizes, which unit, `SPEQ_S3TC=off`)
lives in libspeq, so tuning needs no Mesa rebuild ([SPU.md](../../../docs/SPU.md)).

0011 (2026-10-16) did DXT1. 0032 (2026-10-17) routes DXT5 through the same entry point: generic
`GL_COMPRESSED_RGBA` requests (Teeworlds, TORCS) and `RGBA_DXT5`, with a colour block that never relies
on the 3-colour mode, since DXT5's is always decoded in 4-colour mode. An older libspeq answers
`-ENOTSUP` for DXT5 and Mesa encodes it; `SPEQ_S3TC_DXT5=off` does the same on purpose.

Measured on OpenArena: libspeq's PPE encoder brought the load cost from 1.5 s to 0.5 s with no hitch
(gamedb:batch/2026-10-16-speq-ppe-dxt1), and the SPU encoder (1173 Mpixel/s on six SPEs for a 1024x1024
image, byte for byte the reference) made compressed textures load as fast as plain ones while the game's
VRAM fell from 51.7 to 37.5 MB (gamedb:batch/2026-10-16-speq-spu-dxt1).
[mesa-force-dxt](../mesa-force-dxt/patch.md) (0038) uses this hook to compress textures that games
upload plain.
