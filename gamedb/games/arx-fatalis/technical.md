# Arx Fatalis (Arx Libertatis): technical notes

Also known as: Arx Libertatis (the engine).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "arx-libertatis (Arx Fatalis)": no hard no: GL 1.5 (or 1.4 + ARB_vertex_buffer_object), GLES 1.x accepted, fixed function, no GLSL. Not in Debian: own build. Big-endian poor: endian helpers in 2 source files only; PakReader, FTL models, DLF/LLF levels and saves are read as raw packed structs; no PPC in `Architecture.h`; BE cost L. RAM estimate 300-700 MB. Data retail; a vendor demo exists (`arx-install-data --install-demo`, about 100 MB, not open-licensed). Effort L. Verdict: NOT WORTH IT for now.
