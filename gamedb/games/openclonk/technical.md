# OpenClonk: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-b-2d.md](../../research/gl3-census-b-2d.md), "OpenClonk (own C++ engine, SDL2 + epoxy, master 2026-04-28 (SDL only), Debian 8.1-5)": GL 3.2 core only (no fallback), but the shaders are almost GLSL 1.20 as written (sampler2DArray in 2 landscape shaders, int-indexed 8-bone skinning): GL port M. The blocker is memory (742 MB RSS in a 64x40 sandbox) and CPU for the landscape shader. Builds on ppc64 (`openclonk` 8.1-5+b1). Scores: popularity 3, novelty 5, playability 1, effort L; data free (CC-BY), ~110 MB.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "The ranked table": rank 13, score 3.0: ~350 MB non-texture plus 253 MB of textures; needs texture arrays, which the driver subset can't emulate exactly.
