# Hypersomnia: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "hypersomnia": hard no then: GL 3.2 core (GLSL 150), GLES 3.0, or a desktop `#version 130` fallback; no software path. Not in Debian. Verdict: DROP.
- 2026-10-19, [gl3-census-b-2d.md](../../research/gl3-census-b-2d.md), "Hypersomnia (own C++ engine "augs", GLFW + GL 3.2 core, v3.0.0, git 2026-10-18)": the renderer is nearly GL 2 already (VAO + FBO + stencil, 26 shaders of 35 kB, GLSL 130): the GL port is S. What kills it: no BE support, only x86/arm builds with vendored static libs, ~440-490 MB RSS. Effort L overall. Scores: popularity 2, novelty 5, playability 2; data free (CC content, AGPL-3 source), 61 MB.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "The driver-level option: verdict": it fits the driver-level GL 3.3 subset, but its GL port is S anyway; blocked by big-endian serialisation and an x86/arm-only build.
