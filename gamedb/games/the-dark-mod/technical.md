# The Dark Mod: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "The Dark Mod": 2.08 moved all GLSL to 3.3 core; minimums 2 GB RAM, 30 GB disk (Linux Links). Not in Debian (only the map editor plugin). Verdict: DROP.
- 2026-10-19, [gl3-census-e-idtech-doom-fs2.md](../../research/gl3-census-e-idtech-doom-fs2.md), "The Dark Mod (2.13-era source)": out: `GLimp_CheckRequiredFeatures` demands GL_VERSION_3_3 and S3TC (qgl.cpp:71-80); GLSL 330+ with uniform blocks; several GB of data, 4 GB RAM minimum (UNVERIFIED); the last GL 2-class release (pre-2.06) would still need 1+ GB. Scores: popularity 3, novelty 5, playability 1, effort XL; data free (CC-BY-NC-SA), ~5000 MB.
