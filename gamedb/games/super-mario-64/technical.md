# Super Mario 64 (sm64ex): technical notes

Also known as: sm64ex (the PC port of the decompilation); Render96 (its fork with new models, merged here).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-f-engines.md](../../research/gl3-census-f-engines.md), "Scan: other games that need GL 3+ (one line each)": "sm64ex / Render96": `RENDER_API=GL_LEGACY` (GL 1.3) or GL 2.1; big-endian save support in the README; ROM required (non-free assets); ~150 MB. Verdict: GL2 already: strong candidate, the N64 decomp is BE-friendly; needs the user's ROM. The best find of the scan.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "Not GL 3 problems": sm64ex (GL_LEGACY, needs a ROM).
