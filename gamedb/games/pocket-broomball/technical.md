# Pocket Broomball: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-f-engines.md](../../research/gl3-census-f-engines.md), "Godot 3 open-source games": dulvui: Godot 3.2+, AGPL-3; a 2D mobile game.
- 2026-10-19, [gl3-census-f-engines.md](../../research/gl3-census-f-engines.md), "Godot 3 (3.6.2 source; Debian sid source 3.6.2+ds-1, ppc64 binary is old 3.2.3-stable-1+b2)": the engine: Godot 3's GLES2 renderer runs on desktop GL 2.1 with an EXT FBO fallback and software skinning, so it is shaped for nv30; risks: 57 KB uber-shaders, ~150 MB of default 3D buffers, big-endian binary arrays (`BIG_ENDIAN_ENABLED` is defined nowhere in the SCons tree: add `-DBIG_ENDIAN_ENABLED`, S), and Debian ppc64 has only the stale 3.2.3, which won't open 3.4-3.6 projects, so 3.6.2 is ours to build. Engine effort M, then S per game. GDScript games only (Mono is out).
