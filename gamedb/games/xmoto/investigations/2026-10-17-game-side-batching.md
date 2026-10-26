+++
id = "game-side-batching"
games = ["xmoto"]
driver_patches = []
game_patches = ["xmoto/0001", "xmoto/0002"]
sessions = []
batches = ["2026-10-17-mesa-246", "2026-10-17-mesa-262", "2026-10-17-mesa-299", "2026-10-17-mesa-282-ab", "2026-10-17-mesa-282-kick100", "2026-10-17-xmoto-etr-batching"]
+++

# X-Moto: game-side batching beat the driver rounds (2026-10-17)

**Symptom.** X-Moto, a 2D game, played at 35-47 fps: a GL 1.x game held back by the PPE. The Mesa rounds
moved it a little each: ps3.246 35 to 37 (HUD median, underruns 208 to 126), ps3.262 39 to 47 (+21%,
gamedb:batch/2026-10-17-mesa-262), and ps3.299 cost 5% against ps3.282 in the same boot (44.5 against 47.0;
neither 0027 nor 0030 off restored it, 0031 off and 0028 off gave 46 each,
gamedb:batch/2026-10-17-mesa-282-ab). A fixed `kick100` cost 9% (gamedb:batch/2026-10-17-mesa-282-kick100).

**Evidence.** The host count: 18,500 GL calls and 1,960 draws a frame. DrawLib set its GL state for every
primitive and drew the level's blocks and edges one by one.

**Fix.** 0001 sets DrawLib's state only when drawing and batches its primitives per state run; 0002 draws
the level's blocks and edges from one buffer: 165 calls and 40 draws a frame, the picture identical. On
the RSX: 44.5 to 166.5 fps (`nobatch` against base, same boot, gamedb:batch/2026-10-17-xmoto-etr-batching),
3.7x. The lesson for every game: fewer GL calls in the game beat driver work, where the PPE is the limit.
