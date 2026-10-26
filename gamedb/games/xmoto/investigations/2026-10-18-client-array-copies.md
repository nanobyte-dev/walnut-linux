+++
id = "client-array-copies"
games = ["xmoto"]
driver_patches = []
game_patches = ["xmoto/0001", "xmoto/0004"]
sessions = []
batches = ["2026-10-18-mesa-416-hot-spots", "2026-10-18-batch16"]
+++

# X-Moto's batches copied at every draw (2026-10-18)

Moved on 2026-10-26 from docs/HOTPATHS.md ("X-Moto's client arrays" and the "Measured on the PS3"
rows), where Mesa's side of client vertex arrays stays.

## Symptom

The first profiling batch (2026-10-18, Mesa ps3.299) put 5.5% of X-Moto's CPU (profile 013240, the
credits replay at 154 fps) in `u_upload_data`, Mesa's copy of client vertex arrays: memcpy <
`u_upload_data` 5.3%, the biggest single X-Moto item in the graphics stack.

## Evidence

All of it is xmoto's own batches (patch 0001 batches DrawLib's primitives per state run), drawn from a
client array that Mesa copies at every draw. 3.9 of the 5.5% is the minimap: `renderMiniMap >
drawCircle`, a circle of 8 segments per sprite entity in the minimap's box (933 a frame in the credits
replay, ~17000 batch vertices). The copy goes into GART, which ps3rsx maps cacheable (ordinary RAM,
IOMMU-coherent).

## Fix

**xmoto 0004 `XMOTO_GL_STREAM=1`** (0, the default: as before; recipe variant `stream`). The batch is
written straight into a 2 MiB GL_STREAM_DRAW buffer, mapped with glMapBufferRange (write,
unsynchronized, invalidated range, explicit flush) from a batch's first vertex to its draw, which
sources the buffer at the batch's offset: the vertices are written once, where the GPU reads them. When
the next batch may not fit, the buffer gets new storage (glBufferData NULL), so nothing the GPU may
still read is written again; nothing else reaches the GL while it is mapped (every other call draws what
is pending first). Same vertices, order and draws. In nv30 an unsynchronized map of a GART buffer is the
buffer's own mapping, no staging. The llvmpipe check `gamedb/games/xmoto/packages/xmoto/test/glbatch-test.sh`
draws a random scene through the layer for 400 frames (about 950 batches and one buffer renewal a
frame): the same pixels with and without, no GL errors.

On the PS3, one run each the first time, then x3 in batch 16:

<!-- gamedb:batch-table 2026-10-18-mesa-416-hot-spots games=xmoto metrics=fps_steady,longest_play_ms,cpu root=../../.. -->
| Game | Variant | `fps_steady` (fps) | `longest_play_ms` (ms) | `cpu` (s) |
|---|---|---|---|---|
| [X-Moto](../../../games/xmoto/) | base@wkkick10 | 160.4 | 403.7 | 298 |
| [X-Moto](../../../games/xmoto/) | stream@wkkick10 | 163.1 | 145.8 | 282.2 |

Numbers: gamedb:batch/2026-10-18-mesa-416-hot-spots (Mesa ps3.416~dev hot spots (staging pool, query wait), cube2 355 oqlag, xmoto 362 stream, ClassiCube chunks1 (kick), 2026-10-18; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

<!-- gamedb:batch-table 2026-10-18-batch16 games=xmoto variants=base,stream metrics=fps_steady,longest_play_ms,cpu root=../../.. -->
| Game | Variant | `fps_steady` (fps) | `longest_play_ms` (ms) | `cpu` (s) |
|---|---|---|---|---|
| [X-Moto](../../../games/xmoto/) | base | 160.7 | 42.9 | 299.4 |
| [X-Moto](../../../games/xmoto/) | stream | 162.1 | 380.4 | 290.2 |

Numbers: gamedb:batch/2026-10-18-batch16 (batch 16: game prefetch A/B, idle fix on/off (433), ClassiCube chunks1, X-Moto stream, Luanti lean parts, 2026-10-18; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

A small fps gain (163.1 against 160.4, then 162.1 against 160.7: +1%); the "fewer hitches" of the
first run (longest gameplay frame 146 against 404 ms) did not repeat (380 against 43 ms). It stays off;
it is one of the switch-gated changes in docs/TESTS.md's keep-or-drop re-test. Look for missing or
flickering minimap dots and sprites on the webcam when it runs.
