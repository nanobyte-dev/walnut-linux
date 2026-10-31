+++
component = "webkit2gtk"
patches = ["patches/0010-TextureMapper-the-ARGB-to-RGBA-colour-matrix-on-big-.patch"]
aliases = ["webkit2gtk 0010"]
category = "correctness"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# TextureMapper: the ARGB-to-RGBA colour matrix on big-endian

Tiles and images reach TextureMapper as Cairo ARGB32 words, so on big-endian the bytes A, R, G, B are
uploaded as RGBA and the shader's colour matrix has to give (y, z, w, x). The matrix was the transpose of
that (it goes to the shader column by column), so every composited tile came out as (B, A, R, G). The
little-endian BGRA case is its own transpose and was right. Part of the GPU compositing set with
[webkit-compositing-without-webgl](../webkit-compositing-without-webgl/patch.md).
