# TORCS: technical notes

## Status

Debian's TORCS 1.3.7 (`torcs`, `torcs-data`). Smoke run on 2026-10-29 (kernel .874, Mesa ps3.924, the VRAM swap
on; `ps3-bench-20261028T230810Z-smoke-torcs-base`): the window is named after the binary (`/usr/lib/powerpc64-linux-gnu/torcs/torcs-bin`), and Return
three times by XTEST (Race, Quick Race, New Race) starts Quick Race on Forza with four robots. The loading
screen and the race are drawn right (the track, stands, cars, gauges, the HUD; xwd), fullscreen 1920x1080
(`geom fs=1`), with the recipe's texture compression and a 1024 texture limit. But it crawls: about 7 fps on
the Gallium HUD (the game's own counter 6.6), 1% low 0.3, frames up to 6.1 s, 84 MB peak RSS, sound running.
Runs, far too slow to play: a profile first (plib's immediate-mode drawing, five cars' simulation on the PPE).
The recipe: [benchmark/recipe.sh](benchmark/recipe.sh) (variants `notc`, `window`, `single`, `menu`).
