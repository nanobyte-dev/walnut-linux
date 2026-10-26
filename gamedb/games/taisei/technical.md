# Taisei: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-b-2d.md](../../research/gl3-census-b-2d.md), "Taisei (own C engine on SDL3, master 2026-10-15, Debian 1.4.4+dfsg-3)": GL needs moderate (instancing the hard one), no GLES2/GL2 backend any more, so a port means a new backend or Mesa-side instancing; memory is the real problem (550 MB RSS in stage 1 at 540p). Builds on ppc64 (debian-ports `taisei` 1.4.4+dfsg-3) and is endian-aware. Effort L, PS3 playability poor; Debian's package has no music or sfx. Scores: popularity 3, novelty 5, playability 2; data free, 175 MB.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "The driver-level option: verdict": the only census game that fits the driver-level GL 3.3 subset (core 3.3, 17 shaders `#version 330 core`, instanced sprites, R8 textures, 4 draw buffers, derivatives; no UBO/GS/TF/arrays). Revisit the subset if Taisei becomes a target: items 1, 2, 5, 6, 11 of G-driver.md first (gate, int maths, float targets, instancing loop, ARB_fbo), about 5 weeks.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "The ranked table": rank 11, score 3.3: 562 MB at 540p (stage 1), ~240 MB upper bound on the PS3 with 221 MB of textures; gate: RAM and instancing.

## Investigations

- [2026-10-19 GL3 census: Taisei](investigations/2026-10-19-gl3-census.md): its GL 3.3 needs, a stage trace, memory, CPU.
