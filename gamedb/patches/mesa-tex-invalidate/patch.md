+++
component = "mesa"
patches = ["patches/0020-nv30-invalidate-the-texture-cache-only-after-texture.patch"]
aliases = ["Mesa 0020", "Mesa 0021", "Mesa 0022", "Mesa 0023", "Mesa 0024", "Mesa 0025"]
category = "performance"
switch = "NV30_TEX_INVALIDATE_ALWAYS"
default = "off (invalidate only after texture memory may have changed; 1: before every draw)"
gating = "none: every nv30; on other kernels it relies on the same events, and ps3rsx adds its own invalidate at channel switches"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-262", "2026-10-17-mesa-262-repeats"]
+++

# nv30: less work per draw

Six changes to what nv30 sends and does in every draw, each leaving out GPU writes that change nothing
or PPE work whose result is the same. They went in together with Mesa ps3.262
(gamedb:batch/2026-10-17-mesa-262): every suite game 11-24% faster except Armagetron, whose loss came
from [mesa-no-relocations](../mesa-no-relocations/patch.md) (0019). They were six patches, 0020 to 0025,
until they were folded on 2026-10-31; the switches that brought back each old path went with the fold,
except the texture cache's.

- **Texture cache.** `nv30_state_validate` ended every draw with the texture cache invalidate the NVIDIA
  blob was traced doing (`TEX_CACHE_CTL` 2, then 1, then three `0x1718` NOPs): ten push words, ten more
  for the kernel to copy and check, and a flush of the GPU's texture caches before each draw. libgcm
  leaves the invalidate to the application, after it changed texels. The screen now counts the events
  after which texture memory may hold something the cache doesn't (`tex_gen`, bumped atomically by any
  context): a copy into or out of a texture (uploads, readbacks, the 3D blit), a new framebuffer, and a
  flush. A context emits the same sequence as before, at the same place, before its first draw after the
  count moved. ps3rsx already invalidates both caches whenever another channel ran, so other processes'
  rendering and uploads are covered. `NV30_TEX_INVALIDATE_ALWAYS=1` restores the invalidate before every
  draw, to tell a stale texture from another bug. Armagetron's 13% loss on ps3.262 was first suspected
  here; it ran 52-53 fps with or without this part (gamedb:batch/2026-10-17-mesa-262-repeats).
- **Vertex cache.** `nv30_state_validate` invalidates the vertex cache (`0x1710`) in every draw, after the
  last write any vertex or index buffer of the draw could have had. `nv30_draw_vbo` then emitted another
  one right before the primitive whenever a buffer had been written since the last draw, or a bound
  buffer was mapped coherently, and for the latter walked the vertex buffers every draw. Nothing writes
  vertex memory between the two, so the second invalidated an empty cache; it is dropped, with the walk.
  Two push words saved in most draws of GL 1.x games. A wrong call here would show as stale or corrupt
  vertices.
- **Vertex formats.** `nv30_vbo_validate` wrote the whole VTXFMT burst whenever the vertex buffers or
  elements changed. The formats depend only on the vertex elements and on whether vertices are pushed
  inline, not on the buffers, and the state tracker rebinds buffers far more often than it changes the
  layout (u_vbuf binds a new upload buffer for user arrays in every draw). The context now keeps the
  VTXFMT words the GPU holds (`state.vtxfmt`, carried over a context switch with the rest of the state)
  and leaves the burst out when it would write the same words. ps3rsx shadows VTXFMT and restores it
  after another channel ran. Forgetting the cached formats when the context that drew last is destroyed
  (commit 5fe2290) is part of [mesa-ctx-state-reset](../mesa-ctx-state-reset/patch.md) (0029) now.
- **Constants.** The state tracker hands nv30 its shader constants as user memory, again whenever they
  change: in fixed-function GL about every draw. `nv30_set_constant_buffer` wrapped each pointer in a new
  `nv04_resource` and freed the previous one, an allocation and a free on the PPE per update. Only the
  wrapper's data pointer and size are ever read, and nothing but the context's slot references it, so
  while the slot still holds the wrapper the last call made, and nobody else holds it, the wrapper is
  pointed at the new memory instead. A real constant buffer takes the old path. Constants are uploaded
  exactly as before.
- **Texture units.** The state tracker passes all its sampler views, and binds all its samplers, when any
  of them changes. nv30 marked every unit dirty and rewrote each one: 13 words and two buffer references
  per unit, for units that kept what they had. A unit is now marked only when its view or its sampler is
  a different object; the context holds a reference to each bound view, so an equal pointer is the same
  texture. This also fixes a latent bug: a switch between two contexts of a process on the same channel
  marked everything dirty but validated only the units changed since the context last drew, so a context
  could sample the other one's textures. It now marks all of the context's units.
- **Vertex program constants.** `nv30_vertprog_validate` sent every changed constant on its own:
  `VP_UPLOAD_CONST_ID`, the slot and four words. The GPU moves to the next slot after every four words of
  a burst (libgcm uploads up to eight constants per burst, and ps3rsx's shadow follows the same rule), so
  a run of changed constants in consecutive slots now goes up as one burst of at most four. Which
  constants are uploaded, and with what values, is unchanged; a matrix costs six words less.
