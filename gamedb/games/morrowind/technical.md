# The Elder Scrolls III: Morrowind (OpenMW): technical notes

Also known as: OpenMW (the engine).

## Engine options

- OpenMW 0.51 (Debian `openmw` 0.51.0-1+b4 on ppc64): the only engine looked at; TES3MP is its multiplayer fork, behind OpenMW (pc-classics.md).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "RPG": Morrowind (2002, also on Xbox): OpenMW, playable, OSG near GL 2.1, own data, effort L (RAM).
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "Ranked table": OpenMW 0.51: no hard no: shaders are `#version 120` compatibility profile; a ppc64 binary exists (0.51.0-1+b4, 53 deps incl. GDAL/HDF5, 282 MB). Data: Morrowind (retail, GOG/Steam), no free set. RAM not measurable without data; estimate 600 MB-1.2 GB mid-game; the in-order PPE is the larger problem. Effort M (config). Verdict: try now if the user owns Morrowind, otherwise skip.
- 2026-10-19, [gl3-census-f-engines.md](../../research/gl3-census-f-engines.md), "OpenMW (OpenSceneGraph, 0.51.0, git 2026-10-18 master)": already GL 2.1 / GLSL 1.20 by default, not GL3-blocked. Real work is big-endian (the ESM and BSA readers do no byte swapping, so Morrowind.esm can't be read on ppc64 today; M), then CPU and memory. Scores: popularity 5, novelty 5, playability 2, effort L; data non-free (Morrowind ~1 GB; the CC0 Template+Example Suite is 160 MB).
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "The ranked table": rank 10, score 4.0; PC 456 MB at the menu with no cell loaded; PS3 500-800 MB after cuts (estimate); gate: the big-endian loaders and RAM.

## Investigations

- [2026-10-19 GL3 census: OpenMW](investigations/2026-10-19-gl3-census.md): GL needs, the byte order of the ESM/BSA readers, memory, CPU.
