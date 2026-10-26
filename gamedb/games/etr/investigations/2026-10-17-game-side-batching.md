+++
id = "game-side-batching"
games = ["etr"]
driver_patches = []
game_patches = ["etr/0001", "etr/0002", "etr/0003", "etr/0004", "etr/0005", "etr/0006"]
sessions = []
batches = ["2026-10-16-etr-armagetron-smokes", "2026-10-17-mesa-262", "2026-10-17-xmoto-etr-batching"]
+++

# Extreme Tux Racer: game-side batching (2026-10-17)

**Symptom.** After its first runs (gamedb:batch/2026-10-16-etr-armagetron-smokes), ETR went from 46 fps
HUD on Mesa ps3.246 to 53 on ps3.262 (+15%) and 52 on ps3.299: just short of the 60 fps target, held back
by the PPE.

**Evidence.** A frame made 19,400 GL calls and 697 draws: texture binds repeated, the terrain walked and
drawn in immediate mode, Tux's spheres rebuilt, every tree and item drawn on its own, each snow particle
its own draw.

**Fix.** Patches 0001-0006 (ps3.324), each behind an `ETR_GL_*` switch: a frame went to 850 calls and 72
draws. Same boot, `gloff` against base (gamedb:batch/2026-10-17-xmoto-etr-batching): 52 to 59 fps, CPU 83.6
to 53 s. Run uncapped it stays at 59, so what is left is not a vsync cap.
