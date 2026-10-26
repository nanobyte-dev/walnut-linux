# Doomsday Engine: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-e-idtech-doom-fs2.md](../../research/gl3-census-e-idtech-doom-fs2.md), "Doomsday 2.3.1 (debian-ports ppc64 has 2.3.1+ds1-2)": GL 3.3 core by default, with a build switch `DE_OPENGL_API` = 3.3 / GLES3 / GLES2 (doomsday/libs/gui/CMakeLists.txt:10-58). A GLES2 build is the cheap route if its GLES2 code still compiles (UNVERIFIED). Low priority: dsda-doom already plays Doom, Heretic and Hexen on the PS3, and Doomsday is Qt-heavy. Scores: popularity 2, novelty 2, playability 2, effort M; ppc64 builds; data free (Freedoom).
- 2026-10-16, [game-candidates.md](../../research/game-candidates.md), "Candidates": Doomsday 2.3.1, Debian `doomsday` (203 MB); builds on ppc64; shaders and Qt5 (UNVERIFIED), tier C; heavy RAM. Verdict: no.
