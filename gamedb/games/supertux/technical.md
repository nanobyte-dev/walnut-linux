# SuperTux: technical notes

## Status

Debian's SuperTux 0.6.3 (`supertux`, `supertux-data`, 158 MB download; the binary is `supertux2`) runs: in the
level at 63-66 fps (2026-10-18). Not played yet; sound not judged by ear.

Smoke-tested: one launch to see whether it starts and plays (`SUITE=0`, no benchmark numbers kept; the smoke runs are described in [SUITE.md](../../../docs/games/SUITE.md#smoke-runs)). The recipe ([benchmark/recipe.sh](benchmark/recipe.sh)) starts "Welcome to Antarctica" directly with
`--renderer opengl20` (fixed function with an FBO) and `--show-fps`. The level's title card waits for a key
with no timeout (`LevelIntro::update` leaves only on Jump, Action, Menu-select, Start or Esc, and
`GameSession::setup` shows it for every level with coins, badguys or secrets). SDL2 ignored the virtual
keyboard's Returns (why is not known), so `during()` presses the keys through XTEST (`smoke_xkeys`: the
window focused, keys held 0.25 s): Return, then Space and Return 8 s apart. Tux then stands still until
played. Variants `worldmap`, `menu`, `card`, `sdl`, `nomusic`.
