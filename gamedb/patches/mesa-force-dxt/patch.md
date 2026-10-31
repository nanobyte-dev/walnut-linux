+++
component = "mesa"
patches = ["patches/0038-mesa-texture-compression-at-upload-for-every-game-NV.patch"]
aliases = ["Mesa 0038"]
category = "performance"
switch = "NV30_FORCE_DXT"
default = "off"
gating = "switch NV30_FORCE_DXT"
forwarded = "no"
tests = ["mesa-forcedxt"]
batches = ["2026-10-19-ssdroot2-force-dxt", "2026-10-20-ssdroot8-force-dxt"]
+++

# Texture compression at upload for every game

Most games upload plain RGB(A)8 textures, which cost 4 bytes a texel of VRAM on the PS3 (RGB is stored
as X8R8G8B8), and VRAM is also the swap tier. `NV30_FORCE_DXT=opaque` stores a qualifying
`glTexImage2D` of level 0 as DXT1 when it has no alpha (an RGB format, or every alpha byte 255): an
eighth of the VRAM and of the upload. `NV30_FORCE_DXT=all` also stores images with alpha as DXT5. The
texture gets a DXT format while its internal format stays the game's, and Mesa's S3TC texstore
compresses it, through libspeq's SPE encoders on the PS3 ([mesa-s3tc-libspeq](../mesa-s3tc-libspeq/patch.md)).

The design point is that the texture keeps behaving as the game uploaded it. Sub-image updates on whole
4x4 blocks are compressed again; any other update, a copy or a framebuffer attachment first turns every
compressed level back into the format the game asked for; mipmap generation takes Mesa's compressed
path; and the queries report the game's internal format. Many textures are skipped on purpose:
textures filled later, small ones, NPOT, `GL_NEAREST` magnification (pixel art), cube maps and others.
The decision is plain C in `texforcedxt_policy.h`, tested on the host (`mesa-forcedxt`). It is core
Mesa, so softpipe takes the same paths.

Measured: BananaBread's peak VRAM fell from 119.8 to 73-78 MB with `opaque` at the same frame rate
(gamedb:batch/2026-10-19-ssdroot2-force-dxt); SuperTuxKart with `all` from 95.5 to 58.1 MiB, the other
games compressed nothing (gamedb:batch/2026-10-20-ssdroot8-force-dxt). Off by default; a game that
looks right with it gets the switch in its own launcher
([MEMORY.md](../../../docs/MEMORY.md#texture-compression-at-upload-mesa-0038-nv30_force_dxt)).
