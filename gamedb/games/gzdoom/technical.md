# GZDoom: technical notes

Also known as: Raze (the Build-engine games on the same backend: see gamedb:duke-nukem-3d).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-e-idtech-doom-fs2.md](../../research/gl3-census-e-idtech-doom-fs2.md), "GZDoom 4.14 (and Raze, same backend)": not GL3-blocked: a GLES 2 backend with an explicit desktop-GL-below-3.3 mode and GLSL 100 shaders; one texture-format fix (GL_RED, patchable) and our ppc64 build. The cost is the CPU: ZScript runs in an interpreter off x86_64. Not in Debian. Scores: popularity 5, novelty 2, playability 4 (vanilla maps), effort M; data free (Freedoom), ~30 MB engine + IWAD. Popular because of its mods, but Doom itself is already on the PS3 (dsda-doom here, PrBoom on GameOS).
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "The ranked table / Runners-up": rank 4, score 5.5: not measured (the run stalled at start); 120-250 MB on the PS3 (estimate); BE paths bit-rotted (estimate); gate: ZScript interpreted, BE fixes. The first runner-up: fits memory, popular for mods, low novelty.
- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": listed as probably out of reach: GL 3.x+ only.

## Investigations

- [2026-10-19 GL3 census: GZDoom](investigations/2026-10-19-gl3-census.md): the GLES backend's desktop mode, the GL_RED fix, memory, ZScript on the PPE, big-endian.
