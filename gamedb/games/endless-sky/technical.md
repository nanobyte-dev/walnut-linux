# Endless Sky: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-a-space.md](../../research/gl3-census-a-space.md), "Endless Sky (own C++ engine, SDL2 + GLEW, upstream master 2026-10-18; Debian sid/ports 0.11.2-1)": the only game of part A worth porting: upstream ships an "experimental OpenGL 2" path (3D textures instead of texture arrays, no VAO, EXT FBO) that needs GLSL 1.20 shaders as a plugin (effort M); the blocker was memory (1.3 GB RSS on the PC at the lowest texture settings, 477 MB of it textures). Scores: popularity 4, novelty 5, playability 2; ppc64 binary in debian-ports; data free, ~400 MB.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "The top 3 and first steps": rank 3 (the GL2 path): go/no-go on the PC first (what the ~700 MB of non-texture RSS is), then the 3D-texture route against nv30, the 22 shaders at GLSL 1.20, DXT at upload, a PS3 smoke run.

## Investigations

- [2026-10-19 memory and renderer](investigations/2026-10-19-memory-and-renderer.md): the ~700 MB explained (glibc fragmentation of 24 loader threads), the GL2 path and its shaders, the big-endian bugs, and the load-phase OOMs on the PS3 that patches 0009-0012 answered (state of 2026-10-20).
- [2026-10-19 GL3 census: Endless Sky](investigations/2026-10-19-gl3-census.md): GL needs from the source and a trace, the port, memory, CPU, big-endian, data.
