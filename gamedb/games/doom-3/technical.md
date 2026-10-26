# Doom 3: technical notes

Also known as: dhewm3; RBDOOM-3-BFG (merged here).

## Engine options

- dhewm3 (the pick: GL 1.x + ARB2 programs, SDL2, explicit big-endian build; 2185 stars; Debian contrib, so our build)
- RBDOOM-3-BFG (Debian main `rbdoom3bfg`, a ppc64 build exists, but it renders through NVRHI (Vulkan/D3D12) since 1.4: out; Doom 3 BFG was an official PS3 release anyway)
- id's DOOM-3 and DOOM-3-BFG source releases (frozen dumps, reference)

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Action and shooters": Doom 3 (2004, also Xbox): dhewm3, playable, GL ARB2 paths, own data, effort L (RAM); the pick for its GL 2.x fit.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "dhewm3 (Doom 3 engine)": not strictly a hard no: only the ARB2 path (`ARB_vertex_program` + `ARB_fragment_program`); whether nv30's ARB_fragment_program has enough instruction slots is untested. Debian contrib `dhewm3` 1.5.5+dfsg-2, no ppc64 binary. Big-endian supported upstream (`D3_IS_BIG_ENDIAN`, "Fix bugs on 64bit Big Endian platforms"); BE cost S. RAM estimate 400-700 MB at Low; heavy per-pixel lighting at 1080p: no path to 60 fps. Data retail only (the 2004 demo no longer distributed officially). Effort M. Verdict: NOT WORTH IT (RAM and ARB2 risk, no free data).
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "rbdoom3bfg (RBDOOM-3-BFG)": hard no: NVRHI for DX12 and Vulkan, the GL path is 4.x-era; Debian main 1.4.0+dfsg-9 with a ppc64 build, useless here. Verdict: DROP.
- 2026-10-19, [gl3-census-e-idtech-doom-fs2.md](../../research/gl3-census-e-idtech-doom-fs2.md), "dhewm3 (Doom 3 GPL engine, 1.5.5 / git 455b88e)": not GL3-blocked: GL 1.x fixed function plus ARB_vertex_program / ARB_fragment_program, which nv30 exposes; big-endian a first-class build option. The blocker is memory: the free demo's first map peaked at 1161 MB on the PC. Effort M (our ppc64 build, texture downsizing and compression, memory cuts), playable only if the cuts land. Scores: popularity 4, novelty 3, playability 2; data non-free (free demo 462 MB; retail ~1500 MB).
- 2026-10-19, [gl3-census-e-idtech-doom-fs2.md](../../research/gl3-census-e-idtech-doom-fs2.md), "RBDOOM-3-BFG": out: NVRHI (Vulkan or D3D12), the GL backend removed; scores: popularity 3, novelty 1, playability 1, effort XL.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "The ranked table": dhewm3 rank 9, score 4.5: ~580 MB non-texture plus 474 MB of uncompressed textures; gate: the engine heap.

## Investigations

- [2026-10-19 GL3 census: dhewm3](investigations/2026-10-19-gl3-census.md): the ARB2 paths, the demo's memory on the PC, big-endian, the cuts.
