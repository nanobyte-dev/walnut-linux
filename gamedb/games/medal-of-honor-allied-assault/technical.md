# Medal of Honor: Allied Assault (OpenMoHAA): technical notes

Also known as: OpenMoHAA (the engine).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "openmohaa (Medal of Honor: Allied Assault)": no hard no: CMake `BUILD_RENDERER_GL1` ON (fixed function, ioq3 lineage). Debian contrib `openmohaa` 0.82.1+dfsg-4, no ppc64 binary. Big-endian: "Improved support for big-endian architectures", PowerPC64 binaries shipped as experimental; nobody reported a working Linux ppc64 BE client (MorphOS is the tested PPC port); BE cost M (TIKI/skeletal animation). RAM estimate 300-600 MB. Data retail only. Effort M. Verdict: NOT WORTH IT now.
- 2026-10-19, [gl3-census-d-fps.md](../../research/gl3-census-d-fps.md), "OpenMoHAA (Debian contrib) and ET: Legacy": ioquake3 renderer family, not GL3-blocked (UNVERIFIED by source); non-free data; scores: popularity 2, novelty 4, playability 2, effort M.
