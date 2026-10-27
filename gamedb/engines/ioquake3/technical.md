# ioquake3: technical notes

Our build of Debian's `ioquake3` (`1.36+u20260719.58839361+dfsg-1+ps3.N`,
[packages/ioquake3](packages/ioquake3/)) replaces Debian's and carries 30 patches (there is no 0017).
OpenArena runs on it ([its notes](../../games/openarena/technical.md)), and Quake III Arena with the
player's own data ([its notes](../../games/quake-3-arena/technical.md)). Every change sits
behind a cvar, so a recipe variant can turn it back to upstream's behaviour. The PS3 runs ps3.959~dev (0031) since
2026-10-28; the release candidate is ps3.396 ([the archived release plan](../../../docs/archive/RELEASE-CANDIDATES-2026-10-19.md)).

## renderergl2 memory (0001-0010)

These are the patches that made the GLSL renderer fit in 217 MB of RAM
([the investigation](../../games/openarena/investigations/2026-10-17-opengl2-fits-in-memory.md)).

| Patch | What | Cvar back to upstream |
|---|---|---|
| 0001 | GLSL shader objects deleted once their program links (Mesa keeps no source or IR) | `r_glslKeepShaders 1` |
| 0002 | no GPU bone-animation programs (27 programs index a 105-matrix array, which nv30 turns into compare trees) | `r_glslMaxBones -1` |
| 0003 | none of the 15 shadow and post-processing programs that need framebuffer objects | `r_glslAllPrograms 1` |
| 0004 | compiled programs kept across renderer restarts (no second compile burst at the map load) | `r_glslKeepPrograms 0` |
| 0005 | opt-in: each program built at its first use (`r_glslLazy 1`); it adds stalls when a variant is first drawn | |
| 0006 | the VAO cache allocated only with `r_vaoCache` (21 MB never drawn from) | |
| 0007 | no permanent CPU copy of MD3 vertex frames (about 87 MB of hunk for the bench demo's models) | `r_keepModelVerts 1` |
| 0008 | no tangents in MD3 VBOs without normal mapping | |
| 0009 | `r_md3Lods`, and no LODs that `r_lodbias` never draws | |
| 0010 | `r_gpuVertexAnimation 0` animates MD3 on the CPU as opengl1 does | |

## Engine and opengl1 CPU (0011-0018)

On in every build since ps3.362: no rescan of the shader text after a hash miss (`r_shaderTextScan 1`
for the old way), no crc32 of pk3 entries (`fs_zipCrc 1`), the AltiVec MD3 interpolation and lighting
built and on (`com_altivec 0`), no memsets per decal clip plane (`r_markClear 1`), a player's sounds
loaded at their first play (`s_alLazy 0`), pk3 entries inflated by libdeflate (`fs_zipFast 0`), the
message Huffman trees built from tables (`msg_huffSlow 1`, a command-line cvar). They were never A/B'd
one by one on the PS3; the recipe's `up` variant turns all of them back.

## opengl1 batching (0019-0024)

The lightmap atlas `r_mergeLightmaps` (0019, 0023: values 1 and 2, `r_mergeLightmapsSize`), a GL
state cache `r_glStateCache`, no `glLockArraysEXT` around batches (`r_lockArrays 0`) and the sky box
from vertex arrays `r_skyArrays` (islanddm's sky went from 1453 calls to 18). On llvmpipe at GL 2.1 the
bench demo went from 3022 GL calls and 198 draws a frame to 377 and 79, pixels within 1-4 levels and
identical with every switch off. 0024 makes `r_mergeLightmaps 0` the default, because any atlas page
cost about 10 s of hitches a run on the RSX
([the investigation](../../games/openarena/investigations/2026-10-17-opengl1-batching-and-the-atlas.md)).

## Loads during play (0025-0026)

`r_keepImages 1` keeps the textures of image files across a reload of the same map; `cl_preloadLate 1`
registers again, at the end of the next load of the same map, what the cgame loaded during the last
one's play (bots joining). Both are on by default. `cl_deferPlayers 1` (opt-in) ignores `loaddefered`,
so bots keep a substitute model, which changes the picture
([the investigation](../../games/openarena/investigations/2026-10-17-reload-stalls.md)).

## renderergl2 vertex formats (0027-0029)

| Patch | Cvar (default) | What |
|---|---|---|
| 0027 | `r_vaoSnormColor 1` | vertex colours as GL_SHORT normalized, which nv30 fetches, instead of 16-bit unsigned that u_vbuf converted at every draw (and that made nouveau keep malloc copies of every buffer it read: about 62 MB for the demo's models) |
| 0028 | `r_mipMapPowf 0` | the gamma-correct mipmap bytes from a threshold table instead of a `powf` per texel channel (7.8% of a profile's samples); the same bytes |
| 0029 | `r_md3PackedFrames 0` (opt-in, needs `r_normalMapping 0`) | animated MD3 frames in 12 bytes a vertex instead of 20 (16-bit positions, 8-bit normals): the demo's model VBOs 62.2 to 37.5 MB of VRAM |

[The investigation](../../games/openarena/investigations/2026-10-18-opengl2-freezes.md)
has the measurements.

## Quake III's menus (0030)

| Patch | Cvar (default) | What |
|---|---|---|
| 0030 | `cl_cdkeyCheck 1` | 0: the UI's CD key check answers valid, so Quake III's and Team Arena's menus don't open on the CD KEY screen at every start (`ui_cdkeychecked` is `CVAR_ROM` in both UIs: a value set from outside is forced back to 0). Quake III's autoexec.cfg sets 0; no key is written or read |

## Signals (0031)

| Patch | Cvar (default) | What |
|---|---|---|
| 0031 | `sys_signalQuit 1` | SIGTERM, SIGINT, SIGHUP and SIGQUIT only set a flag, and the main loop quits before its next frame as the `quit` command does (exit status 1, `Client Shutdown (Received signal 15)` as before, the PID file removed); a second one exits at once, and `alarm(10)` ends a quit that doesn't come. Upstream ran the shutdown inside the handler, which hung 4 of 6 SIGTERMs on the PS3 (libxcb's lock, held by the interrupted buffer swap, wanted again by the shutdown's `XGrabPointer`); now 12 of 12 exit in 1-4 s. 0: upstream's handler |

[The investigation](investigations/2026-10-28-sigterm-hang-and-openal-xruns.md) has the stacks.

## Sound dropouts

OpenAL Soft's `[ALSOFT] (EE) mmap commit error: Broken pipe` (and `available update failed: Broken
pipe`) is an audible dropout, 80-440 ms each: OpenAL's mixer thread starved past its 32 ms buffer and
recovered. It runs at normal priority (no RT limit in the session, no RTKit). 150 s with 5 bots: Quake
III 1-2 dropouts a run, OpenArena 1-17; with `LimitRTPRIO=10` (OpenAL takes SCHED_RR by itself) Quake
III had none in 3 runs and OpenArena 3. Proposal, not shipped: an RT limit for the user's session
(system-wide, so every OpenAL game; it needs a suite measurement and the user's ears first).

## Log noise

ioquake3 probes for extensions nv30 doesn't have (`GL_ARB_framebuffer_object`, `texture_float`,
`seamless_cube_map`, `texture_compression_bptc`, a core context) and carries on; there is no IPv6 in the
kernel, so it prints socket errors for it. Builds before 0031 sometimes printed `DOUBLE SIGNAL FAULT:
Received signal 11` right after `Client Shutdown` at the harness's SIGTERM (the shutdown crashing inside
the signal handler), harmless to the numbers. The engine's `analysis.toml` holds the log rules for these.
