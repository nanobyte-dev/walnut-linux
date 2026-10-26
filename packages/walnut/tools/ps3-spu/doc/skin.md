# Vertex skinning on the SPEs (speq_skin_*_v1)

Item 4 of docs/SPU-OFFLOAD-PLAN.md. Sauerbraten on the PS3 skins its skeletal models on the PPE
(our cube2 0002: GPU skinning on nv30 turns the bone lookups into if/else trees, 11 fps against
28). This is libspeq's skinning op, general enough for other engines, and Sauerbraten's hook
(cube2 0006). Built 2026-10-17, not run on the PS3 yet; "estimate" marks numbers nobody measured.

Files: `lib/skin-kern.h` (the kernel, shared by both sides), `lib/skin-job.h` (the job),
`lib/skin.c` (the API and its policy), `spu/skin-op.c` (the op, in the deadline pool's
deadline.spu since 2026-10-17: doc/pool.md), `tools/speq-skin.c` (tests,
the reference, the benchmark), `packages/patched/games/cube2/patches/0006-*.patch` (the hook),
`bench/recipes/sauerbraten.sh` (variants spuskin, ppeskin, offskin).

## 1. How Sauerbraten skins

Line numbers: `engine/skelmodel.h` of our cube2 source up to patch 0005 (Debian
0.0.20201229+dfsg-1).

- **Per model and frame**, `skelmeshgroup::render` (1435): `checkskelcache` (1026) finds or
  makes the skeleton's pose for this animation state; on a miss `interpbones` (872) computes one
  dual quaternion per bone (`sc.bdata`, a few dozen, cheap). With GPU skinning off, every vertex
  buffer is a `vbocacheentry` keyed by that pose (`checkvbocache`, 1414, 16 per mesh group), so
  a model drawn in several passes (shadow map, reflection, glare) is skinned once per frame.
- **Blends.** Vertices with more than one bone use a blend combo (up to 4 bones and weights,
  deduplicated per mesh group). In the CPU path (`genvbo`, 1192) every combo with two or more
  weights gets a slot after the bones (`numgpubones + vblends++`), and `blendbones` (1361) fills
  `bc.bdata` once per pose: the weighted sum of the bones' dual quaternions, sign-corrected
  (`dualquat::accumulate`) and normalized. Single-bone vertices index the bone directly
  (`remapblend`, 1343). So every vertex has one palette index, `vert::interpindex`.
- **The vertex loop**, `skelmesh::interpverts` (344): for each vertex, `b = (index <
  numgpubones ? sc.bdata : bc.bdata - numgpubones)[index]`, then `dst.pos =
  b.transform(src.pos)` (`geom.h` 537: two cross products and a few multiply-adds) and either the
  normal (`b.transformnormal`, a quaternion rotation) or, for bump-mapped models, the tangent
  frame stored as a quaternion: `real * q`, its sign fixed by `fixqtangent` (`animmodel.h` 394,
  the bitangent's sign in w) and packed into 4 shorts (`squat`). Formats: input `vert` (40
  bytes: pos, norm, tc, blend, interpindex) plus `bumpvert` (a float quaternion, 16 bytes);
  output `vvertn` (32 bytes: pos, tc, norm) or `vvertbump` (28 bytes: pos, tc, short4 tangent).
  The texture coordinates are written once by `fillverts` and never touched again.
- **Where the output goes**: `vdata`, one CPU array per mesh group (all its meshes at their
  `voffset`), then `glBufferData(GL_ARRAY_BUFFER, vlen * vertsize, vdata, GL_STREAM_DRAW)`
  (1488) right before the draw. Nothing else reads `vdata`.
- **On the PPE** the bump loop is 94 instructions a vertex (powerpc64 GCC 16, -O2 -mcpu=cell),
  and each of the 4 float-to-short conversions goes `fctiwz`, `stfiwx`, `lwz`: a load-hit-store
  stall each on the Cell (tens of cycles). Estimate: 300-400 cycles a vertex with tangents,
  ~150 with normals.
- **Work per frame on map cave** (bench's sauerbraten recipe, cube2-data 1.3): `-l` starts
  coop edit, the player alone; cave.cfg loads no map models. So `base` skins nothing. Variant
  `gun` draws the grenade launcher: two mesh groups with separate skeletons, the hands
  (`snout_hands.iqm`, 1497 vertices, 33 joints) and the gun (`gl.iqm`, 1683 vertices, 4
  joints), both bump-mapped: 3180 vertices with tangent frames a frame, estimate 0.3-0.4 ms of
  the PPE (1% of a 35 ms frame). Each other player visible would add its model (`snoutx10k.iqm`
  2451 vertices, 75 joints; Frankie 2746) and its weapon (`vwep/*`, 1000-1500 each), ~4000
  vertices.

## 2. Design

- **The op.** One palette entry (or a blend of up to 4) moves each vertex's position, normal
  and tangent. Palettes: dual quaternions (Sauerbraten) or 3x4 matrices; influence: an int32,
  uint16 or uint8 index, or 4 uint8 bones with uint8 weights (blended as Sauerbraten blends dual
  quaternions, matrices summed); tangents: float vec4 (xyz rotated, w kept) or Sauerbraten's
  quaternion frame, out as floats or its shorts. The palette comes in two parts so that
  Sauerbraten's bones and blends need no copy (entries `[0, n0)` from one pointer, the rest from
  the other). The vertex streams (two, any stride) and the output stride and offsets are the
  game's. Every output byte the op doesn't write comes from a template (Sauerbraten passes
  `vdata`, which holds the texture coordinates) or is zero.
- **The batch: libspeq's buffer, not the game's.** A batch is one output buffer (Sauerbraten:
  a mesh group's `vlen * vertsize`) with its meshes at their offsets; the game uploads the
  buffer libspeq hands back instead of its own. The SPEs never write the game's memory: a late
  batch is abandoned to its jobs (its buffer stays theirs until the done word completes) while
  the game skins into `vdata` as before, and a late SPU write can't land in a `vdata` that
  another model's skinning is filling. Four buffers in a ring; `out` stays valid until the next
  begin.
- **Jobs.** A mesh is cut into up to one job per worker, at least 256 vertices each, queued as
  soon as the mesh is added (the next mesh's add overlaps them). A job: the mesh's 128-byte
  descriptor, then the palette (both parts, up to 48 KB: 1536 dual quaternions or 1024
  matrices; the game's blends are a palette, so blends cost the SPU nothing), then the vertices
  in chunks of up to 8 KB per stream, double-buffered (chunk k+1's streams and template land
  while chunk k is skinned and put back). Outputs of neighbouring jobs and meshes share 16-byte
  lines, so a chunk's first and last partial quadwords go out as 4- and 8-byte DMAs of its own
  bytes. Local store: 20 KB code, 100 KB buffers.
- **The kernel** (`skin-kern.h`): one vertex at a time in 4-wide float vectors, GCC's generic
  vectors, the same source for the SPU op, libspeq's PPE path (AltiVec) and the host tests.
  Fields are read and written through aligned quadwords and byte shuffles with precomputed
  masks (the SPU has no unaligned access; on the PPE this also avoids load-hit-store), touching
  only quadwords that hold a byte of the field. Sauerbraten's formulas in its order; the sign
  fix is branch-free. Two formats are compiled with constants (Sauerbraten's), the rest go
  through the general loop. The loop is unrolled twice so two vertices' dependency chains
  interleave. The SPU loop: ~119 instructions a vertex with tangent frames, ~100 with
  normals; estimate 60-75 cycles a vertex on one SPE (0.1 ms for the hud gun on one SPE, half
  on two).
- **The queue: the deadline pool** (doc/pool.md, since 2026-10-17; before it skinning had its
  own queue of two workers). Skinning is short work (tens of us) that the game's frame waits
  on, like audio and unlike DXT (long jobs, load time). Its jobs go to the process's deadline
  pool, two workers running deadline.spu shared with fluidsynth's synth, taken by deadline
  (a mesh's: when end would give up on it), next to DXT's bulk queue of four: 6 contexts on 6
  SPEs with or without music, never time-sliced, and a skin job never waits behind a texture.
  A mesh is cut into jobs of at most 4096 vertices (a running job isn't preempted: the synth
  waits for one at most, estimate 0.1 ms). `SPEQ_SKIN_SPES`: the most of the pool's workers a
  mesh is split across (default all). Workers stay resident 200 ms past their last job (a frame
  is 15-60 ms; a parked worker costs ~90 us to wake) and free their contexts after a minute.
- **The wait point.** The hook submits at the point the engine would skin and waits right after
  the adds, before its upload: Sauerbraten skins a mesh group inside its draw call
  (`skelmeshgroup::render`), with the animation state computed just before. Submitting earlier
  needs a pass over the frame's batched models (`endmodelbatches`, rendermodel.cpp 606) that
  runs the animation part of the render chain without GL; the API already allows it (begin and
  adds early, end at the draw), the hook doesn't do it yet. So the gain is the PPE time of the
  loop minus a round trip: end spins 300 us (`SPEQ_SKIN_SPIN_US`; the jobs take tens of us, a
  futex sleep adds ~62 us), at low SMT priority.
- **Fallback.** libspeq declines (begin returns an error, the engine skins) for batches under
  `SPEQ_SKIN_MIN` vertices in auto (256: a round trip costs about what the PPE spends there),
  when its 4 buffers are all in flight, and for 5 s after three failed or late batches in a row;
  for good once the queue's workers broke. An add outside the formats or limits spoils the
  batch (Sauerbraten then remembers the mesh group and skins it itself from then on). end
  waits `SPEQ_SKIN_TIMEOUT_US` (2000) + 50 ns a vertex, then returns -ETIMEDOUT and the engine
  skins; an SPU error (a fault, an index past the palette) returns -EIO.
- **Modes**: `SPEQ_SKIN=auto|spu|ppe|off` (auto default; ppe runs the same kernel on the
  calling thread into libspeq's buffer: the hook's whole path without SPEs, and libspeq's vector
  code against the engine's scalar loop), `SPEQ_DEBUG=1`: every 10 s and at exit a line
  `speq: skin ...: N batches: N spu (meshes, vertices, jobs, mean and max wait), N ppe, N small,
  N declined; timeouts, errors, meshes refused, with no free buffer, backoffs`.

## 3. API (speq.h)

    int  speq_skin_open_v1(struct speq_skin **s, unsigned abi, unsigned mesh_size);
    int  speq_skin_begin_v1(struct speq_skin *s, uint32_t out_bytes, uint32_t nverts);
    int  speq_skin_add_v1(struct speq_skin *s, const struct speq_skin_mesh *m);
    int  speq_skin_end_v1(struct speq_skin *s, void **out);
    void speq_skin_close_v1(struct speq_skin *s);

`struct speq_skin_mesh` (`SPEQ_SKIN_ABI` 1, 104 bytes on ppc64): nverts, the palette format,
influence and tangent formats, two palette parts, two streams (pointer, stride), the fields
(stream and offset: pos, norm, tan, index, weights), the template, the output offset in the
batch, its stride and field offsets. Offsets and strides multiples of 4, palettes 16-aligned,
the template at the output's 16-byte phase. Sauerbraten's hook copies the struct and the
constants (as fluidsynth's does) and checks nothing else: libspeq refuses a wrong ABI or size
at open.

## 4. Tests (host, 2026-10-17)

`make -C ps3-spu test` runs `speq-skin-fake --check` on the spu path (the skinning op on host
threads, `SPEQ_FAKE=1`, its DMAs checked against the MFC's size and alignment rules), on auto,
and on the ppe path. Against a scalar reference written from Sauerbraten's source
(`interpverts`, `dualquat::transform`, `quat::rotate`, `quat::mul`, `fixqtangent`, `squat`,
`blendbones`) and a general one for the other formats:

- Sauerbraten's two formats with its palette of bones plus normalized blends (4-200 bones, up
  to 1300 blends), mesh groups of 1 to 4 meshes packed as `genvbo` packs them (1683, 1497 + 951
  vertices, tiny meshes, 9000 vertices with a 1500-entry palette), tangent frames of both signs
  and at w = 0: positions and normals equal to the reference on x86 (max relative difference
  0), shorts within 1, every texture coordinate byte the template's.
- General meshes: dual quaternions and matrices, int32/uint16/uint8 indices and 4 weighted
  bones from a second stream, float tangents and Sauerbraten's frames as floats or shorts,
  templates and zeros, five meshes per batch at random 4-byte phases (two of them touching):
  within 3.3e-6 relative, shorts within 1.
- The SPU path equals the PPE path byte for byte (same code, same compiler) in every case.
- Since 2026-10-18 (after the first PS3 run, section 6) floats are compared against the vertex's
  scale, `|got - ref| / (1 + |ref| + |in|)` with the largest components of the output and of the
  input field, within 1e-5 (`FLOAT_TOL` in tools/speq-skin.c; before: per component, `/ (1 +
  |ref_c|)`, within 2e-5), and the fake backend can round the SPU's floats toward zero
  (`SPEQ_FAKE_RZ=1`, as the SPU does): `make test` runs the spu path that way too (max 1.4e-6
  on the scale; to nearest 2.3e-7).
- Refusals (format, tangent frames with matrices, palette alignment and size, strides,
  offsets, template phase, a mesh past the buffer, no position, 40 meshes), an index past the
  palette (-EIO), a faulting job (-EIO, the next batch fine), timeouts (late batches left to
  their jobs, their buffers back afterwards, none short of a buffer), the back-off after three
  failures, auto's minimum, two threads with their own handles (200 batches each).
- `dlopen-check`: the exports, -EPROTO for another ABI, `SPEQ_SKIN=ppe|off` without spufs.
- The same `make test` big-endian, in the ppc64 build chroot under qemu-user (`-O2 -mcpu=cell`:
  the PPE path in AltiVec, the fake SPU path too): all pass; Sauerbraten's formats again equal to
  the reference, the general ones within 5.7e-6. The checks wait up to 20 s per batch (the
  emulated "SPU" is slow; a timeout there would free inputs a late job still reads).

Not covered on the host: the op on an SPU (deadline.spu, built with spu-elf GCC 16.2; first run
on the PS3 2026-10-18, section 6), its speed, DMA on hardware. The SPU's rounding (toward zero)
is emulated since 2026-10-18 (`SPEQ_FAKE_RZ=1`), on x86 hosts (SSE follows the rounding mode;
AltiVec on the ppc64 build host doesn't). The x86 host's benchmark
says nothing about the PS3 (baseline x86-64 has no byte shuffle, so the vector kernel runs
emulated shuffles there; with -march=native it is 2.6-4x faster than the engine's loop).

## 5. On the PS3 (to run)

- `speq-skin --check` (spu path; `--path ppe` too): the same checks on hardware, with the SPU's
  rounding. Then `speq-skin --bench`: us per frame for the hud gun and a player model through
  the engine's loop (compiled the same way), libspeq's PPE path and the SPEs, hot and after
  the workers parked.
- Sauerbraten through bench, interleaved, a few rounds each: `gun` (auto), `spuskin`,
  `ppeskin`, `offskin` (the engine's loop, as before 0006). Watch:
  - the picture (webcam): the hands and gun drawn, animated (idle sway), lit and bump-mapped
    as with `offskin`; a wrong tangent frame shows as inverted or flat lighting on the gun, a
    wrong position as torn or exploded triangles, a stale buffer as a frozen pose;
  - the log's `speq: skin` lines: on `spuskin` batches counted as spu (2 a frame), no timeouts,
    errors or refusals, the mean wait (estimate 30-80 us);
  - fps and the longest frame against `offskin`. The expected gain is small on cave (estimate
    0.3 ms of 35 ms, about 1%): under the run-to-run noise. A map with players or bots would
    show it (each ~4000 vertices); `ppeskin` alone may already take most of it (no
    load-hit-store, AltiVec).
- If something is off: `SPEQ_SKIN=ppe` tells a hook or kernel bug (wrong there too) from an SPU
  bug (right there), `speqskin 0` in the console switches the hook off live.

## 6. On the PS3: libspeq 0.367 (2026-10-18)

`speq-skin --bench`: 42 ns a vertex on the SPEs against 97 on libspeq's PPE path, no errors.
`SPEQ_DEBUG=1 speq-skin --check`: 8 failures, all from the check's float tolerance, none from
the op or the queue:

- **Seven "max rel" lines at 2.0-2.6e-5** over the 2e-5 limit (Sauerbraten's normals with 120
  and 1300 blends, three general dual quaternion meshes; bad bytes 0, shorts within 1). The
  SPU's single precision always rounds toward zero: each operation errs by up to a whole ulp
  instead of half, always the same way, so a vertex's errors add up instead of averaging out.
  The check measured per component, `/ (1 + |ref_c|)`, and a component that comes out of a
  cancellation (near 0, from terms of |v| + |translation|, here up to ~150) carries those
  terms' error. Host emulation (the fake SPU path with `SPEQ_FAKE_RZ=1`, with and without fused
  multiply-adds) reproduced it: 2.0-2.7e-5 on the same kinds of meshes, and to nearest (x86,
  qemu) under 1e-5. A study of the kernel against a double reference, 300k vertices per format,
  in u = 2^-23 (one ulp of 1.0 toward zero): per component dual quaternion positions 71-82 u to
  nearest, 212-250 u toward zero (2.5-3e-5); against the vertex's scale at most 4.3 u to nearest,
  6.5 u toward zero, matrices and the rotations of normals and tangents less. Not a regression:
  `lib/skin-kern.h` is unchanged since 0.318's skin.spu, which never ran on the PS3, and the
  errors are what the arithmetic gives: 0.004 units on terms of 150, so a model of that size
  filling a 1080-line screen moves by 0.03 pixels. The kernel stays as is (a
  more accurate SPU formula would need double precision, far slower there); the check now
  measures against the vertex's scale within 1e-5 (section 4).
- **"two threads, 200 batches each" failed for the same reason**: its stats lines show one
  thread's handle with 200 batches and the other's with 1, both on the SPEs with no error: that
  thread's first batch was compared with the same 2e-5 limit, missed it, and the thread stopped.
  The threads drew their meshes from one unsynchronised random generator, so which one hit a
  bad vertex was luck. Now each thread has its own sequence (a per-thread seed) and a failure
  says which thread, which batch and why.
- The rest of the log is the check working as meant. `a batch failed on the SPEs: -5`: the
  index past the palette (-EIO expected). `worker N died: event 0x40 ... held 222/223`: the
  forced fault (`spq_skin_fault`, a stream EA of 0x1000), event 0x40 a data storage fault,
  each worker holding one of the mesh's two jobs (the pool's 222nd and 223rd), failed with
  -EFAULT (-14), the batch -EIO; 242/243 and the two after are the three faults of the back-off
  case, then `3 batches in a row failed ... 5000 ms` and one `declined` begin, as the check
  asks. The closing stats add up: 57 batches = 32 mesh cases + 10 refusal cases (the index
  one included) + the 40-mesh batch + 1 good + 1 faulting + 1 good + 2 late (the 2 `timeouts`:
  the timeout-0 case) + 6 good + 3 faulting; 10 meshes refused = 9 refusal cases + the
  40-mesh `-ENOSPC`; 5 errors = the index + 4 faults. The auto handle's one batch, right after
  the back-off's faults, waited 3963 us: a died worker makes its context again on the next job
  (~3.4 ms); the first handle's max, 6212 us, is a batch that waited for contexts too (the
  pool's first, or the first after the forced fault).

