# The Legend of Zelda: Ocarina of Time (Ship of Harkinian): technical notes

Also known as: Ship of Harkinian (the PC port).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-f-engines.md](../../research/gl3-census-f-engines.md), "Scan: other games that need GL 3+ (one line each)": "Ship of Harkinian / 2Ship2Harkinian": libultraship Fast3D, `gfx_opengl.cpp:306` `#version 130` on Linux (GLSL 1.20-class code), ES `#version 300 es`; 400 MB+. Verdict: near GL2: change to 120; C++20, ImGui, needs a ROM, BE UNVERIFIED.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "Not GL 3 problems": Ship of Harkinian (`#version 130` one line from 120, needs a ROM).
