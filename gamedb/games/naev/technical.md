# Naev: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-a-space.md](../../research/gl3-census-a-space.md), "Naev (own C engine + a new Rust renderer on glow/naga, SDL3; upstream master 0.14.0-alpha.4, git 2026-10-16; Debian sid 0.8.2)": master requires a GL 3.3 core context (4.6 down to 3.3) with UBOs, instancing, sampler objects, texelFetch, a glTF PBR ship renderer, a Rust toolchain (>= 1.89) and SDL3: XL, a Rust-renderer rewrite rather than a port. Debian 0.8.2 (`naev` on ppc64) is old (GL 3.1 core, GLSL 140, simple 2D) but still 1.7 GB RSS at loading and 365 MB of data; gameplay not measured. Game logic is Lua (LuaJIT interpreter-only on ppc64). Scores: popularity 3, novelty 5, playability 1, effort XL. Skip it.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "The ranked table": rank 17, score 1.8: far over the memory budget; gate: everything.
