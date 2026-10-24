# glcount: GL calls per frame and their callers (host-side, LD_PRELOAD)

Used by the game-side batching work (X-Moto, Extreme Tux Racer, dsda-doom, ClassiCube, BananaBread,
2026-10-17) to count a game's GL calls and draws per frame and attribute them to source lines,
before and after a change. This copy comes from the BananaBread (Debian's cube2) work and is a starting point, not
a finished tool: adapt the paths for the next game.

- `gen.py`: from `cnt/names.txt` (one gl* function name per line) writes `cnt/stubs.S`, one
  trampoline per function that logs (function index, caller's return address) and jumps on to the
  real function; `glcount.c` resolves the real functions with dlsym and writes `glc.log`,
  `glc.names`, `glc.frames` (frame boundaries at SwapBuffers) and `glc.marks` (start/end marks a
  game script triggers, e.g. by exec'ing a marker file). Build both into glcount.so; run the game
  with LD_PRELOAD=glcount.so and GLC_OUT=<dir> (GLC_FAKECLOCK=1 makes SDL time advance a fixed
  33 ms a frame, so runs repeat).
- `analyze.py <dir> [top]`: calls and draws per frame between the marks, by function and by caller
  (addr2line on the game binary: set its path, default `src/sauer_client`).
- `trace.py <dir> [frame]`: the calls of one frame in order.

Host setup that matched the PS3: Xvfb, llvmpipe with MESA_GL_VERSION_OVERRIDE=2.1,
MESA_GLSL_VERSION_OVERRIDE=120 and MESA_EXTENSION_OVERRIDE removing what nv30 lacks
(-GL_ARB_framebuffer_object -GL_ARB_texture_float -GL_ARB_texture_rg -GL_ARB_uniform_buffer_object
-GL_ATI_texture_float -GL_EXT_gpu_shader4 ...; docs/glxinfo-nv47.txt has the real list).
