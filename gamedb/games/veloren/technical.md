# Veloren: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "veloren": wgpu renderer: Vulkan, Metal, DX11/12; forcing OpenGL through wgpu still panics on missing features (FreeBSD packaging review); 4 GB RAM minimum listed. Not in Debian. Verdict: DROP.
- 2026-10-19, [gl3-census-f-engines.md](../../research/gl3-census-f-engines.md), "Confirm-and-skip": out: `voxygen/Cargo.toml:69` wgpu 27 (Vulkan/Metal/DX12 plus an experimental GL 3.3+ path); Rust, several GB of RAM.
