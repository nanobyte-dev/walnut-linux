# OpenTyrian: technical notes

## Status

Our OpenTyrian 2.1 (`opentyrian`, `2.1.20260912-1+ps3.N`, [packages/opentyrian](packages/opentyrian/)):
Debian's contrib source with one packaging change, the data package only Suggested (a free package never
depends on licensed data). The data is the freeware Tyrian 2.1, our licensed `tyrian-data`, which
`licensed/tyrian-data/tyrian-data.sh` builds from the freeware zip (no package.conf: the script makes the
deb itself); it installs in `/usr/share/games/tyrian`, where the game finds it without options.

Smoke-tested once (gamedb:batch/2026-10-19-ssdroot4-smoke): it runs with sound but stopped in its menu, as
the recipe's keys are a first guess from the source. Not played yet. SDL2 renders the 320x200 8-bit
picture through a software scaler (Scale2x by default) to a texture drawn scaled up to the 1920x1080
desktop-fullscreen window; the game itself needs nothing from OpenGL. The recipe
([benchmark/recipe.sh](benchmark/recipe.sh)) presses Return through the intro, "New Game" and the episode,
difficulty and ship screens (`OT_KEYS`), then fires now and then; `menu` presses nothing.
