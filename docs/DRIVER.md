# The ps3rsx driver and the Mesa side

`ps3rsx` is our DRM driver for the PS3's RSX. It registers as "nouveau", so stock libdrm and Mesa's nv30 Gallium driver run on it, with a Mesa patch series (`packages/patched/graphics/mesa/patches/`) for the places where the RSX differs from a PC NV4x. It is finished and in daily use: Xorg with glamor, GL games, KMS with real vsync, a software context switch between processes, VRAM used as swap, and a GART that costs RAM only while the GPU uses it. All six phases of the original plan passed on 2026-10-14; the later work (cheaper submission, VRAM swap, dynamic GART) is described where it belongs below.

The GPU facts the design rests on are in [RSX.md](RSX.md). What is still unbuilt (tiling, Z-cull, vertex texture fetch, a real GPU reset) is in [backlog/graphics.md](backlog/graphics.md). Terms like lv1, GART, CMA, FIFO and validator are in the [glossary](README.md#glossary).

## How it fits together

```
 game / Xorg  --  Mesa nv30 (patched) --  libdrm  --  /dev/dri/renderD128, card0
                                                         |
                                  ps3rsx (module, registers as "nouveau")
                          GEM objects, validator, state shadow, ring, fences, KMS, vswap
                                                         |
                          ps3fb (kernel, patched: sharing API)  -- lv1 hypercalls --  RSX
```

The driver owns one lv1 GPU context (hardware channel 1) next to ps3fb's. Many software channels (one per Mesa context) share that one hardware context. The kernel copies every submission into its own ring, validates it, tracks the state each channel set, and restores a channel's state when the GPU switches between channels. ps3fb and ps3rsx take turns owning the GPU so that the text consoles keep working.

Why one hardware context: lv1 does no PGRAPH context switching, 3D state is shared between channels (E5), and the context limit is ps3fb's plus two, with an unexplained stall when allocating up to it (E1). One extra context is safe, so that is all we use. See [RSX.md, "Contexts"](RSX.md#contexts).

Every reboot of the PS3 needs the user (they set the OtherOS flag in the XMB, then Petitboot boots the disk). We never reboot it or touch its boot flag ourselves, and kexec from our Linux crashed the console, so it is not a way around that. Any graph exception wedges the RSX until a console reboot, which is why the driver validates everything.

## The kernel side

### Build and configuration

The kernel config (`packages/patched/kernel/linux/config/ps3.config`) turns on `CONFIG_DRM=m` (it selects sync_file, dma-buf and kcmp), `CONFIG_DRM_FBDEV_EMULATION` (it pulls in the KMS helpers, so no vkms is needed), `CONFIG_COMPACTION=y` (`ps3_defconfig` has it off), `CONFIG_DEBUG_FS`, `CONFIG_DYNAMIC_DEBUG`, `CONFIG_ZRAM=m` and `CONFIG_NFS_SWAP=y`. The driver uses no TTM, no DRM scheduler and no drm_exec; `drm_gem_lock_reservations` is enough. The module builds out of tree from `packages/walnut/drivers/ps3rsx/src/` and ships as `ps3rsx-modules-<release>` for each of our kernels ([PACKAGING.md](PACKAGING.md)). The boot command line needs `consoleblank=0`, because `ps3fb_blank` mutes the video through ps3av and would blank the KMS output. `ps3vram` is blacklisted (it cannot load while ps3fb owns all of VRAM), as are `ps3_lv1call` and `ps3_disp_manager` while ps3rsx is in use, since both allow arbitrary lv1 GPU calls.

### ps3fb patches (the sharing API)

The kernel patches `packages/patched/kernel/linux/patches/local/0003` (the probe modules use `ps3fb_gpu_handles`), `0004` (GART boot reservation), `0005` (the sharing API), `0006` (gelic netpoll fix) and `0007` (GART from CMA) carry everything the driver needs from ps3fb, so the driver itself is a plain module.

- **Info.** `ps3fb_gpu_get_info()` returns the memory handle, `ddr_lpar`, `memory_size`, the line length (`ddr_line_length`), the mode, frame 0's offset (`full_offset`), and ps3fb's VRAM reservation rounded up to 1 MB. The reservation covers the whole DDR area ps3fb's `smem_len` can use, because the number of frames depends on the mode.
- **Context.** `ps3fb_gpu_ctx_get(flags)` allocates our second lv1 context on the first call (under its own mutex), then caches it and hands back the same one forever: handle, the ctrl, driver-info and reports addresses, and the IRQ outlet. Module reloads therefore never allocate or free contexts, which matters because E1's stall is unexplained. The first call's flags stick until the next reboot. ps3rsx owns the ioremaps of those areas.
- **IO window.** `ps3fb_gpu_iomap(io, lpar, size, flags)` wraps the iomap and records ours, so `ps3fb_shutdown` and `.remove` can undo them and free our context before ps3fb's `lv1_gpu_memory_free`. That works even if ps3rsx was wedged. `suppress_bind_attrs` keeps a sysfs unbind from pulling ps3fb out from under ps3rsx.
- **Exceptions.** `ps3fb_gpu_set_exception_hook(fn, data)` is called from `ps3fb_vsync_interrupt` (hard IRQ) with `v1` and a pointer to `graph_exception_info`. The hook only records the exception and schedules work. Its function and data are published as one struct pointer (`WRITE_ONCE`), and setting it to NULL calls `synchronize_irq(ps3fb.irq_no)`, so unload is safe. `ps3fb_gpu_exception_seen()` is a sticky counter, because `GPU_USER_DEBUG` is cleared by every `restore_state`.

### Takeover: ps3fb and ps3rsx take turns

`ps3fb_gpu_takeover(bool)` moves the GPU between ps3fb (text consoles) and ps3rsx. They cannot share it because they share PGRAPH state.

The lock order is `ps3fb.own_lock` outermost, then `console_lock`, then `ps3_gpu_mutex`. Takeover never takes the fb_info lock, which already has an ABBA ordering with `console_lock`, and nothing takes `own_lock` while holding `console_lock`. ENTER and EXIT take `own_lock` and check `gpu_owned` before `StartStopTask`, which closes a race where ENTER unmapped `fifo.ctrl` under a polling takeover.

Taking over means: take `own_lock`, then `console_lock()` (every sync caller holds it), then `ps3_gpu_mutex`, and set `gpu_owned`. While it is set, the whole of `ps3fb_sync_image` (blit and both flips) does nothing, and the IRQ handler does not wake ps3fbd, so `StartStopTask` is not used and `fifo.ctrl` stays mapped. Then takeover appends `SET_REFERENCE cookie` to ps3fb's own ring (reusing `fb_blit`'s wrap check), kicks it, and polls ctrl+0x48. ps3fb itself never emits a reference, and GET is not completion. It fails if a ps3gpu user is inside ENTER.

While the GPU is owned, these return -EBUSY: ps3fb's direct-GPU interface (ioctls 1860 to 1901 for ENTER, EXIT, attribute and cursor, and the `/dev/ps3gpu_*` mmap), and mode changes in `set_par`, so `ps3av_set_video_mode` cannot change the mode under KMS (`check_var` keeps passing, since fbcon only logs `set_par` failures). `ps3fb_blank` becomes a no-op, because `ps3av_video_mute` would blank the KMS output.

Releasing: the caller has already waited for its last ref. Then, under `console_lock` and `ps3_gpu_mutex`, ps3fb runs `ps3gpu_restore_state()`, a full `ps3fb_sync`, and clears `gpu_owned`.

`console.c` applies this to the VT switches. ps3rsx owns the GPU while a DRM master (X) is active or while master-less clients (offscreen GL) are submitting. Otherwise ps3fb gets it back and draws the console:

- `master_drop` (VT switch away, X exiting): new submits wait, except from a channel with a primitive open (ps3fb must not touch the GPU mid-primitive). Once idle, ps3fb gets the GPU.
- `master_set` (VT switch back): ps3rsx takes the GPU, forgets what PGRAPH held (ps3fb changed it, so the next submit restores its channel's whole state), and shows the master's frame again.
- With no X at all, the first submit takes the GPU and 2 s of idle gives it back.
- While an X server is open but switched away, its clients wait for its return.

Kernel patch `local/0043` frees 8 of the console's 9 MB of XDR while ps3rsx owns the GPU. It is in the booted default kernel `.472` behind `ps3fb.shadow_release=1`, off by default and not tried yet ([KERNEL-MEMORY.md](KERNEL-MEMORY.md)).

### The GART

The GART is main memory the GPU reaches through our context's IO window in 1 MB IO pages. It has two sources:

- `ps3rsx_cma=<size>`, the setting in use: a CMA area (kernel patch `local/0007`; `ps3rsx_cma=64M` on the PS3). IO pages are backed on demand, one 1 MB chunk each, when a buffer object first needs them. A chunk goes back once it has been unused for a while (2 s) and the GPU has passed every fence that could still touch it. Between uses the area holds ordinary movable pages, so the GART costs RAM only while the GPU uses it. `rsxdrm-smoke gart` got 56 MB under memory pressure with the data correct. Measured games use 6.7 to 12.8 MB of it ([MEMORY.md](MEMORY.md)).
- `ps3rsx_gart=<size>`: the earlier fixed boot reservation (`local/0004`; 32 MB in the phase-1 check, 1 MB aligned), all backed for good. It comes from highmem, which is added to memblock before the reservation code runs, so it fits next to ps3fb's 9 MB. Order-8 allocations after boot are unreliable on 200 MB, which is why it is reserved at boot.

The first 2 MB (the reset program and the ring, below) are one contiguous allocation backed for good, because the ring is written as linear memory. Every IO page without a chunk is mapped to a scratch chunk (aliasing works, E2), so a stray GPU fetch never faults, and a fault would wedge the GPU. Chunks are not contiguous in kernel memory, so CPU access goes through `ps3rsx_gart_ptr()` one chunk at a time. IO pages that pending fences still use are never unmapped. GART offsets are IO addresses relative to 0xfeed0001, sub-allocated at 4 KB.

### Memory and buffer objects

There is no TTM. Buffer objects are pinned, each with a `dma_resv`, and presumed offsets never change.

- **VRAM, CPU-visible:** a `drm_mm` from above ps3fb's reservation (from `get_info`) up to 224 MB. Dumb and scanout buffers live here, and so does the VRAM swap's pool.
- **VRAM, GPU-only:** 224 MB up to 254 MB (`memory_size` 0xfe00000). These have no MAPPABLE flag and `map_handle=0`: a CPU mapping there could make lv1 refuse the page table entry, and `ps3_hpte_insert` then BUG()s. Mesa stages through GART anyway. Whether the CPU can map this area is the open "may crash" probe in [backlog/graphics.md](backlog/graphics.md).
- **GART:** as above.
- **mmap:** GART cached, the 64 KB reports area noncached (it is the notifier GEM object), VRAM noncached.
- **Sizes:** `GEM_NEW` sizes round up to 4 KB, and `domain=0` means VRAM or GART. FLINK and PRIME work.
- **Zeroing:** every new GART buffer is zeroed, so no other process's data reaches userspace. A GART buffer made per frame is costly beyond that: its mmap faults in one hash table entry per 4 KiB page through lv1 and its munmap takes one lv1 call per page, which is why Mesa reuses mapped staging buffers (0036, 0040) instead. `clear_dcbz=1` (module parameter, off by default, writable at run time) zeroes with `clear_page` (dcbz, no read of the line first) instead of memset ([HOTPATHS.md](HOTPATHS.md#soft-masked-time-and-the-staging-buffer-per-frame)).

### Ioctls (`abi16.c`, `nvif.h`)

The DRM name is "nouveau", version 1.3.1 (exactly Mesa's minimum), with `DRIVER_GEM|DRIVER_RENDER`. It is a platform device (`platform_device_register_simple("ps3rsx")`), so libdrm uses its MODALIAS fallback instead of OF data.

- **GETPARAM:** `FB_SIZE` and `AGP_SIZE` are the managed sizes; `CHIPSET_ID` is 0x47; `PCI_VENDOR` is 0x104d (Mesa's PS3 check); `BUS_TYPE` is PCIE; `PTIMER_TIME` is nanoseconds from the timebase. A private `PS3_ABI` param (0x1000, value `PS3RSX_ABI_VERSION`) is what the patched Mesa queries. `CHANNEL_ALLOC` is refused until it has been called, so stock Debian Mesa (pulled in by `mesa-utils`) fails cleanly at init instead of submitting unpatched streams. Everything else, `HAS_VMA_TILEMODE` included, returns -EINVAL.
- **NVIF:** it needs its own `unlocked_ioctl` path, copied from `nouveau_drm_ioctl`, because the argument size varies per request. It handles NEW of the device (route 0), the device-info MTHD (0x47, CURIE, PCIE), and NEW and DEL of engine objects (route 0xff, token = channel, owner ANY), including NV01_NULL (0x30, handle 0). Mesa's subchannel DEL never reaches the kernel, so objects are freed at `CHANNEL_FREE`; `GROBJ_ALLOC` is the same bookkeeping. Bookkeeping is per channel, because handles repeat across channels, and it is keyed on the PS3 handles: the patched Mesa passes them at allocation time (objects, notifiers, `nv04_fifo.vram` and `gart`), so the command stream carries them with no further Mesa change. The kernel accepts only the known PS3 handle set.
- **CHANNEL_ALLOC:** a software channel on the shared hardware context. `pushbuf_domains` is GART and `notifier_handle` is the reports-area GEM object.
- **NOTIFIEROBJ_ALLOC** hands out per-channel slices of the one reports area: the fence (`0xbeef1e00`) gets 0x20 + 16·channel; the query notifier (`0xbeef0351`) gets a 4 KB slice at 0x1400 + 0x1000·channel (at most 14 channels); `0xbeef0301` gets 0x1000. Mesa asks for 32 bytes (fence), 32 bytes (ntfy) and 3968 bytes (query, 4096 - 128), and the kernel's limits accept those.

### Submission (`submit.c`, `exec.c`)

`GEM_PUSHBUF` copies each push into a kernel ring instead of CALLing user memory. The copy avoids validate-then-modify races, so the validator and the shadow see exactly what the GPU runs. On big-endian Mesa used to push every vertex inline, so draws were long 0x1818 bursts in the pushbuf; the RSX now fetches vertex data itself (see the Mesa side), but pushes can still be large.

- **Ring:** 2 MB (two GART chunks; `RING_START` 0x1000, `RING_END` 0x200000). A submit is at most about 512 KB, since Mesa's push BOs are 512 KB; larger submits are rejected.
- **Per submit:** look up the BOs, copy and validate the pushes, apply relocations to the copy if `presumed` is ever invalid (offsets never move, so it should not be), then append `SET_REFERENCE seq` and write PUT.
- **Wrap:** a JMP only between submits (a submit is at most 512 KB and the ring 2 MB), so no mid-push decoding is needed. Ring space is counted from ref.
- The ioctl returns `suffix0 = suffix1 = 0` (no RETURN needed) and `vram_available`/`gart_available`, and honours `NOUVEAU_GEM_PUSHBUF_SYNC`. The first `nr_push=0` call must succeed.
- **Fences:** a `dma_fence` per submit, attached to the BOs it references but not to BOs that are only push sources (Mesa maps those at every buffer switch). An hrtimer polls ref (HZ=250, PREEMPT_NONE), and it runs fast only while someone waits (this took timer interrupts from 4000 to 1540 a second and fixed ClassiCube stutters). `CPU_PREP` polls ref directly while waiting. Expect heavy churn of 4 KB GART BOs from Mesa's per-fence BO.
- **Wedge detection:** an exception IRQ, or no ref progress for 2 s with work pending. On a wedge the driver dumps the exception block to netconsole, returns -EIO, and errors all fences.
- **Module parameters:** `trace=1` dumps each submit to the kernel log (small tests only), `dry_run=1` validates but sends nothing, `ctxsw` (default on) switches the software context.

### The validator

`validate.c` is on by default. Because any graph exception wedges the GPU until a console reboot, it decodes every user push before the GPU sees it:

- every method of a multi-count burst is checked, including non-incrementing headers;
- JMP, CALL and RETURN are refused outright;
- method-0 binds accept only hypervisor object handles;
- the method allowlist is a table built from data: the per-class list in [MESA-METHODS.md](MESA-METHODS.md#4-per-class-method-list) as (class, base, stride, count, max burst). Indexed ranges are expanded: 16 texture units at stride 0x20, 16 vertex formats and buffers, all immediate-attribute formats, VP 0xb80 to 0xbfc, and constants 0x1f00 to 0x1f7c. Unknown methods are logged and rejected.

Only some data words are checked: the handle slots (against {feed0000, feed0001, 66604200, 66606660, 66626660, 0}), the location bits (1 or 2), and at the trigger methods (0x1808 begin, 0x1d94 clear, 0x328, 0x40c) the shadowed offset, pitch, clip and format against the owning BO's bounds. A 0x1d6c release must be 16-byte aligned and inside the channel's fence slot. 0x1800 and 0x1e98 must be type 1 and inside the channel's query slice. The VP load pointer plus count must stay at or below 512 slots, and constants at or below 468.

Known rejections and why: vertex format type 0 (a graph exception), vertex format type 7 (UB256) with a size other than 4 or 0 (3 components raised cause 0x106 with Mesa 0043; since ps3rsx 0.545~dev, [RSX.md](RSX.md#errors-and-what-wedges-the-gpu)), swizzled render targets 1 pixel wide (cause 0x106), and, since ps3rsx 0.249 (commit 8e99b76), 3D texture offsets that are not 128-byte aligned (TEX_OFFSET of fragment units 0 to 15 and vertex texture units 0 to 3; 0x105). Mesa never sends such an offset after its patch 0014. The vertex texture methods 0x900 to 0x97c are still accepted without data checks.

A rejection keeps that one push off the GPU, nothing more. Mesa doesn't know the push was dropped and leaves out state it believes the GPU already holds (vertex formats it sent before, for one), so the channel's later draws run on the GPU's older state. Every word of that state passed the validator, but a draw can then fetch its vertices with another draw's layout. A run that logs `ps3rsx: rejected push` is no measurement and should be stopped; it is not a way to test a known-bad stream on the hardware.

Version 0.249 (commit 9e2a3b9) made the checks cheaper (5 to 16% of each game's CPU in perf): a fast-path table gives one lookup per word and cuts inline data cost about 13 times on the host. It also fixed a bug where a rejected push in a multi-push submit left the earlier pushes' object bindings in the validator's state, which was the route to a 0x109 wedge.

### The software context switch (the state shadow)

Two processes rendering means the GPU's 3D state must follow the channel. A Mesa-side "re-emit everything on each kick" is not enough: Mesa has no start-of-pushbuf hook, and copies continue across flushes (the M2MF DMA in and out is set once before a loop that can flush, `nv30_transfer.c`). So the kernel does it.

**Shadow.** `validate.c` and `shadow.c` keep a per-channel shadow of the state methods of every subchannel: 3D, plus M2MF, SF2D, SWZ and SIFM state with their 0x180 DMA, SIFM 0x2fc, and the method-0 binds. Indexed methods are emulated: vertex-program data goes through the 0xb80 to 0xbfc window with auto-increment from load pointer 0x1e9c (a 512-slot shadow), and constants go through 0x1efc plus 0x1f00 to 0x1f7c. The load pointers are restored afterwards.

**Never shadowed** (they have side effects): 0x100, 0x104, 0x110; the 0x1d70 and 0x1d74 releases; 0x1710 to 0x1718, 0x1fd8, 0x17c8, 0x1800; the draw and inline-data range 0x1808 to 0x1824; immediate vertex attribute 0 writes (they emit a vertex); the 0x1d94 clear; and the copy triggers, M2MF 0x328 BUFFER_NOTIFY (the last word of the OFFSET_IN×8 burst; 0x310 OFFSET_OUT is state) and SIFM 0x40c (inside the SIZE×4 burst).

**Restore.** The kernel keeps a second shadow of what PGRAPH currently holds. When the next submission comes from another channel, it emits only the words that differ (usually 1 to 2 KB, worst case about 24 KB), then invalidates the texture and vertex caches (0x1fd8, 0x1710 to 0x1718). The driver never restores inside BEGIN/END: Mesa splits long inline draws across pushes, so ps3rsx waits for the open primitive to close before it switches channels. The restore is skipped while the same channel keeps the GPU. One known limit: the ZPASS accumulator cannot be restored, so occlusion queries running in two processes at once mix their counts.

ps3rsx 0.249 also writes the shadow per burst and keeps dirty bitmaps, so a channel switch restores and takes only what changed (about 14.6 down to 1.3 µs on the host). `shadow_fast` (version 0.250, module parameter, default off, flip it at run time with `echo 1 | sudo tee /sys/module/ps3rsx/parameters/shadow_fast`) replays bursts by runs of methods and walks the restore bitmaps a word at a time. It leaves identical state and targets the ioctl's 5.4% of a game's CPU. It is built and fuzzed against the slow path in userspace (`packages/walnut/drivers/ps3rsx/test/`: 38 million bursts and 3.8 million submits identical on x86, under ASan/UBSan, and on big-endian ppc64; about 60% fewer instructions in `shadow_burst`), but it has **not been run on the PS3**. Its same-boot A/B is scheduled in [TESTS.md](TESTS.md); the profile that motivated it is in [HOTPATHS.md](HOTPATHS.md).

### KMS (`kms.c`)

A `drm_simple_display_pipe` with a virtual connector and the single current mode from `ps3fb_gpu_get_info`. ps3rsx does not set up DRM fbdev; fbcon stays on fb0.

- BGRX8888 matches the RSX's X,R,G,B memory order on big-endian, and `quirk_addfb_prefer_host_byte_order` is set. XRGB8888 is also accepted (for kmscube, whose colours then come out off).
- `addfb` rejects any pitch other than ps3fb's `ddr_line_length`. A scanout buffer may sit anywhere in VRAM, including above 224 MB (flips there work).
- Flips go to both heads, as ps3fb's do, with `lv1_gpu_display_flip` from our context, at `full_offset`-relative addresses. Vblank comes from our context's vsync interrupt (E7), head A like ps3fb; `vblank_irq=0` brings back a frame-rate timer, which drifted against the real vsync and sent flip events before the flip had latched (clients drew into the buffer still being scanned out). Flip events wait for the flip to latch, which ended the half-redrawn frames seen in ClassiCube.
- On unload the driver flips back to ps3fb's frame 0 and releases the GPU, so fbcon returns.
- DPMS off blanks with a ps3av video mute (like ps3fb's blanking) instead of showing ps3fb's console, and the hardware cursor comes back with the display (f30b505). The video unmute is always sent, and a reflip with an active CRTC unmutes it: before a1f1b91 and 8baa747 (ps3rsx 0.219), every X after the first started black because the cached mute flag was stale.
- PRIME uses the stock GEM helpers.

### VRAM swap (`ps3vswap0`)

The PS3 has 256 MB of RAM and 256 MB of VRAM, and X plus a game use well under half the VRAM. `vswap.c` is a block device that keeps each 4 KB swap page either in a VRAM slot or at the same place on a backing partition (the USB disk's swap partition), so the swap layer sees one device. Swap on the PS3's internal HDD is out of the question: `ps3da`, `ps3db` and `ps3dc` are GameOS's raw disk, `dev_hdd0` and the 2 GB `dev_hdd1` cache, and `mkswap` on any of them would destroy GameOS data. An NFS swapfile was tried and deadlocked the PS3 under memory pressure (`Write error -12 on dio swapfile`); zram was the first swap, and the VRAM swap now sits above the disk in priority.

- Writes go to VRAM while the pool has room (1 MB chunks from the top of the CPU-visible VRAM heap, at most `vswap_vram_mb`, default 96), else to the disk. Discards (`swapon --discard=pages`) free slots.
- Games come first. When a buffer object finds no VRAM, `ps3rsx_vswap_shrink()` writes the pages of the emptiest chunks back to the disk and hands the chunks over.
- The VRAM holds the recently swapped. When the pool is nearly full, a background job moves its oldest chunk to the disk; pages that sat in swap longest without being read back are the cold ones, and reads free their slots through discards.
- Writes are PPE stores (8-byte, about 124 MB/s). **Swap-ins and write-backs are copied by the GPU** (`kcopy.c`): the M2MF copies VRAM into a pinned 1 MB GART bounce at about 600 MB/s when the GPU can be had without waiting. Otherwise the PPE reads VRAM itself at 10 MB/s. The GPU only ever reads the VRAM slots, so a failed, late or wedged copy loses nothing: the PPE reads the same slot. Parameters: `vswap_gpu` (default on), `vswap_gpu_min` (swap-in pages per request from which the GPU copies even when busy), `vswap_size_mb`.
- Set up from userspace by the rootfs `ps3-vswap.service`: write the backing partition's path to `/sys/block/ps3vswap0/backing`, then `mkswap` and `swapon` at a priority above the disk's.

`kcopy.c` is the kernel-submitted M2MF copy between client submits. It never waits for anything a submitter might hold while it reclaims memory (a submitter allocates under `exec_lock`), so every lock in it is a trylock and every failure means "use the PPE". The GPU must already be ours (`own_lock`, `gpu_owned`, no console handover pending), with no primitive open on any channel, context switching on and not a dry run. The copy uses Mesa's M2MF object on subchannel 6 (Mesa uses 2 to 5 and 7), records every word in the hardware shadow and makes the next client submit restore its own state, so no client ever sees our M2MF settings. There is no `dma_fence`: a bare `SET_REFERENCE`, polled. Requests run on the driver's own work queue with a fresh stack, because blk-mq can run `queue_rq` inline from a plug flush in direct reclaim, which was already deep (an IRQ found under 4 KB of stack left).

Measured (task #13): a working set that fits the VRAM tier swaps in about 10 times faster (passes of 0.2 to 1.2 s against 4.4 to 17.7 s); the GL suite passed three times in a row while about 92,000 kernel copies interleaved with its clients; zero fallbacks. OpenArena did not improve, because its threads no longer block on swap-ins; what remains is file I/O on the USB disk ([STALLS.md](STALLS.md)).

## The Mesa side

Mesa's nv30 driver runs on ps3rsx with a patch series on Debian's Mesa source ([PACKAGING.md](PACKAGING.md) has the patch workflow; the earlier cross-build pipeline is gone). The target is Mesa 26.2.4.

The PS3 is detected in `nouveau_screen_init`, before the channel is allocated: chipset 0x47, `nouveau_getparam(PCI_VENDOR) == 0x104d`, and the `PS3_ABI` getparam (0x1000) succeeds. The result is `nouveau_screen.is_ps3`, and every change is gated on it. Handles are substituted at allocation time, so the streams carry the PS3 handles automatically, because Mesa writes the handles it allocated.

What the series does (numbers are the files in `packages/patched/graphics/mesa/patches/`; the full list is `packages/patched/graphics/mesa/patches/series`). The series was renumbered when it was folded into Debian's source package, so older commit messages use other numbers.

| Patch | What | Found by |
|---|---|---|
| 0001 | detect the PS3, use its DMA handles (`nv04_data.vram/gart` = 0xfeed0000/0xfeed0001), smaller push buffers (they live in the small GART, and the kernel copies each submission) |  |
| 0002 | `nouveau_mm`: return NULL when no slab can be allocated (out of GART) |  |
| 0003 | push buffer fixes in the context: room for the fence kick, a context that never queued anything gets space before the kick, destroy survives a failed init, per-context count of upload staging in flight |  |
| 0004 | RSX screen quirks: no Z16 (it mis-renders next to a 32-bit colour buffer), the semaphore value pre-swizzled (bytes 0 and 2 swapped) |  |
| 0005 | the RSX fetches big-endian vertex and index buffers itself; no vertex type 0; a lone inline attribute is pushed twice |  |
| 0006 | query notifier offset: QUERY_GET (0x1800) and RENDER_ENABLE (0x1e98) add `query->offset - 0x1400` so each channel's slice lands in its own place; timer queries stay on |  |
| 0007 | texture uploads that fit the GART (bands of at most 1 MB, at most 4 MB of staging in flight per context, which fixed a burst crashing Xorg), 1-pixel-wide render targets stay linear (0x106), fragment program constants and the blit's program halfword-swapped, a GART blit destination stays in GART |  |
| 0008 | 32-bit colour exports from fragment programs (`NV40_FP_EXPORT32=0` turns it off) |  |
| 0009 | dri/gbm/loader: 8888 formats as host-order words |  |
| 0010 | fixed-function fog distance as -Ze (a sky was fogged white in some directions) | [ClassiCube](../gamedb/games/classicube/) |
| 0011, 0032, 0038 | S3TC (DXT1, then DXT5) compression through libspeq on the SPEs, and at upload for every game ([SPU.md](SPU.md)) |  |
| 0012 | L8A8 and R8G8 texels in big-endian order |  |
| 0013 | thin (1-pixel) mip levels as linear targets |  |
| 0014 | the 3D blit source stays 128-byte aligned | [Neverball](../gamedb/games/neverball/) |
| 0015 to 0017, 0019 to 0031 | CPU-cost and submission patches: fewer fence and kref operations, no relocations on the PS3, no redundant cache invalidates, vertex-format, constant-buffer and texture-unit rewrites, submit when the GPU has caught up, fragment programs fetched from GART |  |
| 0033 to 0040 | 8-bit vertex attributes to floats, fence-buffer recycling, query sleep instead of spin, staging reuse (0036 small uploads, 0040 big ones: `NV30_STAGING_POOL_MB`), limits on nouveau's system-memory copies, compressed upload for every game, the shader disk cache | [BananaBread](../gamedb/engines/cube2/investigations/2026-10-18-byte-normals.md) (0033, 0035), [ClassiCube](../gamedb/games/classicube/) (0036), [OpenArena](../gamedb/games/openarena/investigations/2026-10-18-opengl2-freezes.md) (0037), [OpenTTD](../gamedb/games/openttd/investigations/2026-10-19-staging-buffer-per-frame.md) (0040) |
| 0045 | fragment TXB emitted as TEX, the bias dropped: the RSX's TXB samples wrong whatever the bias (a dark, flat 3D view); `NV30_TXB_HW=1` keeps the hardware TXB for tests | [Warzone 2100](../gamedb/games/warzone2100/investigations/2026-10-20-dark-3d-view.md) |
| 0046 | a query whose report slot is taken back keeps its result. The notifier holds 124 reports; when they ran out, the oldest report object was freed while its query still pointed at it, so the query's next result check read freed memory and freed it again (wrong occlusion results, and a segfault in `nouveau_heap_free` from `nv30_query_object_del`). Upstream nv30 has the same code. Host test: `packages/patched/graphics/mesa/tests/query-steal-test.c` (`tools/run-tests.sh --only mesa-query-steal`) | [Sauerbraten](../gamedb/games/sauerbraten/investigations/2026-10-24-online-crashes.md) |
| 0047 | an upload buffer that cannot be had is not written through: `st_setup_current()` copied the current vertex attributes into a NULL `u_upload_alloc()` result (after "out of GART": a NULL write in a memcpy); `u_upload_alloc()` leaked the previous buffer at every failure and could hand a stale one back; nv30 reads an unbound zero-stride attribute as zero | [Sauerbraten](../gamedb/games/sauerbraten/investigations/2026-10-24-online-crashes.md) |
| 0048 | nouveau buffer allocation failures fail the call: no 4 MiB slab means a buffer of the request's own size, VRAM buffer maps without a staging area fail, a discard or invalidate keeps the old storage when there is no new one, a migration between VRAM and GART that finds no room keeps its storage. Host test: `packages/patched/graphics/mesa/tests/alloc-fail-test.c` (in `mesa-query-steal`) | [Sauerbraten](../gamedb/games/sauerbraten/investigations/2026-10-24-online-crashes.md) |
| 0049 | out of GART, nothing crashes or is lost silently. A context always has a current fence: the next one is made before the old one goes, its 4 KiB wait object comes after waiting for the GPU's submitted work, else the fence goes without one and is polled (before: `nv->fence` NULL after a failed wait-object allocation, a segfault, and the batch of that kick dropped). A texture upload with no staging buffer waits for the GPU, drains the staging pools and tries again, else goes through malloc memory and the CPU into the CPU-mapped VRAM (before: lost, the texture kept old pixels). Buffer maps wait and retry for their staging area; a fragment program or the blit's program with no buffer keeps the bound one instead of writing through NULL. Host tests: `alloc-fail-test.c` (fences) and `packages/patched/graphics/mesa/tests/transfer-cpu-test.c` (texture transfers), in `mesa-query-steal` | [SuperTuxKart](../gamedb/games/supertuxkart/investigations/2026-10-26-fence-segfault.md) |
| 0050 | GL_POINT_SMOOTH points are round (`NV30_POINT_SMOOTH`, on by default; 0: the old squares). nv30 ignored `point_smooth` and NV3x/NV4x have no known smoothing switch, so smooth points were squares on the PS3. Points drawn with it run a variant of the bound fragment program (`nv30_point_smooth.c`, a TGSI transform like `nir_lower_point_smooth`): the point sprite's replaced coordinate in a free texture coordinate, the fragment killed outside the circle, alpha times a one-pixel edge's coverage, about 20 instruction slots more. A program with no VRAM buffer is uploaded at validate (the variant is translated when made; ps3.860 bound it with a NULL buffer when it had no constants, a segfault). Triangles keep the plain program even with GL_POINT_SMOOTH left on. Host test: `packages/patched/graphics/mesa/tests/point-smooth-test.c` (`mesa-point-smooth`: the variant on Mesa's TGSI interpreter and its NV40 translation); PS3 check: `packages/walnut/tools/ps3-gpu-tools/mesa-tests/gl-pointsmooth` | [Yamagi Quake II](../gamedb/games/yquake2/investigations/2026-10-26-point-smooth.md) |

Several of these have switches that default off or are neutral when measured; [HOTPATHS.md](HOTPATHS.md) and [games/README.md](games/README.md) say which. The facts behind patches 0004 to 0014 are in [RSX.md, "Mesa, KMS and X"](RSX.md#mesa-kms-and-x-on-the-driver). "Found by" links the game whose run showed the bug or the cost, to its investigation where there is one; the list of driver bugs found by games is in [games/README.md](games/README.md).

One more PS3 rule lives in `nv30_miptree.c`: the scanout pitch is only 64-byte aligned, because the display pitch is fixed by ps3av at width×4 and Mesa's power-of-two padding (7680 to 8192 at 1080p) would display sheared. Tiling and Z-cull are not used yet (`HAS_VMA_TILEMODE` returns -EINVAL).

### Build and test tools

`packages/walnut/tools/ps3-gpu-tools/mesa-tests` (installed with the Mesa debs) holds `egl-clear`, `egl-tri`, `egl-tex` and `egl-gears`, which render offscreen (surfaceless EGL, or `EGL_RENDER_NODE=/dev/dri/renderD128` for the device platform) and print `GL_RENDERER` and a checksum; with an argument they write a PPM. Each runs once with `LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=softpipe` (the reference) and once without (nouveau). The GL feature tests are `gl-texfmt`, `gl-genmip`, `gl-mipread` and `gl-texsub`. The driver tests are `ps3-gpu-tools/rsxdrm-*.c`, raw ioctls:

- `rsxdrm-info`: GETPARAM, NVIF, `drmdevice`.
- `rsxdrm-smoke nop|m2mf|clear|tri|fence|reload|quirks|flip|gart`. `fence` runs about 10,000 iterations of a 0x1d70 release followed by a ref wait, then checks the label had landed when ref arrived (Mesa's fence order). `reload` runs 20 rmmod/insmod cycles. `quirks` covers the M2MF NOP, RT_ENABLE values outside libgcm's set, and vertex format type 0. `flip` flips both heads to an offset in our `drm_mm` range and then to one at 224 to 254 MB.

Results are checked by readback, and by webcam (`tools/webcam-grab.sh`) after a flip.

## Phase results (2026-10-14)

The plan had six phases. All passed on the day.

| Phase | Done when | Outcome |
|---|---|---|
| 1. Kernel (one batched rebuild, one reboot) | boots, existing tools still draw, the GART and second context come from the kernel, takeover and release leave the console intact | Done, rechecked with `packages/walnut/tools/ps3-gpu-tools/scripts/phase1-check.sh`: GART reserved, `rsx-probe` draws, takeover refuses direct GPU access, the console redraws after release (webcam). Kernel patches local/0004, 0005, 0006 |
| 2. `ps3rsx` render driver | `rsxdrm-smoke` passes every subtest with no wedge, a triangle drawn through the driver shows on screen | Done. Triangles show on screen (kmscube, glxgears, webcam); the validator rejects vertex format type 0, which wedges the RSX |
| 3. Mesa patch, offscreen GL | offscreen gears matches softpipe, no channel off by more than 1 (the RSX interpolates colours slightly differently; agreed 2026-10-14) | Done. egl-clear, egl-tex and egl-short are byte-identical. egl-tri: 89 of 12288 bytes 1 lower. egl-gears: 282 of 196608 bytes 1 lower, nothing more. That is the hardware's attribute interpolation (about ±0.03 of an 8-bit step), seen only where the exact value lies just above a rounding midpoint |
| 4. Software context switch | two interleaved GL processes render correctly | Done. Three concurrent GL processes give the same images as solo runs (`rsx-suite.sh multi`) |
| 5. KMS | `kmscube` runs (webcam) | Done. modetest colour bars and kmscube at 60 fps |
| 6. Xorg and glxgears | glxgears runs in Xorg on the RSX | Done. Xorg modesetting with glamor on NV47; `glxinfo -B` reports NV47 with direct rendering; glxgears at 60 fps (`xwd`, webcam) |

Two details of phase 3: the colour is exported from fp32 registers (patch 0008; the fp16 export gave 2193 differing bytes in gears), and the softpipe reference uses 24-bit depth like the PS3, because with Z16 four silhouette pixels resolved their depth tie differently.

What differed from the plan: swap is not zram (the NFS swapfile deadlocked; zram came first and the VRAM swap followed); Mesa needed twelve patches by the end of the phases, not one; and the driver waits for an open primitive to close before switching channels.

Phase 6 needed glamor, which requires GL 2.1, `GL_ARB_texture_border_clamp` and VAO (xserver `glamor.c`); modesetting offers DRI2 and DRI3 only with glamor, so without it glxgears would run on softpipe. nv30 reports GLSL 1.20, so glamor takes its slower, less tested GLSL 1.20 paths. `AccelMethod none` is only a KMS sanity check, and `prefer_shadow` suits the 10 MB/s CPU reads from VRAM. xf86-video-nouveau is not an option: it needs a PCI device and NV04 2D objects the hypervisor does not provide.

## Later changes to the driver

- ps3rsx 0.219 (a1f1b91, 8baa747): the video unmute fixes above.
- ps3rsx 0.249 (9e2a3b9, 8e99b76): the validator fast path, shadow per burst with dirty bitmaps, the texture-offset check.
- `shadow_fast` (0.250, f8f1608): built and fuzzed, default off; run on the PS3 in batch ssdroot2 (ClassiCube, BananaBread, X-Moto, x2): no gain resolved.
- The dynamic GART from CMA, the VRAM swap's GPU copies (`kcopy.c`), the vsync interrupt for vblank, and the fence timer that polls fast only while someone waits.
- Done around the driver and recorded in [PLAN.md](PLAN.md): GPU vertex fetch (ClassiCube 6.5 to 60 fps), Mesa fixes (fog, banded uploads, staging limits), game mode and the VRAM swap.

Not tried yet: the probe above 224 MB, a hardware context save area, the context-limit stall, exception recovery without a console reboot, 64 KB IO pages, tiling and Z-cull, lifting ps3fb's size, kexec, and an lv1 "user command" completion interrupt. See [backlog/graphics.md](backlog/graphics.md).

Older versions and the dated history: [archive/DRIVER-PLAN.md](archive/DRIVER-PLAN.md).
