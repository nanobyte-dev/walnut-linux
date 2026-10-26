# The Ur-Quan Masters: technical notes

## Status

Our `uqm` (`0.8.0+dfsg-4+ps3.N`, [packages/uqm](packages/uqm/)): Debian's contrib source with one
packaging change, the content packages only Suggested, so `uqm` installs without them (a free package never
depends on licensed data). The content is Debian's non-free arch-all `uqm-content` and `uqm-music`
(`0.8.0+deb-1`), put unchanged into the licensed suite.

Smoke-tested once (gamedb:batch/2026-10-19-ssdroot4-smoke): it runs with sound (10 underruns). Its window
had a 54 fps HUD median but a steady 6 and 61 s of stalls, not looked into yet. Not played yet. SDL2; the engine's OpenGL path (`--opengl`: one textured quad through
SDL's GL context, fixed function) scales the 320x240 screen to a 1920x1080 fullscreen window. The recipe
([benchmark/recipe.sh](benchmark/recipe.sh)) presses Return through the intro and "Start New Game" and
then now and then (`UQM_KEYS`, a first guess from the source); variants `sdl` (`--nogl`), `window`,
`menu`, `melee`, `nomusic`.
