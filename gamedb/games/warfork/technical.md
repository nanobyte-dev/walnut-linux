# Warfork: technical notes

Also known as: Warsow (its predecessor, on the same qfusion engine).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-d-fps.md](../../research/gl3-census-d-fps.md), "Warfork / Warsow (qfusion, TeamForbiddenLLC/warfork-qfusion master 2025-05-24)": `ref_gl` builds GLSL 120/130/140 (or ES 300/310) programs and treats texture arrays, gpu_shader5 etc. as optional: it should run on GL 2.1, not GL3-blocked. A second renderer `ref_nri` (Vulkan/D3D12) is out of scope. 1.6 GB of assets with an "absolute spaghetti of different licenses"; no Debian package; ppc64 build is ours. Effort M. Scores: popularity 4, novelty 5, playability 2.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "The ranked table": rank 5, score 5.5; not measured; gate: RAM and the unclear asset licence.
- 2026-10-16, [game-candidates.md](../../research/game-candidates.md), "Candidates": "Warsow / Warfork": not in Debian; GL needs UNVERIFIED (likely GL 3). Verdict: out.
