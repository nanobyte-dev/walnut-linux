# Plan: Mesa nv30 on the PS3 RSX, up to glxgears in Xorg

## Context

Phases 0–1 are done: our Debian kernel boots over NFS and the RSX 3D engine works from Linux.
Today's probing (docs/RSX.md) established:
- A second lv1 GPU context works on ps3fb's memory handle (hardware channel 1). A second VRAM
  allocation does not.
- The IO window is 256 MB, mapped in 1 MB pages.
- The object handles are fixed by the hypervisor.
- The CPU reaches VRAM very slowly.
- **Any graph exception wedges the whole GPU until a console reboot.**

Research showed that Mesa (nv30 is still in main) now talks to the kernel only through nouveau's
ABI16 and NVIF ioctls, about 10 of them, and picks its own object handles.

User decisions:
- The handle translation goes into a small Mesa patch.
- This plan goes all the way to glxgears in Xorg.

Goal: a render-only + KMS DRM driver `ps3rsx` that registers as "nouveau", plus patched
Mesa, so that stock nv30 renders on the RSX.

**Scope of this round: only Phase 0, the hardware derisking tests (E1–E16).** Phases 1–5 below
are a draft. We refine them with the user once Phase 0's results are in docs/RSX.md.

How Phase 0 runs:
- Tests are kernel-module parameters in `gpu/kmod/rsx_ctx_probe2.c`, or userspace tools in
  `gpu/tools/` (`rsx-tri.c`, `rsx-tex.c`) on the ps3fb FIFO. The current kernel
  (`local/0003` export) is enough, so no rebuild is needed.
- One test per insmod or run. Tests that can wedge go last, one per boot.
- E16 (netconsole) comes first. The fix may need a cmdline/config change, which is
  `config.mk` only.
- The user brings the PS3 to Petitboot (fan on SYSCON first). I never reboot it or change its
  boot flag myself.
- Commit the tools, and update docs/RSX.md after each batch of results.

Test-loop constraints:
- A reboot needs the user (XMB → set the OtherOS flag → Petitboot → `make netboot`), so
  iteration happens through loadable modules, and kernel changes are batched into as few
  rebuilds as possible.
- No kexec.
- The webMAN fan setting must be SYSCON before every OtherOS session.

## Phase 0 – Remaining hardware questions (module only, no reboot unless something wedges)

`gpu/kmod/rsx_ctx_probe2.c` runs one test per `insmod` (module parameter), so a wedge tells
us which test caused it:
- **E1** How many contexts can be allocated (decides one context per channel or one shared).
- **E2** Scattered 1 MB iomaps, unmap and remap (how the GART pool grows).
- **E3** FIFO `CALL` into another chunk, then `RETURN` (the pushbuf model). *May wedge.*
- **E4** Write a pre-swizzled value with the 3D back-end release (0x1d70); expect it to come
  out correct (the Mesa fence fix). *May wedge.*
- **E5** Two contexts alternating 3D clears with different state (is 3D state per channel?).
  *May wedge.*
- **E6** `lv1_gpu_display_flip` from context 1 to a red VRAM area (webcam check): which
  context can flip.
- **E7** Do vsync and exception IRQs reach context 1's own IRQ outlet?
- **E8** VRAM mapped write-combined vs guarded: write speed.

Further derisking tests. Run these before any driver code, ordered by how much the design
depends on them:
- **E9** iomap a chunk at IO 64 MB, then M2MF to and from it through 0xfeed0001. This checks
  that the ctxdma covers IO addresses mapped after the context was created, which the GART
  pool assumes.
- **E10** M2MF at VRAM offsets near 224 MB and near 254 MB (`memory_size` = 0xfe00000):
  how far 0xfeed0000 reaches.
- **E11** A large 3D clear, then compare `ref` with the VRAM contents at once. Does
  SET_REFERENCE imply rendering is done? If not, the kernel's fences use a back-end release.
- **E12** A raw-command triangle, `gpu/tools/rsx-tri.c` on the ps3fb FIFO:
  - vertex program upload;
  - fragment program fetch, with Mesa's halfword swap on big-endian;
  - inline vertex data.

  *May wedge.*
- **E13** A raw textured quad: byte order of ARGB textures and render targets. *May wedge.*
- **E14** Where the 0x66604200 notifier writes, using M2MF with notify on.
- **E15** Mesa's nv30 init methods:
  - offline, check `nv30_screen.c` init and the methods Mesa uses against RPCS3's RSX method
    table;
  - then replay the init list in small batches on hardware. *May wedge.*
- **E16** Fix netconsole (no Linux output has ever reached `make listen`). This must be done
  before any test that can wedge.

Results go into docs/RSX.md. Order: E16 first, then the tests that can't wedge (E1, E2, E6,
E7, E8, E9, E10, E14), then the ones that can, one per boot.

## Phase 1 – One batched kernel rebuild (one reboot)

**Config** (`kernel/config/ps3.config`):
- `CONFIG_DRM=m`, `CONFIG_DRM_VKMS=m` (selects the KMS helper, shmem, sync_file and dma-buf).
- `CONFIG_DEBUG_FS=y`, `CONFIG_ZRAM=m`.

**Rootfs:** blacklist `ps3vram`.

**ps3fb patch:** replace `local/0003` with `local/0004-ps3fb-gpu-sharing-api.patch`, which
only adds exports to `drivers/video/fbdev/ps3fb.c`:
- `ps3fb_gpu_get_info()`: memory and context handles, ddr_lpar, VRAM size, ps3fb's own frame
  range, the mode, the line length;
- `ps3fb_gpu_set_hooks(vsync, exception)`, called from ps3fb's IRQ handler;
- `ps3fb_gpu_takeover(bool)`: stops and restarts its blit/flip thread;
- `ps3fb_gpu_flip(offset)`.

**Verify:** boots, `rsx-clear` and `rsx-probe` still work, `modprobe vkms` gives
`/dev/dri/card0`.

## Phase 2 – `ps3rsx` render driver (out-of-tree module, `gpu/ps3rsx/`)

**Build.** Generalise the `gpu-tools` target so it builds every `gpu/*/Kbuild` against
`build/kernel/obj`. Reuse and adapt code from `build/kernel/src/drivers/gpu/drm/nouveau/`:
- `nouveau_abi16.c`: the NVIF subset, GETPARAM, channel and notifier logic;
- `nouveau_gem.c`: the pushbuf argument handling, the reloc apply loop, CPU_PREP;
- `nouveau_fence.c`: the seqno and fence-list logic.

Starting points for the context, iomap and FIFO code: `gpu/kmod/rsx_ctx_probe.c` and
`gpu/tools/rsx-probe.c`.

Files: `drv.c`, `mem.c`, `abi16.c`, `submit.c`, `validate.c` (later `kms.c`).

**`drv.c`**
- A platform device "ps3rsx" with `of_node = of_root`, so libdrm's `drmGetDevice2`
  accepts it.
- DRM name "nouveau", version 1.3.1, `DRIVER_GEM|DRIVER_RENDER`.
- At load, a NOP + SET_REFERENCE probe. If the GPU is already wedged, refuse to load and
  log "reboot".

**`mem.c`** (no TTM, pinned BOs, each with a `dma_resv`)
- **VRAM:** a `drm_mm` from above ps3fb's frames (1 MB aligned) up to 224 MB. The offset is
  relative to ctxdma 0xfeed0000.
- **GART:** a pool of 1 MB `alloc_pages(order 8)` chunks.
  - Preallocated (default 32 MB, module parameter), growable.
  - Iomapped into every context at a fixed IO address.
  - Sub-allocated at 4 KB granularity with a `drm_mm`.
  - The offset is the IO address, relative to ctxdma 0xfeed0001.
- **mmap:**
  - VRAM: `io_remap_pfn_range` on the lpar address (write-combined if E8 helps);
  - GART: cached;
  - the notifier GEM object maps the context's 64 KB reports area.
- **Sharing:** FLINK, PRIME, dumb buffers in VRAM.

**`abi16.c`**
- **GETPARAM:**
  - `FB_SIZE`/`AGP_SIZE`: the managed sizes;
  - `CHIPSET_ID`: 0x47 (selects class 0x4097);
  - `PCI_VENDOR`: 0x104d (Sony), which is how Mesa detects the PS3;
  - `BUS_TYPE`: PCIE;
  - `PTIMER_TIME`: the timebase;
  - everything else returns -EINVAL, including `HAS_VMA_TILEMODE`, so zink is not picked.
- **NVIF:** device info (chipset 0x47, CURIE, PCIE); SCLASS with the nv30 class list; engine
  object NEW/DEL as bookkeeping only. GROBJ_ALLOC behaves the same way.
- **CHANNEL_ALLOC:**
  - a new lv1 context per channel (or shared, depending on E1);
  - a 64 KB ring in a GART chunk;
  - `pushbuf_domains` = GART;
  - `notifier_handle` = the reports-area GEM object.
- **NOTIFIEROBJ_ALLOC** returns offsets that line up with the hypervisor ctxdma bases:
  - fence `0xbeef1e00` → 0 (ctxdma 0x66606660, labels);
  - query `0xbeef0351` → 0x1400 (ctxdma 0x66626660, reports);
  - `0xbeef0301` → 0x1000 (0x66604200).

**`submit.c`**
- **GEM_PUSHBUF:**
  - look up the BOs and apply relocs when `presumed` is invalid (LOW/OR with vor/tor);
  - write `CALL(io|2),0` into the ring for each push, then SET_REFERENCE seq, then PUT;
  - wrap the ring with JMP;
  - return `suffix0 = 0x00020000` and `vram/gart_available`.
- **Fences:** a `dma_fence` per submit, attached to each BO's resv. A kthread polls ctrl+0x48
  while fences are pending. If readbacks turn out stale, switch to a kernel-owned 3D back-end
  release.
- **Cross-channel sync:** a CPU wait in the kernel. Never a FIFO semaphore acquire.
- **Wedge detection:** the exception hook, or no ref progress for 2 s while work is pending.
  On a wedge, dump the driver-info exception block, then every ioctl returns -EIO and pending
  fences are signalled with an error.

**`validate.c`** (module parameter, on during bring-up)
- Decode user pushes:
  - no JMP/CALL inside them;
  - track subchannel → class from method-0 binds, and allow only hypervisor handles;
  - methods allowlisted per class (from envytools rnndb plus Mesa's usage); unknown methods
    are rejected and logged;
  - DMA_* values must be known ctxdmas;
  - M2MF/SIFM ranges must fall inside a submitted BO.
- `trace=1` dumps the decoded submit to netconsole before the kick; `submit=0` is a dry run.

**Raw-ioctl tests** (`gpu/tools/rsxdrm-*.c`, against `make headers_install` output):
- `rsxdrm-info`: version, GETPARAM, NVIF, `drmdevice`.
- `rsxdrm-smoke` with `nop | m2mf | clear | fence | two-chan`, one per run. M2MF sets its
  notifier DMA (0x180) before it transfers.

## Phase 3 – Mesa patch, cross build, offscreen GL

**Patch** `mesa/patches/0001-nouveau-nv30-ps3-rsx.patch` (~60 lines, active when
`PCI_VENDOR == 0x104d`):
- `nouveau_screen.c`: `nv04_fifo.vram/gart` = 0xfeed0000/0xfeed0001 (the reloc vor/tor
  values follow automatically).
- `nv30_screen.c`:
  - objects: 3D 0x31337000, M2MF 0x31337303, SURF2D 0x313371c3, SWZ 0x31337a73,
    SIFM 0x3137af00;
  - notifiers: fence 0x66606660, ntfy 0x66604200, query 0x66626660;
  - fence emit writes the seqno pre-swizzled (bytes 0 and 2 swapped), per E4.
- If E5 says 3D state isn't per channel: force a full state re-emit on every kick.

**Build** (cross-compiled with meson against a sysroot):
- Dockerfile gets meson, ninja, pkgconf, python3-mako/yaml/packaging, bison, flex.
- `make mesa-sysroot`: `mmdebstrap --arch=ppc64` at the same snapshot with
  `mesa/sysroot-packages.txt` (libdrm-dev, libglvnd-dev, X11/xcb dev headers, expat, zlib,
  zstd, udev) → `build/mesa/sysroot`.
- `config.mk`: `MESA_VERSION`/`MESA_SHA256`, the upstream tarball at the snapshot's version.
- `mesa/cross-ppc64.ini`.
- Meson options: `-Dgallium-drivers=nouveau,softpipe -Dvulkan-drivers= -Dllvm=disabled
  -Dplatforms=x11 -Dglx=dri -Degl=enabled -Dgbm=enabled -Dglvnd=enabled --prefix=/opt/mesa`.
- `make mesa` → `build/mesa/`; `make mesa-deploy` (sudo rsync) → `$(NFS_ROOT)/opt/mesa`, plus
  `/opt/mesa/env.sh` with the library, driver and EGL vendor paths.

**Bring-up ladder.** Each rung runs first with `submit=0` (the dump gets inspected), then for
real with the validator on. softpipe on the PS3 is the reference image.
1. `eglinfo -B`: the renderer is NV47.
2. `gpu/tools/egl-clear`: surfaceless GLES2 FBO clear plus `glReadPixels`, compared with
   softpipe.
3. `egl-tri`: vertex and fragment programs, inline vertices on big-endian.
4. `egl-tex`: texture upload and a depth test.
5. Offscreen gears: 100 frames, checksum against softpipe.

Save the GL extension list, to check what glamor needs.

## Phase 4 – Minimal KMS in ps3rsx (`kms.c`)

**Pipeline:**
- `drm_simple_display_pipe` with a virtual connector.
- One fixed mode from `ps3fb_gpu_get_info`.
- Formats: XRGB8888/BGRX8888 with prefer-host-byte-order.
- `atomic_check` requires a VRAM BO with ps3fb's line length.

**Flips and vblank:** flips go through `ps3fb_gpu_flip` (or our own context, if E6 allowed
it). vblank comes from the vsync hook, or our IRQ if E7 passed.

**Handover:** the first modeset calls `ps3fb_gpu_takeover(true)`. lastclose releases the
display and flips back to offset 0, so fbcon returns.

**Tests:** `modetest` pattern (webcam), `vbltest`, `kmscube`.

## Phase 5 – Xorg + glxgears

**Rootfs** (`rootfs/packages.txt`): xserver-xorg-core, xinit, xauth, x11-xserver-utils,
x11-apps, mesa-utils, libdrm-tests, kmscube, glvnd/xcb runtime libraries; tibi joins the
`render` group.

**Steps:**
1. X with vkms and `AccelMethod none`, to prove X fits in about 200 MB of RAM.
2. `/etc/X11/xorg.conf.d/20-ps3rsx.conf`: modesetting on our card, first `AccelMethod none`,
   then glamor.
3. `xinit glxgears -info` (with `/opt/mesa/env.sh`). Check that `glxinfo -B` reports nouveau
   NV47, then take an `xwd` screenshot plus a webcam frame.

**Fallback if glamor won't run on nv30:** xf86-video-nouveau (EXA + DRI2) with the same handle
patch in libdrm_nouveau.

## Risks

- **Wedges.** Mitigations: dry runs, the validator, netconsole traces (so first fix
  netconsole: it has never reached `make listen`), one test per run, refusing to load on an
  already wedged GPU.
- **Mesa init methods the RSX rejects.** The validator catches them in a dry run; the patch
  skips them on the PS3.
- **Big-endian bugs** (colour order, 16-bit data, query word layout). Compare against
  softpipe. Occlusion queries stay off until the query layout is verified.
- **Notifier ctxdma bases don't line up as assumed.** Checked by the `fence` smoke test and a
  GET_REPORT check.
- **RAM fragmentation** for order-8 chunks: preallocate at load, add zram, keep Xorg minimal.

## Verification (per phase, as above)

| Phase | Done when |
|---|---|
| 0 | docs/RSX.md answers E1–E8. |
| 1 | The kernel boots and `vkms` loads. |
| 2 | All `rsxdrm-smoke` subtests pass with no wedge. |
| 3 | Offscreen gears matches softpipe. |
| 4 | `kmscube` runs on screen (webcam). |
| 5 | `glxgears` runs in Xorg on the RSX, confirmed by `glxinfo -B` and a webcam frame. |

docs/PLAN.md and README get updated at each milestone.
