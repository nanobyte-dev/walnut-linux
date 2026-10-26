+++
id = "water-query-wait"
games = ["bananabread", "sauerbraten"]
driver_patches = ["mesa/0035", "mesa/0042"]
game_patches = ["cube2/0010"]
sessions = []
batches = ["2026-10-18-mesa-416-hot-spots", "2026-10-18-query-wait-repeat", "2026-10-19-ssdroot2-gameplay-profiles", "2026-10-19-ssdroot7-query-kick"]
+++

# The water's occlusion query: the frame waits for the GPU (2026-10-18)

Moved on 2026-10-26 from docs/HOTPATHS.md (Target 5, "BananaBread's 62.8%", the third round and
"Measured on the PS3"). Found in BananaBread's benchmark demo (map cave); the code is the Cube 2
engine's, so cube2 0010 serves Sauerbraten too. The driver side, Mesa 0035 `NV30_QUERY_WAIT` and
0042 `NV30_QUERY_KICK`, is described in [docs/HOTPATHS.md](../../../../docs/HOTPATHS.md#target-5-the-query-result-wait-mesa-0035).

## Symptom

The first profiling batch (2026-10-18, Mesa ps3.299) put 10.3% of BananaBread's CPU in
`nv30_query_result`, Mesa's busy loop waiting for a query result; no other game of the nine had it.
On the SSD root (batch ssdroot2 P1b, Mesa ps3.476, 2026-10-19) it was 17.4% of the game's CPU, the
second biggest hot path after the vertex conversion ([byte-normals](2026-10-18-byte-normals.md)),
and the start-up profiles of batch 17 found the same spin in the warm-up (`nv30_query_result` 13.1 s).

## Evidence

From the second-round profiles (BananaBread 012658, with the game's own frames symbolised and
`stacks.py` for the stacks around a function): all of the 10.3% in `nv30_query_result` comes from one
place, `gl_drawframe > drawreflections > checkquery > glGetQueryObjectuiv`. On the SSD-root profile,
14.3 of the 17.4% is `waterqueryoccluded > checkquery`; 3.1% is `rendergeom`'s queries of the world.

And each of those queries ends with a submission of its own: `nv30_query_end` submits at the end of
every occlusion query, 5.6% of BananaBread's CPU under `nouveau_pushbuf_kick` (the "who submits" table
in HOTPATHS.md), which Mesa 0042 targets.

## Cause

`queryreflections` issues an occlusion query per water surface right after the world geometry; a few
passes later in the same frame `drawreflections` asks for its result (with the default `oqwater 2`,
whenever last frame's query found the water hidden, the usual case on cave), and the GPU, still behind
on the frame, hasn't written it. The world's own queries (rendergeom) are last frame's and never wait
(0.15%). So the engine waits for the GPU in the middle of a frame, and Mesa waits by spinning, taking
issue slots from the PPE's other hardware thread. The GPU is behind there, so the driver can only make
the wait cheaper for the other hardware thread (0035) or the game stop asking (cube2 0010).

## Fix

**cube2 0010 `oqwaterlag 1`** (0, the default: as before). This frame's water query counts only if its
result is in (GL_QUERY_RESULT_AVAILABLE); until then the reflection passes keep last frame's verdict
(skipped while the water stayed hidden) and `renderwater` draws the surface (hidden water shows nothing
through the depth test). The frame no longer stops for the GPU, so CPU and GPU overlap there. Not
pixel-identical in one case: the first frame water shows again after being hidden has the reflection it
had when it was last drawn (one frame, then it is current). The stock engine's `oqwait 0` is the other
way round (every late query counts as visible, so the reflection passes run whenever the GPU is behind:
correct pixels, two extra scene passes): recipe variant `oqnowait`, for comparison. Recipe variant
`oqlag` turns on 0010.

Measured on the PS3:

| Change | Result | Batch | Verdict |
|---|---|---|---|
| Mesa 0035 `NV30_QUERY_WAIT=200` | 45.6 against 25.0 in one run; the interleaved repeat 23.0 / 33.4 against 26.0 / 37.9, CPU lower with it (96-197 against 151-232 s) | gamedb:batch/2026-10-18-mesa-416-hot-spots, gamedb:batch/2026-10-18-query-wait-repeat | no fps gain, stays off; the single run was lucky |
| cube2 0010 `oqwaterlag` | 39.4 steady / 43.5 smooth against base 25.0 / 30.3, one run; `stall_play_s` 8.2 against 8.6 s | gamedb:batch/2026-10-18-mesa-416-hot-spots | promising; the x3 repeat is queued (the keep-or-drop re-test in docs/TESTS.md) |
| Mesa 0042 `NV30_QUERY_KICK=2` | 39.8 against 39.4 unset, 1% low 27.1 against 26.6, CPU 231 s both, x3 | gamedb:batch/2026-10-19-ssdroot7-query-kick | neutral |
| Mesa 0042 `NV30_QUERY_KICK=0` | 39.6, 1% low 27.1, CPU 232 s, x3 | gamedb:batch/2026-10-19-ssdroot7-query-kick | neutral: nothing to offer on BananaBread |

The single-run BananaBread numbers swing a lot on the first-person demo (the repeat's two base runs:
26.0 and 37.9), so small A/Bs there need more rounds. Not picked for Mesa's third round: the query
wait itself, since the GPU is behind there and cube2 0010 is the lever.
