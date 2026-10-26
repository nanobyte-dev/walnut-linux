# TORCS: technical notes

## Status

Debian's TORCS 1.3.7 (`torcs`, `torcs-data`, 326 MB) is installed on the PS3 and has a smoke recipe, but it is
menu only (`-r` is the console mode, no window): Race > Quick Race > New Race with the arrow keys, so it waits
for the user at the controller; no run yet (`SMOKE_SECS=300 BENCH_TIMEOUT=420 ./run.sh smoke torcs`).

setup() ([benchmark/recipe.sh](benchmark/recipe.sh)) enables the game's "texture compression ARB" option in
`config/graph.xml` (plib uploads `GL_COMPRESSED_RGB(A)_ARB`, which Mesa makes DXT1/DXT5) and a 1024 texture
size limit; the key names come from the binary's strings and are unverified. Fullscreen goes through GLUT game
mode and XRandR. Risks: plib's state changes and big textures, the simulation and 4 robots on the PPE.
Variants `notc`, `window`, `single`.
