# Chromium B.S.U.: technical notes

## Status

Debian's Chromium B.S.U. (`chromium-bsu`, `-data`, 1 MB) is installed on the PS3 and has a smoke recipe, but it
is menu only (Enter on Start Game; the arrows or the mouse move, Space fires), so it waits for the user at the
controller; no run yet. The recipe ([benchmark/recipe.sh](benchmark/recipe.sh)): medium graphics, OpenAL, its
own fps counter on. Variants `full`, `min`, `noblend`, `nosound`.
