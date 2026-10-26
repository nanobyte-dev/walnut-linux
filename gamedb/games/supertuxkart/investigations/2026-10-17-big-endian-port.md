+++
id = "big-endian-port"
games = ["supertuxkart"]
driver_patches = []
game_patches = ["supertuxkart/0001", "supertuxkart/0002", "supertuxkart/0003", "supertuxkart/0004"]
sessions = []
batches = ["2026-10-17-group4-smokes", "2026-10-18-batch8"]
+++

# SuperTuxKart: the big-endian port and the VRAM (2026-10-16 to 10-18)

**Symptom.** The research of 2026-10-16 had two warnings: the FAQ asks for at least 1 GB of RAM against our
217 MB, and Debian had an old powerpc colour bug (#714519). On 2026-10-17 Debian's build stopped while
loading: its mesh loader refuses big-endian (e708512). Our +ps3.264 loaded, the karts rendered, the track
didn't: VRAM exhausted (210 failed buffer allocations), the minimap FBO incomplete, a SIGSEGV at exit.

**Cause and fix.**

- 0001: the .spm mesh and armature loader byte-swaps the little-endian data instead of refusing to run
  (upstream #5578 was closed as not planned). All 1268 .spm files parse identically on amd64 and ppc64.
- 0002: textures and vertex colours in the right channel order on the OpenGL driver (the old powerpc
  colour bug).
- 0003: the legacy renderer no longer crashes when render-to-texture fails, and the minimap's render
  target is power-of-two (512x512).
- 0004: texture compression reaches the legacy renderer: opaque power-of-two textures as DXT1,
  compressed by Mesa at upload.

With 0003-0004 (ps3.306) the VRAM went from 210 to 132 MB (98 MB with `nohd` or `lean`), HUD 13-19 fps.
On 2026-10-18 the track drew (gamedb:batch/2026-10-18-batch8): base 12.6 fps with 13.5 s of stalls,
`lean` 21.4 with 2.4 s.
