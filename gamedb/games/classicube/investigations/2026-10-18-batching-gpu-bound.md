+++
id = "batching-gpu-bound"
games = ["classicube"]
driver_patches = []
game_patches = ["classicube/0001", "classicube/0002"]
sessions = []
batches = ["2026-10-18-batch8", "2026-10-18-luanti-memory", "2026-10-18-mesa-416-hot-spots", "2026-10-18-batch16"]
+++

# ClassiCube's batching measured slower: GPU-bound (2026-10-18)

Moved on 2026-10-26 from docs/HOTPATHS.md ("ClassiCube's batching measured slower: GPU-bound" and
the `chunks1` rows of "Measured on the PS3"), where the per-draw state cost it shows stays a
cross-game finding (Target 3: the lever is fewer draws in the games).

## Symptom

Our build with fewer GL calls per frame (patches 0001 and 0002, `CC_GL_CHUNKS=2` by default) ran
slower than upstream's drawing (`gloff`, `CC_GL=0`), at half the CPU:

<!-- gamedb:batch-table 2026-10-18-batch8 games=classicube metrics=fps_steady,cpu root=../../.. -->
| Game | Variant | `fps_steady` (fps) | `cpu` (s) |
|---|---|---|---|
| [ClassiCube](../../../games/classicube/) | base@wkkick10 | 47.3 | 83.2 |
| [ClassiCube](../../../games/classicube/) | gloff@wkkick10 | 49.4 | 160.6 |

Numbers: gamedb:batch/2026-10-18-batch8 (batch 8: STK lean, OpenTTD gl, ClassiCube/dsda batching, malloc presets, profiles (Mesa ps3.408, kick), 2026-10-18; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

## Evidence

Profiled again in the same boot (PERF=1, Mesa ps3.408, runs 100447 base and 101011 gloff):

<!-- gamedb:batch-table 2026-10-18-luanti-memory games=classicube metrics=fps_steady,cpu root=../../.. -->
| Game | Variant | `fps_steady` (fps) | `cpu` (s) |
|---|---|---|---|
| [ClassiCube](../../../games/classicube/) | base@wkkick10+perf | 48.4 | 87 |
| [ClassiCube](../../../games/classicube/) | gloff@wkkick10+perf | 50.3 | 163.7 |

Numbers: gamedb:batch/2026-10-18-luanti-memory (Luanti ps3.405 memory variants + ClassiCube PERF (kick, STALLS), 2026-10-18; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

report.py's shares (game 15.7% against 9.5%, Mesa 42.8 against 57.7) read as if the batching moved CPU
from Mesa into the game, but they are shares of very different totals. In CPU time per frame (samples /
499 Hz / frames in the perf window):

| ms of CPU per frame | base (batching) | gloff (upstream) |
|---|---|---|
| all of the game process | **8.5** | **17.1** |
| game (ClassiCube's own code) | 1.33 | 1.62 |
| Mesa | 3.62 | 9.86 |
| ps3rsx and drm (kgpu) | 0.72 | 2.26 |
| kernel | 1.47 | 1.74 |
| st/mesa per-draw state | 1.61 | 4.06 |
| submission ioctl | 0.75 | 2.47 |
| nv30 validation | 0.60 | 2.20 |
| GL API entry | 0.43 | 1.24 |
| nv30 draw emit | 0.33 | 1.22 |
| frame time | 20.7 | 19.9 |

Every class goes down with the batching, the game's own code too (1.33 against 1.62 ms): nothing moved
into the game, Mesa's part shrank by 6.2 ms a frame and the submission and ps3rsx by another 2.2.

## Cause

With the batching the game process is busy 8.5 ms of a 20.7 ms frame and waits the other 12 (its
biggest visible wait is the swap's fence, `dri3_swap_buffers > nouveau_fence_wait`, as in the 004514
profile); without it, busy 17.1 of 19.9. So base is GPU-bound, and the GPU needs longer for base's frame
than for upstream's. What costs the GPU is chunks2 itself: drawing all faces of a chunk part in one draw
also draws the face directions upstream skips on the CPU (faces turned away from the camera), 75% more
indices by the host count (545k against 310k a frame), all transformed by the RSX's vertex units before
it culls them. The pointer and state savings (0001: 2672 calls a frame instead of 5127, the same draws
and indices) cost the GPU nothing.

## Fix

`CC_GL_CHUNKS=1` draws the faces upstream draws with neighbouring ranges merged (~770 draws instead of
1107, no extra indices), keeping 0001's per-call savings:

<!-- gamedb:batch-table 2026-10-18-mesa-416-hot-spots games=classicube variants=base*,chunks1@wkkick10 metrics=fps_steady,cpu root=../../.. -->
| Game | Variant | `fps_steady` (fps) | `cpu` (s) |
|---|---|---|---|
| [ClassiCube](../../../games/classicube/) | base@wkkick10 | 48.4 | 83.7 |
| [ClassiCube](../../../games/classicube/) | chunks1@wkkick10 | 50.9 | 104.4 |

Numbers: gamedb:batch/2026-10-18-mesa-416-hot-spots (Mesa ps3.416~dev hot spots (staging pool, query wait), cube2 355 oqlag, xmoto 362 stream, ClassiCube chunks1 (kick), 2026-10-18; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

<!-- gamedb:batch-table 2026-10-18-batch16 games=classicube metrics=fps_steady,longest_play_ms,cpu root=../../.. -->
| Game | Variant | `fps_steady` (fps) | `longest_play_ms` (ms) | `cpu` (s) |
|---|---|---|---|---|
| [ClassiCube](../../../games/classicube/) | base | 45.9 | 98.9 | 85.7 |
| [ClassiCube](../../../games/classicube/) | chunks1 | 48.1 | 60.5 | 107.8 |

Numbers: gamedb:batch/2026-10-18-batch16 (batch 16: game prefetch A/B, idle fix on/off (433), ClassiCube chunks1, X-Moto stream, Luanti lean parts, 2026-10-18; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

+5% in the first run and +4.7% over three rounds in batch 16 (48.1 against 45.9, longest frame 60.5
against 98.9 ms), at more CPU than chunks2 (107.8 against 85.7 s a run). The recommendation stands to
make it the default; chunks2 stays for a view where the CPU is the limit. The default has not been
changed yet (classicube .374 still defaults to 2).
