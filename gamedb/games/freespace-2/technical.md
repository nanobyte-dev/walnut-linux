# FreeSpace 2 (FreeSpace Open): technical notes

Also known as: FreeSpace Open / fs2_open (the engine).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Flight and space": FreeSpace 1/2 (1998/99): FreeSpace Open, playable, modern GL, own data, the FS2 demo free, effort L; 495 stars, the only engine.
- 2026-10-20, the user, "the rule of 2026-10-20 (no API veto)": GL 3 and Vulkan are not out when memory use is decent: an open renderer gets ported to GL 2 with effects dropped; the 2026-10-19 drop of GL 3-only engines no longer holds.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "FreeSpace Open (fs2_open)": hard no then: `gropengl.cpp:53` "minimum GL / GLES version we can reliably support is 3.2", GLSL 150, plus a Vulkan renderer; no fixed-function or software fallback. Not in Debian (only `freespace2-launcher-wxlauncher` in contrib). Verdict: DROP (hard no).
- 2026-10-19, [gl3-census-e-idtech-doom-fs2.md](../../research/gl3-census-e-idtech-doom-fs2.md), "FreeSpace 2 Open (fs2open master 2026-10-18)": GL3-blocked hard: GL 3.2 core and GLSL 150, every texture a `sampler2DArray`, uniforms in uniform blocks: a port of master is XL. The realistic route is an old release with the GL 2 / fixed-function renderer (3.7.4, 2016) and retail or freeware data: M, modern mods lost. Scores: popularity 4, novelty 5, playability 3 (retail data, old engine), effort XL (master) / M (3.7.4); data retail non-free; free total conversions (The Babylon Project, Wing Commander Saga, Diaspora); 600-2000 MB.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "The ranked table": rank 6, score 5.5: an old release (3.7.4, GL 1.2 + optional GLSL, UNVERIFIED); master XL (arrays everywhere, UBOs); PS3 200-350 MB with retail data (estimate); gate: data and the old release's byte order.

## Investigations

- [2026-10-19 GL3 census: FreeSpace 2 Open](investigations/2026-10-19-gl3-census.md): the renderer's GL 3.2 needs, the 3.7.4 route, memory, byte order.
