# Unvanquished: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-d-fps.md](../../research/gl3-census-d-fps.md), "Unvanquished (Daemon engine, git master 2026-10-16; game 0.55.x)": the Daemon renderer officially supports "OpenGL 2.1 with EXT_framebuffer_object and ARB_vertex_array_object", and nv30 has both plus EXT_framebuffer_blit and S3TC; a GLSL 120 path, tiled dynamic lights switch off by themselves. The best novel candidate of part D. Blocked by: the game VM (native only on ppc64, `vm.sgame.type 3`), RAM, CPU, a ~600+ MB dpk data set. Not in Debian (ppc64 listed in its CMake, "POWER5 minimum"). Scores: popularity 3, novelty 5, playability 1, effort L; data free, about 800 MB (UNVERIFIED).
- 2026-10-19, [gl3-census-d-fps.md](../../research/gl3-census-d-fps.md), "Three most important findings": #3: the most promising novel game in part D (effort L), limited by RAM, CPU and the native-only game VM on ppc64.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "The ranked table": rank 12, score 3.0; not measured; heavy RAM (estimate).
