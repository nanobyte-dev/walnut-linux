# Plan: Mesa nv30 on the PS3 RSX, up to glxgears in Xorg (revised after Phase 0)

## Context

Goal: a DRM driver `ps3rsx` that registers as "nouveau", plus a small Mesa patch, so that
stock Mesa nv30 renders on the RSX: offscreen first, then KMS, then glxgears in Xorg. Getting it
working comes first; pushing the hardware limits comes later (last section).

Phase 0 (hardware derisking, 2026-10-14) is done; results are in docs/RSX.md. What changed the
design:

- **3D state is shared between hardware channels (E5).** lv1 does no PGRAPH context switching,
  so the driver uses **one** hardware context. When work from several processes is mixed, the
  kernel restores each one's 3D state itself (a software context switch, Phase 4).
- **Contexts:** ps3fb's plus at most 2 more. Allocating up to that limit once stalled every
  channel. One extra context is safe, and that's all we use.
- **What works:**
  - IO window: 1 MB chunks can be mapped anywhere below 256 MB at any time, and the GPU
    reaches them (E2/E9). That is the GART pool.
  - CALL/RETURN into user buffers (E3).
  - SET_REFERENCE is a real completion fence (E11).
  - VRAM is usable up to 253 MB (E10), against the 224 MB ps3fb assumes.
  - Our own context gets vsync interrupts (E7) and can flip the display (E6).
  - Raw triangles, vertex arrays and textures draw correctly (E12/E13).
  - Mesa's whole init stream is accepted (E15).
- **Fence and notifier:**
  - the notifier object writes at reports+0 (E14), so the fence must use another label offset;
  - the 3D back-end release swaps bytes 0 and 2, and writing a pre-swizzled value fixes that (E4).
- **Constraints:**
  - Any graph exception wedges the GPU until a console reboot.
  - CPU access to VRAM is slow, write-combined or not (E8).
  - Every reboot needs the user: they set the OtherOS flag in the XMB (fan on SYSCON first),
    then `make netboot`. I never reboot the PS3 or touch its boot flag.
  - No kexec.

## Phase 1: one batched kernel rebuild (one reboot)

Everything later phases need from the kernel goes in here, so that Phases 2–6 are modules
and userspace only (plan review, 2026-10-14).

**Config** (`kernel/config/ps3.config`):
- `CONFIG_DRM=m`: selects sync_file, dma-buf and kcmp.
- `CONFIG_DRM_FBDEV_EMULATION`: pulls in the KMS helpers, so vkms isn't needed.
- `CONFIG_COMPACTION=y`: `ps3_defconfig` has it off.
- `CONFIG_DEBUG_FS=y`, `CONFIG_DYNAMIC_DEBUG=y`, `CONFIG_ZRAM=m`, `CONFIG_NFS_SWAP=y`.
- The driver uses no TTM, DRM scheduler or drm_exec (`drm_gem_lock_reservations` is enough).

**GART reservation:** a boot-time `ps3rsx_gart=` region (default 64 MB, 1 MB aligned), using
the existing `prealloc()`/early_param code in `arch/powerpc/platforms/ps3/setup.c:113-146`,
exported like `ps3fb_videomemory`.
- It comes from highmem, which is memblock-added before `prealloc` runs (mm.c:1240,
  setup.c:255; review 2), so it fits next to ps3fb's 9 MB.
- The lpar address is looked up per 1 MB chunk with `ps3_mm_phys_to_lpar`.
- Order-8 allocations after boot are unreliable on 200 MB.

**ps3fb patches:** keep `local/0003` (the probe modules use `ps3fb_gpu_handles`) and add
`local/0004-ps3fb-gpu-sharing-api.patch` on top.

*Info and context*
- `ps3fb_gpu_get_info()`: memory handle, ddr_lpar, `memory_size`, line length
  (`ddr_line_length`), the mode, frame 0's offset (`full_offset`), and ps3fb's VRAM
  reservation, rounded up to 1 MB. The reservation covers the whole DDR area its `smem_len`
  can use, because the number of frames depends on the mode.
- `ps3fb_gpu_ctx_get(flags)` allocates our second lv1 context on first call (under its own
  mutex), then caches it and hands back the same one forever: handle, ctrl/dinfo/reports lpars,
  IRQ outlet.
  - Module reloads therefore never allocate or free contexts (E1's stall is unexplained).
  - The first call's flags stick until the next reboot. ps3rsx owns the ioremaps of those
    areas.
- `ps3fb_gpu_iomap(io, lpar, size, flags)`: a wrapper that records our iomaps, so that
  `ps3fb_shutdown`/`.remove` can undo them, then free our context, before ps3fb's
  `lv1_gpu_memory_free`. That works even if ps3rsx was wedged.
- `suppress_bind_attrs`, so a sysfs unbind can't pull ps3fb out from under ps3rsx.

*Takeover* (`ps3fb_gpu_takeover(bool)`)
- **Lock order:** a new `ps3fb.own_lock` is outermost, then `console_lock`, then
  `ps3_gpu_mutex`. The fb_info lock is never taken by takeover, because it already has an ABBA
  ordering with `console_lock`. Nothing takes `own_lock` while holding `console_lock`.
  ENTER/EXIT take `own_lock` and check `gpu_owned` before `StartStopTask`. This closes the race
  where ENTER unmaps `fifo.ctrl` under a polling takeover.
- **Taking over:**
  - take `own_lock`, then `console_lock()` (every sync caller holds it), then `ps3_gpu_mutex`;
  - set `gpu_owned`. While it is set:
    - the *whole* `ps3fb_sync_image` (blit and both flips) is a no-op;
    - the IRQ handler doesn't wake ps3fbd, so `StartStopTask` isn't used and `fifo.ctrl`
      stays mapped;
  - append `SET_REFERENCE cookie` to ps3fb's own ring (reusing `fb_blit`'s wrap check), kick
    it, and poll ctrl+0x48. ps3fb itself never emits a reference, and GET is not completion.
  - fail if a ps3gpu user is inside ENTER (task == NULL).
- **While owned**, these return -EBUSY:
  - ps3fb's direct-GPU interface: ioctls 1860–1901 (ENTER/EXIT/attribute/cursor) and the
    `/dev/ps3gpu_*` mmap;
  - mode changes in `set_par`, so `ps3av_set_video_mode` can't change the mode under KMS.
    `check_var` keeps passing (fbcon only logs `set_par` failures);
  - `ps3fb_blank` (FBIOBLANK and fbcon unblank) is a no-op, because `ps3av_video_mute` would
    blank the KMS output.
- Blacklist `ps3_lv1call` and `ps3_disp_manager` while ps3rsx is in use: both allow arbitrary
  lv1 GPU calls.
- **Releasing:** the caller has already waited for its last ref. Then, under `console_lock` +
  `ps3_gpu_mutex`: `ps3gpu_restore_state()`, a full `ps3fb_sync`, clear `gpu_owned`.

*Exceptions*
- `ps3fb_gpu_set_exception_hook(fn, data)` is called from `ps3fb_vsync_interrupt` (hard IRQ)
  with `v1` and a pointer to `graph_exception_info`. The hook only records it and schedules
  work.
- The hook's fn and data are published as one struct pointer (`WRITE_ONCE`). Setting it to
  NULL calls `synchronize_irq(ps3fb.irq_no)`, so module unload is safe.
- `ps3fb_gpu_exception_seen()` is a sticky counter. `GPU_USER_DEBUG` won't do: every
  `restore_state` clears it.

**Rootfs:** blacklist `ps3vram`, add `libdrm-tests`; kernel cmdline gets `consoleblank=0`
(`ps3fb_blank` → `ps3av_video_mute` would blank KMS output).

**Swap, not on the PS3's HDD** (VRAM stays for the GPU; ps3vram can't load anyway while
ps3fb owns all of VRAM):
- `ps3da`/`ps3db`/`ps3dc` almost certainly are GameOS's: raw disk, dev_hdd0 and the 2 GB
  dev_hdd1 cache (review 3). They're encrypted, so content checks can't identify them, and
  `mkswap` on any of them destroys GameOS data. **Never write to them.**
- An OtherOS++ Linux region would appear as an extra disk (e.g. `ps3dd`), identified only by
  the `ps3dX … (N MiB, M MiB for OtherOS)` dmesg line plus the user's confirmation.
- Instead: zram (compressed RAM swap). A swapfile on the NFS root was tried and deadlocked the
  PS3 under memory pressure (`Write error -12 on dio swapfile`). A USB stick is the
  alternative for real swap (`USB_STORAGE=y` is built in).
- Swap is a prerequisite for Phase 6 (about 160 MB of RAM is left after ps3fb and the GART).

**Verify:**
- boots, `rsx-tri tex` still draws;
- `modprobe drm` works;
- the GART region is reserved;
- the second context comes from ps3fb;
- a takeover/release cycle from a test module leaves the console working;
- ps3fb's GPU ioctls return -EBUSY while the GPU is taken over.

## Phase 2: `ps3rsx` render driver (out-of-tree module, `gpu/ps3rsx/`)

**Build.** Generalise the `gpu-tools` target to build every `gpu/*/Kbuild`.

**Reuse:**
- `build/kernel/src/drivers/gpu/drm/nouveau/`:
  - `nouveau_abi16.c`: NVIF subset, GETPARAM, channel and notifier logic;
  - `nouveau_gem.c`: pushbuf argument handling, reloc apply, CPU_PREP;
  - `nouveau_fence.c`: seqno and fence list.
- From our tools:
  - `gpu/kmod/rsx_ctx_probe2.c`: context alloc, reset-program replay, iomap, kick/wait;
  - `gpu/tools/rsx-mesainit.c`: the known-good init stream.

**Hardware ownership**
- **At load:**
  - refuse if `ps3fb_gpu_exception_seen()`, or if a NOP + SET_REFERENCE probe times out;
  - `ps3fb_gpu_takeover(true)`, so ps3fb's blits never interleave with our work (they share
    PGRAPH state);
  - `ps3fb_gpu_ctx_get(0x820)`, then run ps3fb's reset program in it;
  - iomap the GART reservation; plug our IRQ outlet (vsync) and the ps3fb exception hook.
- **At unload, and in a platform `.shutdown`:** drain the ring, wait for the last ref, unmap
  and unplug, keep the context, `takeover(false)`.
- Console: SSH and netconsole while the driver is loaded.
- `platform_device_register_simple("ps3rsx")`. libdrm falls back to MODALIAS without OF data;
  check the snapshot's libdrm has that fallback. No `of_root`.
- DRM name "nouveau", version 1.3.1 (exactly Mesa's minimum), `DRIVER_GEM|DRIVER_RENDER`.

**Memory** (no TTM, pinned BOs, each with a `dma_resv`; presumed offsets never change)
- **VRAM, CPU-visible:** `drm_mm` from above ps3fb's reservation (from `get_info`) to 224 MB.
  Dumb and scanout buffers live here.
- **VRAM, GPU-only:** 224 MB to 254 MB (`memory_size` 0xfe00000). No MAPPABLE, `map_handle=0`:
  a CPU mapping there could make lv1 refuse the HPTE, and `ps3_hpte_insert` then BUG()s. Mesa
  stages VRAM through GART anyway. An optional dedicated "may crash" probe can settle CPU
  access later.
- **GART:** the boot reservation, iomapped as 1 MB pages, sub-allocated at 4 KB.
  - The offset is the IO address, relative to 0xfeed0001.
  - A scratch chunk is aliased over every unused IO page (aliasing works, E2), so stray
    fetches never fault.
  - IO pages that pending fences still use are never unmapped.
- **mmap:** GART cached, the 64 KB reports area noncached (the notifier GEM object), VRAM
  noncached.
- **Sizes:** GEM_NEW sizes are rounded to 4 KB; `domain=0` means VRAM|GART.
- **Sharing:** FLINK, PRIME.

**ioctls**
- **GETPARAM:**
  - `FB_SIZE`/`AGP_SIZE` = managed sizes;
  - `CHIPSET_ID` = 0x47;
  - `PCI_VENDOR` = 0x104d (Mesa's PS3 check);
  - `BUS_TYPE` = PCIE;
  - `PTIMER_TIME` = nanoseconds, from the timebase;
  - a private `PS3_ABI` (0x1000) param, which the patched Mesa queries. CHANNEL_ALLOC is
    refused until it has been called, so stock Debian Mesa (pulled in by `mesa-utils`) fails
    cleanly at init instead of submitting unpatched streams;
  - everything else, including `HAS_VMA_TILEMODE`, returns -EINVAL.
- **NVIF:**
  - It needs its own `unlocked_ioctl` path, copied from `nouveau_drm_ioctl`, because the
    argument size varies per request.
  - Handles: NEW of the device (route 0), device info MTHD (0x47, CURIE, PCIE), NEW and DEL of
    engine objects (route 0xff, token = channel, owner ANY), including NV01_NULL (0x30,
    handle 0).
  - Mesa's subchannel DEL never reaches us, so objects are freed at CHANNEL_FREE.
  - GROBJ_ALLOC is the same bookkeeping.
  - Bookkeeping is **per channel**, because handles repeat across channels. It is keyed on the
    **PS3 handles**: the Mesa patch passes them at allocation time (objects, notifiers,
    `nv04_fifo.vram/gart`), so the command stream carries them with no further Mesa change.
    The kernel accepts only the known PS3 handle set.
- **CHANNEL_ALLOC:** a software channel on the shared hardware context.
  `pushbuf_domains` = GART; `notifier_handle` = the reports-area GEM object.
- **NOTIFIEROBJ_ALLOC hands out per-channel slices** (all channels share one reports area):
  - fence `0xbeef1e00` → 0x20 + 16·channel;
  - query `0xbeef0351` → a 4 KB slice at 0x1400 + 0x1000·channel (at most 14 channels);
  - `0xbeef0301` → 0x1000.
- **GEM_PUSHBUF: copy each push into the kernel ring** instead of CALLing user memory.
  - This avoids validate-then-modify races, and the validator and shadow see exactly what
    the GPU runs.
  - It is needed anyway: on big-endian Mesa pushes every vertex inline (`nv30_vbo.c` forces
    `vbo_fifo`), so draws are long 0x1818 bursts in the pushbuf.
  - **Ring:** 2 MB (two GART chunks). A submit is at most about 512 KB, since Mesa's push BOs
    are 512 KB. Larger submits are rejected.
  - **Per submit:**
    - look up BOs;
    - copy and validate the pushes;
    - apply relocs **to the copy** if `presumed` is ever invalid (offsets never move, so it
      shouldn't be);
    - append SET_REFERENCE seq and write PUT.
  - **Wrap:** JMP only between submits (a submit is at most 512 KB, the ring 2 MB), so no
    mid-push decoding is needed. Ring space is counted from ref.
  - Return `suffix0 = suffix1 = 0` (no RETURN needed) and `vram/gart_available`, and honour
    `NOUVEAU_GEM_PUSHBUF_SYNC`. The first `nr_push=0` call must succeed.
- **Fences:**
  - a `dma_fence` per submit, attached to the referenced BOs but **not** to BOs that are only
    push sources (Mesa maps those at every buffer switch);
  - signalled by an hrtimer poll of ref (HZ=250, PREEMPT_NONE); CPU_PREP polls ref directly
    while waiting;
  - expect heavy churn of 4 KB GART BOs, from Mesa's per-fence BO.
- An lv1 "user command" completion interrupt is speculative: an unknown software method could
  raise an exception. Try it only after the baseline runs.

**Safety**
- **Wedge detection:** exception IRQ, or no ref progress for 2 s with work pending. On a
  wedge: dump the exception block to netconsole, return -EIO, error all fences.
- **`validate.c` on by default:**
  - decode user pushes, checking every method of a multi-count burst, including
    non-incrementing headers; no JMP/CALL/RETURN at all;
  - only hypervisor object handles in method-0 binds;
  - **Method allowlist, built from data:**
    - a table generated from `docs/MESA-METHODS.md` §4 as (class, base, stride, count, max
      burst);
    - indexed ranges expanded: 16 texture units at stride 0x20, 16 vertex formats/buffers, all
      immediate-attribute formats, VP 0xb80–0xbfc, constants 0x1f00–0x1f7c;
    - unknown methods are logged and rejected.
  - **Data words are not all checked; only:**
    - handle slots, against {feed0000, feed0001, 66604200, 66606660, 66626660, 0};
    - location bits ∈ {1, 2};
    - at trigger methods (0x1808 begin, 0x1d94 clear, 0x328, 0x40c), the shadowed
      offset/pitch/clip/format against the owning BO's bounds;
    - 0x1d6c: 16-byte aligned and inside the channel's fence slot. 0x1800/0x1e98: type 1 and
      inside the channel's query slice;
    - VP load pointer + count ≤ 512, constants ≤ 468.
- **Debug options:** `trace=1` dumps each submit to netconsole; `submit=0` is a dry run.

**Tests** (`gpu/tools/rsxdrm-*.c`, raw ioctls):
- `rsxdrm-info`: GETPARAM, NVIF, `drmdevice`;
- `rsxdrm-smoke nop|m2mf|clear|tri|fence|reload`:
  - `tri` reuses rsx-tri's words through the driver;
  - `fence` runs about 10k iterations of a 0x1d70 release followed by a ref wait, then checks
    that the label had landed when ref arrived (Mesa's fence order);
  - `reload` runs 20 rmmod/insmod cycles;
  - `quirks` covers three things Mesa does that libgcm doesn't: the M2MF NOP (0x100 on the
    M2MF subchannel), RT_ENABLE values outside libgcm's set, and vertex format type 0;
  - `flip` flips both heads to an offset in our `drm_mm` range, then to one at 224–254 MB;
- the result checked by readback, and by webcam (`tools/webcam-grab.sh`) after a flip
  (lv1_gpu_display_flip from our context, E6).

## Phase 3: Mesa patch, cross build, offscreen GL (one process)

**Patch** `mesa/patches/0001-nouveau-nv30-ps3-rsx.patch`. The PS3 is detected in
`nouveau_screen_init`, before the channel is allocated: chipset 0x47,
`nouveau_getparam(PCI_VENDOR) == 0x104d` and the `PS3_ABI` getparam (0x1000) succeeds. The result
is `nouveau_screen.is_ps3`; every change below is gated on it.
**Handles are substituted at allocation time.** The streams then carry them automatically,
because Mesa writes the handles it allocated.
- **`nouveau_screen.c`:** `nv04_data.vram/gart` = 0xfeed0000/0xfeed0001 on the PS3.
- **`nv30_screen.c`:**
  - objects allocated with 3D 0x31337000, M2MF 0x31337303, SF2D 0x313371c3, SWZ 0x31337a73,
    SIFM 0x3137af00;
  - notifiers allocated with fence 0x66606660, ntfy 0x66604200, query 0x66626660. Mesa asks
    for **32 bytes (fence), 32 bytes (ntfy) and 3968 bytes (query, 4096 - 128)**: the kernel's
    `NOTIFIEROBJ_ALLOC` limits must accept these;
  - fence emit writes `screen->fence->offset` and the seqno pre-swizzled (bytes 0 and 2
    swapped), so the readback at `notify->map + fence->offset` equals the sequence;
  - drop VERTEX_BUFFER/INDEX_BUFFER from `vidmem_bindings` on the PS3. On big-endian the CPU
    reads vertex data to push it inline, and VRAM reads run at 10 MB/s.
- **`nv30_query.c`:** QUERY_GET (0x1800) and RENDER_ENABLE (0x1e98) add
  `query->offset - 0x1400`, so each channel's query slice lands in its own place. Timer queries
  stay on: type 1 gives the u64 timestamp Mesa reads.
- **`nv30_miptree.c`:** on the PS3, scanout pitch is only 64-byte aligned. Mesa's
  power-of-two padding (7680 → 8192 at 1080p) would display sheared, because the display
  pitch is fixed by ps3av at width×4.
- **Later, as big-endian issues appear:** format tables.

**Build:** cross-compile with meson against a ppc64 sysroot. The target is Mesa 26.2.4, the
version in the 20261013 snapshot (`MESA_VERSION`/`MESA_SHA256` in `config.mk`).
- **`docker/Dockerfile.mesa`:** a separate image `FROM` the builder (so the builder tag, and the
  kernel, don't change) adding `g++-powerpc64-linux-gnu`, meson (trixie's 1.7), ninja, pkgconf,
  python3-mako/yaml/packaging.
- **`make mesa-sysroot`:** `mmdebstrap --arch=ppc64 --variant=extract` (no maintainer scripts,
  so no qemu, no `--privileged`) with the packages in `mesa/sysroot-packages.txt`. base-files
  provides the merged-/usr `/lib` link that glibc's `libc.so` script needs; the script checks it.
- **`mesa/cross-ppc64.ini`:** `--sysroot`, `-mcpu=cell`, `-ffile-prefix-map`,
  `needs_exe_wrapper`, `pkg_config_libdir` and `sys_root` (= `PKG_CONFIG_SYSROOT_DIR`) in the
  sysroot. Two builds give identical libraries.
- **Meson options:** `-Dgallium-drivers=nouveau,softpipe -Dvulkan-drivers= -Dllvm=disabled
  -Dgallium-va=disabled -Dvideo-codecs= -Dplatforms=x11 -Dglx=dri -Degl=enabled -Dgbm=enabled
  -Dglvnd=enabled -Dgles1=disabled -Dgles2=enabled --buildtype=debugoptimized
  --prefix=/home/tibi/mesa`. The prefix is `$(NFS_ROOT)/home/tibi/mesa` on the PC, which tibi
  owns, so deploying needs no sudo.
- **No clash with Debian's Mesa 26.2.4:**
  - Mesa's `VERSION` gets a suffix (`26.2.4-ps3`), so `libgallium-*.so` names differ;
  - `env.sh` sets `LD_LIBRARY_PATH`, `__EGL_VENDOR_LIBRARY_FILENAMES`,
    `__GLX_VENDOR_LIBRARY_NAME=mesa`, `GBM_BACKENDS_PATH`, `LIBGL_DRIVERS_PATH` and
    `MESA_SHADER_CACHE_DISABLE`;
  - the rootfs has no GL/X client libraries, so the build bundles everything the installed
    files and glvnd's libEGL/libGLESv2/libGL need from the sysroot into `lib/` (not glibc or
    the compiler runtime);
  - for Xorg (the setuid wrapper strips `LD_LIBRARY_PATH`), add `/etc/ld.so.conf.d/00-ps3mesa.conf`
    or run Xorg as root with the env.
- `make mesa mesa-tests`, then `make mesa-deploy` (rsync to `$(NFS_ROOT)/home/tibi/mesa`).

**Tests** (`mesa/tests`, installed to `~/mesa/bin`): `egl-clear`, `egl-tri`, `egl-tex`,
`egl-gears` render offscreen (surfaceless EGL, or `EGL_RENDER_NODE=/dev/dri/renderD128` for the
device platform) and print `GL_RENDERER` and a checksum; an argument writes the image as PPM.
On the PS3: `. ~/mesa/env.sh`, then each test once with
`LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=softpipe` (reference) and once without (nouveau).

**Ladder.** Each rung runs first with `submit=0`, then for real, against a softpipe reference:
1. `eglinfo -B` (needs `mesa-utils-bin` in the rootfs)
2. `egl-clear` + `glReadPixels`
3. `egl-tri`
4. `egl-tex`
5. offscreen gears, 100 frames, checksum

Save the extension list.

## Phase 4: multi-process: software context switch (kernel)

Needed as soon as two processes render: X + a GL client, or a compositor.

A Mesa-side "re-emit everything on each kick" isn't enough. Mesa has no start-of-pushbuf hook,
and copies continue across flushes: M2MF DMA in/out is set once before a loop that can flush
(`nv30_transfer.c:537-560`). So the kernel does it.

**Shadow.** `validate.c` already decodes every copied push. It keeps a per-channel shadow of
the state methods of **every subchannel**: 3D, plus M2MF/SF2D/SWZ/SIFM state and their 0x180
DMA, SIFM 0x2fc, and the method-0 binds.

**Indexed methods are emulated:**
- vertex-program data goes through the 0xb80–0xbfc window with auto-increment, starting from
  load pointer 0x1e9c (512-slot shadow);
- constants go through 0x1efc plus 0x1f00–0x1f7c.

The load pointers are restored afterwards.

**Never shadowed** (side-effect methods):
- 0x100/0x104/0x110;
- 0x1d70/0x1d74 releases;
- 0x1710–0x1718, 0x1fd8, 0x17c8, 0x1800;
- the draw and inline-data range 0x1808–0x1824;
- immediate vertex attribute 0 writes (they emit a vertex);
- the 0x1d94 clear;
- the copy triggers: **M2MF 0x328 BUFFER_NOTIFY**, the last word of the OFFSET_IN×8 burst
  (0x310 OFFSET_OUT is state), and **SIFM 0x40c**, inside the SIZE×4 burst.

**Restore.**
- The kernel keeps a second shadow of what PGRAPH currently holds. When the next submission
  is from another channel, it emits only the words that differ (typically 1–2 KB, worst case
  about 24 KB).
- Then it invalidates the texture and vertex caches (0x1fd8, 0x1710–0x1718).
- Known limit: the ZPASS accumulator can't be restored, so occlusion queries running in two
  processes at once mix their counts.

**Tests:**
1. Replay a channel's own shadow into itself: it must be idempotent (same images as without).
2. Two `egl-tri` processes with different colours and viewports, interleaved; readbacks match
   softpipe.

## Phase 5: minimal KMS in ps3rsx (`kms.c`)

- `drm_simple_display_pipe` + a virtual connector, with the single current mode from
  `ps3fb_gpu_get_info`.
- BGRX8888 matches the RSX's X,R,G,B memory order on big-endian; set
  `quirk_addfb_prefer_host_byte_order`. Also accept XRGB8888 for kmscube (its colours will be
  off).
- `addfb` rejects any pitch other than ps3fb's `ddr_line_length`. The scanout BO may sit
  anywhere in VRAM if the Phase 2 `flip` test passed above 224 MB; otherwise it must be below
  224 MB.
- Flips go to **both** heads, as ps3fb's do, at `full_offset`-relative addresses.
- ps3rsx doesn't set up DRM fbdev; fbcon stays on fb0.
- PRIME uses the stock GEM helpers (same-device import shortcut).
- Flip with `lv1_gpu_display_flip` from our context. vblank from our IRQ outlet. The flip-done
  bit (3) is untested, so send flip events at vblank first.
- On unload: flip back to ps3fb's frame 0 and `takeover(false)`, so fbcon returns.
- **Tests:** `modetest` pattern (webcam), `vbltest`, `kmscube`.

## Phase 6: Xorg + glxgears

- **Rootfs:** xserver-xorg-core, xinit, xauth, x11-xserver-utils, x11-apps, mesa-utils,
  kmscube, glvnd/xcb runtime libs; tibi in the `render` group.
- **Go/no-go from the Phase 3 extension list:** glamor needs GL ≥ 2.1,
  `GL_ARB_texture_border_clamp` and VAO (xserver `glamor.c:721-761`). modesetting only offers
  DRI2/DRI3 with glamor, so without it glxgears would run on softpipe.
- nv30 reports GLSL 120, so glamor takes its GLSL-1.20 paths: slower and less tested. The only
  big-endian prior art is NV4x in G5 PowerMacs, so expect format bugs first.
- **Prerequisites:** swap (NFS swapfile) and Phase 4.
- **X:** modesetting on our card with glamor (needs Phase 4) and `prefer_shadow`, since CPU
  reads from VRAM run at 10 MB/s. `AccelMethod none` is only a KMS sanity check.
- **Run:** `xinit glxgears -info` with `~/mesa/env.sh`, then check `glxinfo -B` (nouveau
  NV47), an `xwd` screenshot and a webcam frame.
- **If glamor refuses nv30:** fix the glamor/nv30 gaps in Mesa (extension exposure). The floor
  is GBM/EGL gears on KMS. xf86-video-nouveau is not an option: it needs a PCI device and NV04
  2D objects that the hypervisor doesn't provide.

## Later: pushing the hardware limits (after the baseline runs)

| Limit hit | Ideas to get past it |
|---|---|
| CPU access to VRAM above 224 MB not proven safe | a dedicated probe that may crash the kernel, then a lifted ps3fb `GPU_DDR_SIZE` |
| 3D state not context-switched (E5) | lv1 context attributes / RAMIN layout for a hardware context save area (RPCS3, lv1 RE notes) |
| Context limit, and allocating up to it stalls the GPU (E1) | find which step breaks it (third alloc, failed fourth, frees) |
| Exceptions wedge the GPU until a reboot | lv1 attributes we haven't tried; PGRAPH exception acknowledge; whether a Petitboot-only reboot suffices |
| 1 MB IO pages (flags 0x820) | flag 0x2 (64 KB pages) for finer GART allocations |
| CPU↔VRAM 10–120 MB/s (E8) | cache-inhibited vs other lv1 mapping attributes, `dcbz`-free copy loops |
| No tiling / zcull | lv1 attributes 0x300/0x301 (RPCS3 encodings) for colour/depth tiles and zcull |
| VRAM above ps3fb's 224 MB unused by ps3fb | drop ps3fb's hardcoded size |
| kexec from our Linux crashes the console | find the handover step that breaks it (would remove most XMB trips) |
| One boot hung at 11 s (netconsole?) | reproduce; delay netconsole until userspace if it recurs |

## Verification

| Phase | Done when |
|---|---|
| 1 | Kernel boots, the existing tools still draw, the GART region and second context come from the kernel, and takeover/release leaves the console intact. |
| 2 | `rsxdrm-smoke` passes every subtest (including 10k fences and 20 reloads) with no wedge, and a triangle drawn through the driver shows on screen. |
| 3 | Offscreen gears matches softpipe: no channel differs by more than 1 (the RSX interpolates colours slightly differently; agreed 2026-10-14). |
| 4 | Two interleaved GL processes render correctly. |
| 5 | `kmscube` runs (webcam). |
| 6 | glxgears runs in Xorg on the RSX (`glxinfo -B`, `xwd`, webcam). |

## Results (2026-10-14)

| Phase | Outcome |
|---|---|
| 1 | Done, rechecked 2026-10-14 with `gpu/tools/phase1-check.sh`: GART reserved, zram swap, `rsx-probe` draws, takeover refuses direct GPU access, the console redraws after release (webcam). Kernel patches local/0004 (GART boot reservation, `ps3rsx_gart=32M`), 0005 (ps3fb sharing API, ENTER/EXIT trylock), 0006 (gelic netpoll fix). |
| 2 | Done. `rsxdrm-smoke` passes every subtest; triangles drawn through the driver show on screen (kmscube, glxgears, webcam); the validator rejects vertex format type 0, which wedges the RSX. |
| 3 | Done. egl-clear, egl-tex and egl-short are byte-identical to softpipe. egl-tri: 89 of 12288 bytes 1 lower; egl-gears: 282 of 196608 bytes 1 lower, nothing more. Those are the hardware's attribute interpolation (about ±0.03 of an 8-bit step), seen only where the exact value lies just above a rounding midpoint. Getting there: the colour is now exported from fp32 registers (0009; fp16 export gave 2193 bytes in gears), and the softpipe reference uses 24-bit depth like the PS3 (with Z16 four silhouette pixels resolved their depth tie differently). |
| 4 | Done. Three concurrent GL processes give the same images as solo runs (`rsx-suite.sh multi`). |
| 5 | Done. modetest colour bars and kmscube at 60 fps (webcam). vblank is a frame-rate timer, not lv1's interrupt. |
| 6 | Done. Xorg modesetting + glamor on NV47; `glxinfo -B` reports NV47 with direct rendering, glxgears runs at 60 fps (`xwd`, webcam). |

What differed from the plan:
- Swap is zram only: the NFS swapfile deadlocked under memory pressure.
- Mesa needed 12 patches, not 1: PS3 handles (0001), vertex type 0 (0002), blit into GART (0003),
  big-endian blit fragment program (0004), no Z16 (0005), flushing an empty context (0006),
  host-order 8888 formats for gbm/X on big-endian (0007), no single-attribute inline vertices
  (0008), fp32 colour export (0009), textures in CPU-visible VRAM (0010), surfaces releasing
  their texture (0011), no swizzled 1-pixel-wide render targets (0012). Facts behind them: docs/RSX.md.
- The driver waits for an open primitive to close before switching channels.

docs/PLAN.md and README
get updated at each milestone.
