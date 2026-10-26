# Red Eclipse: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-d-fps.md](../../research/gl3-census-d-fps.md), "Red Eclipse 2 (Tesseract-derived engine, redeclipse/base master 2.0.9)": Red Eclipse 2 is XL: a deferred-only renderer (G-buffer with 4 MRT, shadow atlas, radiance-hint GI, volumetrics, SMAA), no forward path. It accepts GL 2.0 contexts and EXT_framebuffer_object and nv30 meets its hard limits, so it may even start, but every light is a fullscreen-class pass over a 1080p G-buffer, far beyond the RSX. Debian's 1.6 (cube2) is the right Red Eclipse for the PS3. Scores: popularity 3, novelty 5, playability on 256 MB 1.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "The ranked table": Red Eclipse 2: out (fill rate); 1.6 already runs.
