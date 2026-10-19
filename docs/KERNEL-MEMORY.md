# Kernel RAM cost on the PS3

Where the kernel's share of the PS3's 240 MiB goes, what each config or boot change saves, and the
console frame that ps3fb can give back while X runs. The release kernel (the `ps3-release`
flavour, default since kernel .472) carries the measured config cuts: `MemTotal` is +8.7 MB over
the debug kernel. The console frame release (local/0043, 8 MiB) is in .472 too, but its switch is
off and its PS3 checks (section 6) have not run. The games' side of memory is
[MEMORY.md](MEMORY.md); changes not made are in [backlog/memory.md](backlog/memory.md).

How the numbers were obtained (2026-10-15, Linux 6.12.112 with our patches; the kernel is now
Debian's 6.12.111 with the same patches, and the release kernel's `MemTotal` was measured again):

- **boot log**: `dmesg` of the running PS3 (6.12.112-ps3-debug-202610151803), read only.
- **measured, variant**: `vmlinux` variants built from the same patched source (the 15:03 build's
  tree, copied to `build/kexp/src`; one `O=` tree per variant, flags in `build/kexp/cfg/*.config`,
  runner `build/kexp/run-variant.sh`, same builder image). The unmodified config reproduces the
  shipped `vmlinux` section for section, so the deltas are real. "Resident" is the sum of the sizes
  of all alloc sections outside the init range, ignoring the 16 MiB padding (which gets its own row).
- **computed**: from the named source file, or from `size`/`nm` on the build output.
- **booted**: the release flavour, measured on the PS3 (section 4).

## 1. What the 35 MB "reserved" is

The debug kernel's boot line: `Memory: 143960K/245760K available (6536K kernel code, 972K rwdata,
9848K rodata, 3244K init, 496K bss, 35012K reserved, 65536K cma-reserved)`.

It is printed from `page_alloc_init_late()` (mm/mm_init.c:2325), after SMP bring-up and before the
CMA area is activated. `reserved` = physpages - totalram - totalcma (mm/mm_init.c:2650): everything
memblock still holds at that point, CMA excluded. Checked against the running system: MemTotal
213992 kB = 245760 - 35012 + 3244, the 3244K being the init memory freed at 5.6 s ("Freeing unused
kernel image"). So the debug kernel's steady-state reserve is **31768K**. The release kernel's
`MemTotal` is 222716 kB (kernel .472, in the bench snapshots of 2026-10-18), a steady-state reserve
of **23044K**.

| Item (debug kernel) | KB | Source |
|---|---:|---|
| Kernel text (`_stext`..`_etext`) | 6536 | `System.map`: `_etext` = 0x662000 |
| Kernel rodata, real content | 3648 | measured, variant rwx: `__end_rodata` - `__start_rodata` once the padding is gone |
| **Alignment padding between rodata and init** | **6200** | `__end_rodata` is aligned to 16 MiB: arch/powerpc/kernel/vmlinux.lds.S:257 `ALIGN(STRICT_ALIGN_SIZE)`, `DATA_SHIFT` = 24 because `STRICT_KERNEL_RWX && PPC64` (arch/powerpc/Kconfig:872-874). The last rodata section ends at 0x9f17f0, `__init_begin` is 0x1000000. Nothing frees it |
| Init (freed at 5.6 s) | 3244 | of which `.rela.dyn` (RELOCATABLE) 2864K, computed from `readelf -S` |
| rwdata (`.data` etc.) | 972 | `_sdata`..`_edata` |
| bss | 496 | `__bss_start`..`__bss_stop` |
| **Kernel image total** | **21100** | `_end` = 0x149b000; reserved whole by `memblock_reserve(PHYSICAL_START, __pa(_end) - PHYSICAL_START)`, arch/powerpc/kernel/prom.c:844 |
| ps3fb videomemory | 9216 | boot log `ps3fb videomemory: 9437184 bytes`; `memblock_alloc` in arch/powerpc/platforms/ps3/setup.c:115-128, size from `CONFIG_FB_PS3_DEFAULT_SIZE_M`=9 or `ps3fb=` (setup.c:139-148) |
| struct page array (memmap) | 3840 | computed: 61440 pages x 64 B (`pahole -C page vmlinux`: size 64). Classic SPARSEMEM (`SPARSEMEM_VMEMMAP` is off), 15 sections of 16 MiB x 256K. Covers the CMA and ps3fb pages too |
| Dentry hash (32768 x 8 B) | 256 | boot log; `HASH_EARLY` memblock allocation, fs/dcache.c:3123-3127 |
| Inode hash (16384 x 8 B) | 128 | boot log; fs/inode.c:2423-2427 |
| Mount + mountpoint hash | 8 | boot log |
| IRQ stacks (2 per CPU) + emergency stacks (3 per CPU), 16K each | 160 | computed: arch/powerpc/kernel/setup_64.c:727-740, 774-800; `THREAD_SHIFT`=14 |
| Per-CPU first chunk (static 37016 + dyn 40808 B per CPU) | ~152 | boot log `percpu:` line; the unused tail of the 1 MiB allocation is given back |
| Not attributed (DT, pgdat, sparse tables, memblock) | ~152 | by difference |
| **Sum** | **35012** | |

In the release kernel the image's non-init part is 8964K instead of 17856K (section 4); the other
rows are the same, which accounts for the 23044K within about 170K.

Not in Linux's RAM, on purpose:

- **HTAB**: the hypervisor owns it. `htab_initialize()` sets `htab_address = NULL` for
  `FW_FEATURE_PS3_LV1` (arch/powerpc/mm/book3s64/hash_utils.c:1089-1093) and allocates nothing.
  `CONFIG_PS3_HTAB_SIZE`=20 (1 MB, boot log `ppc64_pft_size = 0x14`) is passed to
  `lv1_construct_virtual_address_space` (arch/powerpc/platforms/ps3/mm.c:167). Lowering it changes
  nothing for Linux's 240 MB (`phys_mem_size = 0xf000000`); it would only make the hash table
  smaller.
- **crashkernel**: no `crashkernel=` argument, so `reserve_crashkernel()` reserves nothing.
- **initrd**: none on the USB root (`root=PARTUUID=...`). The SSD root boots with an initramfs
  ([GAMEOS-HDD.md](GAMEOS-HDD.md)); it is freed after boot.
- **CMA 64 MiB** (`cma: Reserved 64 MiB at 0x0a000000`) is counted separately. It is not lost: the
  pages go to the buddy allocator as movable memory (MemTotal includes them). On the PS3 `CmaTotal
  65536 kB`, `CmaFree 52 kB`: page cache fills it, and the GART takes chunks back by migration. It
  costs nothing in memmap or reserve, so shrinking it saves no RAM (section 3).
- **ps3flash bounce buffer**: `PS3_FLASH` is off.

## 2. The config

From `build/kernel/obj/.config` (ps3_defconfig + linux/config/ps3.config, the debug flavour's
config; the release flavour adds linux/config/ps3-release.config):

| Option | State | Cost |
|---|---|---|
| `SLUB_DEBUG` | y, `SLUB_DEBUG_ON` unset, no `slub_debug=` | code only, no per-object metadata (the boot log says `heap alloc:off`); an older estimate in HARDWARE-LEVERAGE.md was wrong here |
| `KALLSYMS_ALL` | y (n in release) | the biggest single item, below |
| `DYNAMIC_DEBUG`, `IKCONFIG(+PROC)` | y (n in release) | below |
| `DEBUG_INFO` | y | no RAM cost (the staged `vmlinux` is stripped); only build time |
| `FTRACE`, `PROVE_LOCKING`, `LOCKDEP`, `DEBUG_SPINLOCK`... | off | already trimmed in ps3.config |
| `DEBUG_KERNEL` | y, and can't be turned off | `EXPERT=y` selects it (init/Kconfig:1549), and `EXPERT` is needed for ELFv1 |
| `CC_OPTIMIZE_FOR_SIZE` | y | already the small setting |
| `NR_CPUS` | 2 | already minimal |
| `LOG_BUF_SHIFT` | 17 (16 in release) | 576K static at 17, below |
| `STACKPROTECTOR_STRONG`, `INIT_STACK_ALL_ZERO`, `RANDOMIZE_KSTACK_OFFSET`, `DEBUG_STACKOVERFLOW` | y | code size and speed |
| `THREAD_SHIFT` | 14 (16K stacks) | `KernelStack` 2384 kB on the PS3 |
| `BPF_SYSCALL`, `NETFILTER`, `FTRACE`, `KPROBES`, `AUDIT`, `SECURITY` | off | fine |
| `CONFIG_MODULES` drivers | 101 `=m` | already modular: Bluetooth, sound, DRM, cfg80211, zram, spufs |

### Measured deltas (one variant per row, against the debug config)

| Change | Resident image | Init (boot only) | Notes |
|---|---:|---:|---|
| `STRICT_KERNEL_RWX=n` (`DATA_SHIFT` falls to 12) | **-6200K** (image end 21100K -> 14892K) | 0 | content change only -5K |
| `KALLSYMS_ALL=n` | **-1013K** | 0 | `rodata` 2556K -> 1544K |
| `KALLSYMS=n` (not advised) | -1685K | 0 | no symbol names in oopses or `perf` |
| `LOG_BUF_SHIFT=15` | -432K | 0 | static ring buffer = 4.5 x 2^shift: buffer + 2^(shift-5) descriptors x 112 B. Shift 16 = -288K (computed) |
| `DYNAMIC_DEBUG=n` + `IKCONFIG=n` | -320K | -173K | `__dyndbg` table 67K, `kernel_config_data` 21K, call-site code |
| `DEBUG_FS=n` | -259K | -129K | also removes the blk-mq/BT/CIFS debugfs bits and ps3rsx's drm debugfs files |
| IPV6(+SIT), IO_URING, QUOTA, KEXEC(+CRASH_DUMP), HUGETLBFS off; `NFS_V4=m` | **-988K** | -237K | per piece (computed from `size` of the objects): IPv6 388K, io_uring 142K, hugetlb 61K, quota 49K, kexec 42K + 16K bss, NFSv4 149K + GSS 59K (the latter two move into modules, loaded only when used) |
| `STACKPROTECTOR=n`, `INIT_STACK_NONE`, `RANDOMIZE_KSTACK_OFFSET=n` | -275K | 0 | text only; also a little faster |
| `COMPAT=n` | -197K | -30K | vdso32 45K plus compat syscalls |
| `DEBUG_MISC/MEMORY_INIT/STACKOVERFLOW`, `PM_DEBUG`, `SCHED_DEBUG`, `DEBUG_BUGVERBOSE`, `DETECT_HUNG_TASK`, `RCU_TRACE` off | -130K | -15K | |
| `SLUB_DEBUG=n` | -99K | 0 | loses `/proc/slabinfo`, which this work used |
| **Recommended set** (rwx, KALLSYMS_ALL, DYNAMIC_DEBUG, IKCONFIG, shift 16, the prune set, dbg2 off) | **-8892K** (non-init image 17856K -> 8964K) | -428K | variant `rec`: `end` = 11780K instead of 21100K. Shipped as the release flavour (section 4) |

All 11 first variants were built from the same tree; the control variant (`ctl`) matched the
shipped image exactly (text 6536K, rodata 9848K, init 3244K, rwdata 972K, bss 496K).

### Module memory (the "modules 3.5 MB")

`/proc/vmallocinfo` on the PS3 (debug kernel): 88 `load_module` areas, 3644K. `/proc/modules` sums
to 3292K for 29 modules. The loaded size is far above the `.ko` section sizes (1957K for the 28
non-ps3rsx modules) because of three per-module costs. A model computed from the `.ko` files with
`readelf` (rules from kernel/module/kallsyms.c:78-103 and arch/powerpc/kernel/module_64.c:104-115,
254) gives 3272K against 3208K measured (2% off):

| Part | KB (28 loaded modules) |
|---|---:|
| Section bytes (text, rodata, data), page-rounded in 3 groups per module | 2096 |
| Kept symbol table + string table + type table (`KALLSYMS_ALL`: all alloc symbols) | 918 |
| ELFv1 call stubs, 48 B per distinct callee | 182 |

- `KALLSYMS_ALL=n` keeps only text symbols: 918K -> 277K, **-641K** of module memory on top of the
  image's -1013K.
- `DYNAMIC_DEBUG=n`: the `__dyndbg` sections total 43K (bluetooth alone 34K).
- A small module costs 2-3x its raw size (uhid: 7.8K -> 20K), the big ones 1.5-2x (bluetooth 568K
  -> 848K, drm_kms_helper 110K -> 220K). So **moving a driver from `=y` to `=m` costs RAM if it is
  loaded in every session**, and saves RAM only for drivers that stay unloaded. A module that is
  always loaded would be cheaper built in (not measured; roughly the symbol-table and stub share,
  ~30%).
- Loaded and unused on 2026-10-15: `cfg80211` (483K, refcount 0, loaded at t=18.3 s by something in
  userspace, not by a dependency of Bluetooth, per `modules.dep`), `sg` 45K, `joydev` 28K.

## 3. Boot-time tunables

| Tunable | Today | Alternative | Saving |
|---|---|---|---:|
| `dhash_entries=` (fs/dcache.c:3104-3112) | 32768 = 256K | 8192 = 64K. The PS3 had 5838 active dentries (slabinfo) | 192K |
| `ihash_entries=` (fs/inode.c:2401) | 16384 = 128K | 4096 = 32K. ~4300 inodes live | 96K |
| `CONFIG_INET_TABLE_PERTURB_ORDER` (net/ipv4/Kconfig:405, needs `EXPERT`, which we have) | 8 in release; 16 in debug ("Table-perturb hash table entries: 65536 (262144 bytes)") | | **255K** (runtime, zeroed pages) |
| TCP ehash/bind, UDP, IP idents, futex | 16K + 64K + 8K + 32K + 64K | no knob worth using (`thash_entries=` could save ~60K) | - |
| Log buffer | `LOG_BUF_SHIFT`=16 in release (288K), 17 in debug (576K) | 15 (144K). The PS3's whole `dmesg` after 34 min was 43K (561 lines) incl. an oops; netconsole and journald catch the rest | 144K more |
| `vm.min_free_kbytes` | 8192 (image/customize.sh:167); the kernel default for this RAM is ~1.7 MB | leave it: it is a stall guard, not waste (two hangs without it) | - |
| `THREAD_SHIFT` | 14 | 13 would give back ~1.2 MB of the 2.4 MB of stacks, but ppc64 ELFv1 frames are big and root-on-USB plus NFS plus swap paths are deep | not advised |

The boot arguments both flavours ship (`linux/flavours/release.args`, `debug.args`) start with
`video=ps3fb:mode:133 consoleblank=0 ps3rsx_gart=0 ps3rsx_cma=64M`.

### ps3fb: how big must it be, and can the console move to VRAM?

- The size comes from `CONFIG_FB_PS3_DEFAULT_SIZE_M`=9 or `ps3fb=<size>`; it is a 1 MiB-aligned
  `memblock_alloc` (setup.c:115-148). The driver refuses less than 2 MiB
  (drivers/video/fbdev/ps3fb.c:2342). The first 512K is the GPU command area (`GPU_FB_START`,
  ps3fb.c:46), and the frame starts after it (`smem_len = size - 512K`, ps3fb.c:2487).
- A frame needs `max(ddr_frame_size, xdr_frame_size)`, and `num_frames = smem_len / that`
  (ps3fb.c, `ps3fb_set_par`). Computed:

| Console mode | Frame | + 512K | `ps3fb=` minimum | Saves against 9M |
|---|---:|---:|---:|---:|
| 1080p (`mode:133`) | 8160K (1088 lines x 7680) | 8672K | **9M** (today; 8M gives 0 frames) | 0 |
| 720p (`mode:131`) | 3680K (736 x 5120) | 4192K | **5M** | **4 MB** |
| 480p (`mode:130`) | 1440K | 1952K | 2M | 7 MB |

- **The 720p question.** A 720p console means the HDMI output is 720p for X too: the mode is shared
  (ps3fb sets it, ps3rsx KMS follows, [RSX.md](RSX.md)), so the desktop and games run at 720p (2.25x
  fewer pixels per frame). Whether the TV scaling is acceptable is the user's call: decision D2 in
  [PLAN.md](PLAN.md), still open. Only one 1080p monitor has been used so far (PLAN 4.10).
- A smaller console at 1080p output would need a ps3fb patch (a window centred in the mode, and
  `num_frames` computed from the XDR frame only). Not written.
- **Moving the console into VRAM through ps3rsx is not a small change.** `ps3rsx/src/console.c`
  only hands the GPU back and forth between ps3rsx and ps3fb; the console is still drawn by ps3fb
  into XDR and blitted by the GPU. ps3rsx registers no fbdev (`DRM_FBDEV_EMULATION=y` is on, but
  `grep fbdev ps3rsx/src` finds nothing). A CPU-drawn console in VRAM would run into the 10 MB/s VRAM
  reads (fbcon's scroll copy reads), and DRM's generic fbdev would keep a shadow buffer in XDR,
  which is the same 8 MB again. ps3rsx also reads ps3fb's reset program from the start of ps3fb's
  buffer (exec.c:353), so the first 512K must stay either way.
- What was built instead: the console frame is given back while X owns the screen and allocated
  again when the console is visible. That is local/0043, section 6.

### CMA size (`ps3rsx_cma=64M`)

The GART takes 1 MiB chunks from the area on demand (ps3rsx/src/gart.c:3-10); between uses the pages
hold page cache. So CMA costs no reserve, and shrinking it returns no RAM: smaller values only
shrink the cache that can live there and the GART ceiling. 6-14 MB of GART were measured in games,
so 32M would still work. The one real downside of 64M is that it is movable-only memory. With the
release kernel's 217.5 MiB `MemTotal`, the non-CMA part is about 153 MiB, against ~34 MB of
unmovable kernel allocations measured on 2026-10-15, so there is room. Leave it at 64M.

## 4. Ranked changes

Savings are steady-state RAM. Every kernel row needs a boot on the PS3. Rows 1, 3, 4, 8, 9, 10 and
15 are the release flavour (`linux/config/ps3-release.config`), shipped 2026-10-16 and booted:
`MemTotal` 222704 kB against 213992 kB on the debug kernel, **+8712 kB**, and `vmlinux` 11.9 MB
against 21.2 MB. The release kernel .472 reads 222716 kB.

| # | Change | Saves | Risk | Where | Status |
|---|---|---:|---|---|---|
| 1 | `# CONFIG_STRICT_KERNEL_RWX is not set` and `# CONFIG_STRICT_MODULE_RWX is not set` (powerpc selects the first whenever the second is set; found 2026-10-16) | **6.0 MB** (measured, variant) | low: kernel text stays writable through the linear map, irrelevant here. The image file shrinks from 21 to ~15 MB (faster kexec load); Petitboot's old kexec-tools load it | kernel config | **done**, release |
| 2 | Console at 720p: `ps3fb=5M`, `mode:131` | **4 MB** (computed) | the output becomes 720p for X as well | boot args | open: the user's decision (D2) |
| 3 | `# CONFIG_KALLSYMS_ALL is not set` | **1.0 MB** image + **0.64 MB** modules (measured + modelled) | low: oopses still show function names; data symbols disappear | kernel config | **done**, release |
| 4 | Drop IPV6, IO_URING, QUOTA, KEXEC, HUGETLBFS; `NFS_V4=m` | **1.0 MB** (measured, variant) | low-medium: some userspace may expect IPv6 sockets or io_uring (libraries fall back). NFS root (v3) stays. Nothing uses our kexec (netboot kexec runs in Petitboot's kernel) | kernel config | **done**, release |
| 5 | Rootfs: stop zram once ps3vswap is up | 1.5 MB table (vmalloc `disksize_store`, 384M x 16 B) plus its compressed pages | low | image/customize.sh:97-107 | not done: system trims wait (CLAUDE.md "Memory trims"); backlog |
| 6 | Rootfs: seccomp filters from systemd units, 126 BPF programs x 8K in vmalloc | 1.0 MB (`/proc/vmallocinfo`) | low: drop `SystemCallFilter=` on resident services, or `SECCOMP=n` | rootfs / kernel | not done, as row 5; backlog |
| 7 | Keep `cfg80211` from loading (`install cfg80211 /bin/false`); find who loads it first | 483K (refcount 0) | low; a USB Wi-Fi stick would stop working | rootfs | not done, as row 5; backlog |
| 8 | `LOG_BUF_SHIFT=16` (15 if you trust netconsole) | 288K (432K) | low | kernel config | **done** (16), release |
| 9 | `DYNAMIC_DEBUG=n`, `IKCONFIG=n` | 320K image + 43K modules | loses runtime `dev_dbg` toggling: kept in the debug flavour | kernel config | **done**, release |
| 10 | `INET_TABLE_PERTURB_ORDER=8` | 255K | none in practice | kernel config | **done**, release |
| 11 | `STACKPROTECTOR=n`, `INIT_STACK_NONE`, `RANDOMIZE_KSTACK_OFFSET=n` | 275K and a little speed | hardening lost | kernel config | not done; backlog |
| 12 | `DEBUG_FS=n` | 259K | ps3rsx's debugfs files go (MEM=1 reads `ps3rsx_mm` there); check the module still builds | kernel config | not done; backlog |
| 13 | `COMPAT=n` | 197K | breaks any 32-bit ppc binary | kernel config | not done; backlog |
| 14 | `dhash_entries=8192 ihash_entries=4096` | 288K | longer hash chains on trees with 100k+ files | boot args | not done; backlog |
| 15 | `dbg2` set (DEBUG_MISC etc.) | 130K | loses hung-task detection | kernel config | **done**, release |
| 16 | `SLUB_DEBUG=n` | 99K | loses `/proc/slabinfo`: kept for memory work | kernel config | not done, on purpose |

The release flavour took the non-init image from 17856K to 8964K (-8.7 MB) and the modules by
about 0.7 MB, so the steady-state reserve went from 31.8 MB to about 22.5 MB (23044K measured). Row
2 would make it about 18.5 MB.

Does not help, or is not worth it:

- Shrinking the CMA area, `CONFIG_PS3_HTAB_SIZE`, `MEMCG=n` (would cut struct page to 56 B, 0.5 MB,
  but game mode needs `memory.low`), `FLATMEM` (a few KB), 64K pages (page cache waste at 240 MB),
  `THREAD_SHIFT=13` (risk), `RELOCATABLE=n` (its 2.8 MB of relocations are init memory, freed at
  5.6 s; only the boot-time peak would drop).
- Turning `=y` drivers into modules: HID (212K), USB, SCSI, ext4 and the PS3 platform code are
  loaded in every session, and a loaded module costs more than the same code built in. FAT/VFAT
  (69K) was the only sizeable candidate that stays unused most of the time; it is now in use for
  `dev_hdd1` ([GAMEOS-HDD.md](GAMEOS-HDD.md)).

## 5. How to reproduce

```
build/kexp/run-variant.sh <name> <fragment names>     # inside the builder container, cwd = repo
```

Fragments live in `build/kexp/cfg/`. `DEBUG_INFO=n` is `CONFIG_DEBUG_INFO_NONE=y` (the line
`# CONFIG_DEBUG_INFO is not set` has no effect: `DEBUG_INFO` is a derived symbol). The release
flavour is built the way this section once proposed: `FRAGMENTS_release="ps3.config
ps3-release.config"` in `linux/package.conf`, its own Debian flavour `ps3`
([PACKAGING.md](PACKAGING.md)). `build/kexp/` is ephemeral; if it is gone, the variants are a copy
of the kernel tree with one fragment each.

## 6. ps3fb's console shadow while X owns the display (local/0043, switch off by default)

During games ps3fb's blit thread `ps3fbd` is idle (0 CPU s over 20 s, measured on the PS3,
2026-10-18), so ps3fb costs no game time, but its XDR frame stays allocated while ps3rsx owns the
GPU and nobody sees the console. local/0043 (`linux/patches/local/0043-ps3fb-release-console-shadow.patch`)
gives that memory back.

**State.** Written and compile-tested 2026-10-18 (`ps3fb.o` clean with W=1 for both configs,
`vmlinux` links), first built as `6.12.111+ps3.436~dev20261018101906` and in kernel .457 (built,
never booted). It is in the release kernel 6.12.111+ps3.472, which is the default and runs every
bench since 2026-10-18 22:10 UTC; `ps3fb.shadow_release` is 0 there, nobody has turned it on, and
the PS3 checks below have not run. No release or debug `.args` sets it.

### What is freed, and where it comes from

- `ps3fb_videomemory` is a 1 MiB-aligned `memblock_alloc` in setup_arch (setup.c, 9 MiB with
  `CONFIG_FB_PS3_DEFAULT_SIZE_M=9`): `PageReserved` pages with struct pages, outside the buddy
  allocator. ps3fb maps all of it into IO space 0 of its own lv1 GPU context
  (`lv1_gpu_context_iomap`, IO pages are 1 MiB). Layout: 0-4K reset program, 4K-64K FIFO, 512K-9M
  the frame (8160K at 1080p, `GPU_FB_START` = 512K).
- **The first MiB stays**: it is IO page 0 (FIFO and reset program; ps3rsx copies the reset program
  from it at load, exec.c; `/dev/ps3gpu` minor 1 maps it). The other 8 MiB go: at the default 9M,
  **8192 KiB**.
- The first release hands the boot memory to the page allocator with `memblock_phys_free()`
  (powerpc keeps memblock) and `free_reserved_area()` (what `free_initmem` uses): `MemTotal` grows
  by 8192 kB for good. From then on the frame lives in **1 MiB chunks**: `alloc_pages(order 8,
  __GFP_NORETRY)` first, else ps3rsx's CMA area (`ps3rsx_cma_alloc(1)`). The page allocator comes
  first because the GART has no source but the CMA area (`chunk_back` fails when it is full), and
  scattered console chunks there would also break the contiguous ring allocation on a ps3rsx
  reload. Chunks need no physical contiguity: each is one GPU IO page, the CPU sees them through one
  `vmap()` (first MiB + chunks), and `/dev/fb0` mmap maps them chunk by chunk (shared mappings only
  in that layout: `remap_pfn_range` takes a private mapping only whole).
- Not contiguous on purpose: one 8 MiB CMA allocation can fail on fragmentation (GART chunks), eight
  1 MiB ones practically can't. A dedicated ps3fb CMA area was rejected: CMA areas come in
  pageblocks, 16 MiB here (`pageblock_order` = 12 with 4K pages, `MAX_PAGE_ORDER` 12), so it would
  take 16 MiB of movable-only memory for an 8 MiB need.
- When the page allocator has no free 1 MiB blocks, the console's 8 MiB come out of the 64 MiB
  `ps3rsx_cma` area while it is visible, so the GART ceiling is up to 8 MiB lower then (X is away or
  gone; 6-14 MB of GART were measured in games). Not measured: fbcon now draws through 4K vmalloc
  mappings instead of the linear map's 16M pages (more hash table misses on scrolls).

### When

The hook is the existing GPU handover, `ps3fb_gpu_takeover()` (local/0005); ps3rsx is unchanged.

- **Release**: `takeover(true)` (X's `master_set`, a VT switch to X, a client without X submitting)
  schedules a work after `shadow_release_delay_ms` (3 s). If ps3rsx still owns the GPU then and no
  process has `/dev/fbN` open, it releases. The delay keeps a quick VT switch round trip or a short
  offscreen GL run from churning 8 MiB through the allocator.
- **Take back**: `takeover(false)` (X's `master_drop` on a VT switch away or exit, ps3rsx's 2 s idle
  release, ps3rsx unload) hands the GPU back at once and queues the work. The work allocates with no
  lock held: reclaim or CMA migration can take seconds right after a game exits, and doing it inside
  the handover would stall ps3rsx's `own_lock` (`master_set`, every submitter, the vswap copies).
  Then, under `gpu_own_lock`, it checks the console is still wanted, maps the chunks and installs
  them; fbcon resumes and redraws the visible console, and the next vsync blits it. Until then the
  screen shows ps3fb's last DDR frame (ps3rsx flips to it). Without memory the console stays dark
  and the work tries again after 1, 2, 4 ... 32 s (reset by the next handover).
- **Switch**: `/sys/module/ps3fb/parameters/shadow_release` (0644; at boot `ps3fb.shadow_release=1`).
  Writing 1 while X runs releases at once; writing 0 takes the memory back at once (the console
  stays hidden until X lets it go). That allows same-boot A/B measurements.

### What keeps off the freed memory (release order)

1. Under `console_lock` and `shadow_lock` (which `open` and `mmap` take): `released` is set,
   `fb_set_suspend(info, 1)` runs (fbcon takes its cursor off while the memory is still there, then
   `FBINFO_STATE_SUSPENDED`), and `screen_base = NULL`. All of it under `shadow_lock`, because
   `fb_read` and `fb_write` check the state unlocked and then use `screen_base`: a process opening
   `/dev/fbN` after the check of the open count must already see the suspended state (found in
   review).
2. The frame's IO pages (1..8) are pointed at the first MiB, read-only (like ps3rsx's GART scratch
   chunk, which is RW): a stray GPU fetch must never fault, since a fault wedges the GPU until a
   reboot. Read-write would let a stray GPU write hit the FIFO and reset program. If lv1 refuses
   read-only, the old mappings go back and nothing is freed (dmesg `iomap of IO page`; the feature
   then can't work as written).
3. Then the memory is freed (or the chunks, `vunmap` first).

Gates while released: fbcon (`fbcon_is_inactive` and every `sys_*` draw op check `info->state`);
`fb_read`/`fb_write` (the same check); `ps3fb_set_par` skips its memset (`screen_base` NULL, changed
only under `console_lock`, which `set_par` always runs under); `ps3fb_sync_image` returns (this
covers the vsync blits, `FSEL`, the DDR clear in `set_par`, the last-close sync); `mmap` returns
-EBUSY; `PS3GPU_IOCTL_ENTER` returns -EBUSY (direct-GPU users use the frame as their "gart"). A
process holding `/dev/fbN` open blocks the release (an mmap keeps the file open), and the last close
triggers it. ps3fb's own GPU work stops at `takeover(true)` already (FIFO fenced), so the GPU reads
nothing from the frame while released. Shutdown or reboot while released unmaps IO pages one by one
with what each maps now; a `takeover()` after ps3fb's shutdown returns -ENODEV (before the patch, it
ran the reset program on the freed context).

**Panic or oops while released**: nothing is drawn. fbcon is suspended, and `fbcon_is_inactive()`
in this tree has no forced-output path (no `FBINFO_CAN_FORCE_OUTPUT`), so no CPU write can reach
freed memory or a NULL `screen_base`. Nothing is lost: with X in front the VT is in `KD_GRAPHICS`
and ps3fb doesn't blit while ps3rsx owns the GPU, so a panic under X showed nothing on screen
before either. Panic output while X runs goes to netconsole (`tools/listen-debug.sh`) only, as it
did. With the console in front nothing changes (the buffer is there).

With `shadow_release=0` the only differences are the open count, the `shadow_lock` around `mmap`,
and the -ENODEV after shutdown.

A second agent (Opus) reviewed it before the build: the one blocker (the `fb_read`/`fb_write` race
above) and the should-fixes (refill out of the handover, no RW scratch, page allocator before CMA,
retry backoff, takeover after shutdown) are in the patch.

### PS3 checks

On kernel .472 or later, already installed and the default, so no netboot and no new kernel is
needed. Same boot for the A/B (the runtime switch), the user not at the PS3, a webcam photo for
every "console visible" step. Values: `grep -E 'MemTotal|MemFree|MemAvailable|CmaFree'
/proc/meminfo` and `dmesg | grep ps3fb`. Nothing of this has run yet.

1. Boot with the switch off. Text console: meminfo (T0).
2. Start the desktop, 60 s idle: meminfo (A0). `echo 1 | sudo tee /sys/module/ps3fb/parameters/shadow_release`;
   expect `console shadow released ... (8192 KiB)` at once; meminfo (A1). Expected: `MemTotal`
   +8192 kB (first release only), `MemAvailable` about +8 MB. First thing to check: no `iomap of IO
   page ... failed` line (lv1 took the read-only mapping; if it refused, nothing was freed and the
   patch needs another scratch). Photo: X's picture unchanged.
3. Cycle the switch 0/1 three times with X in front: each 0 takes 8 MiB back (`MemFree`, or
   `CmaFree` when there were no free 1 MiB blocks; `console shadow back`), each 1 gives it back.
   `MemTotal` stays after the first release; X's picture is unchanged throughout.
4. VT switch: Ctrl+Alt+F1, the login prompt is readable within ~1 s (photo), type something;
   Ctrl+Alt+F7, X's frame is back; after 3 s `released`. 10 rounds; then the same with a game
   running (memory under pressure): time until the console is readable, no `no memory` warning.
5. X exit (stop the display manager): the console comes back with the text that was there before X
   started plus what came after; `fbset -i` works; a write test through an mmap of `/dev/fb0` (a
   pattern shows on screen) and `dd if=/dev/zero of=/dev/fb0` (the screen clears) in chunk mode.
6. `sleep 600 </dev/fb0 &` before starting X: no release while it runs; kill it and the release
   follows.
7. ps3rsx unload with X stopped, then reload and start X again: release and return still work.
8. Games: the 5-10 game suite with the switch on against off in the same boot: `MemAvailable`
   during play +8 MB, fps unchanged (ps3fbd is idle in games anyway), less swap. The measurement
   rules apply (desktop stopped, 60 s warm-up, 120 s window, webcam).
9. Reboot and power off from X with the console released: clean (netconsole shows no lv1 errors).
10. Panic, only with the user's OK (it needs the power button): `echo c > /proc/sysrq-trigger` from
    X with the console released: the screen shows nothing (as before), netconsole has the oops, no
    second oops from ps3fb.

If anything is odd, `echo 0` returns the frame, and 0 is the default anyway. When the checks pass:
default on (`ps3fb.shadow_release=1` in `linux/flavours/*.args`, or the default in the patch).

## History

- 2026-10-15: the analysis above, on 6.12.112 debug builds; it replaced HARDWARE-LEVERAGE.md's
  "~5-10 MB est." for a kernel trim.
- 2026-10-16: the recommended set shipped as the `ps3-release` flavour, `MemTotal` +8712 kB. While
  doing it, `STRICT_MODULE_RWX` turned out to select `STRICT_KERNEL_RWX` on powerpc, so both are off.
- 2026-10-17: rows 5-7 (zram, seccomp, cfg80211) set aside by the user's rule that memory comes
  from game mode, not from disabling what a desktop user expects (CLAUDE.md "Memory trims").
- 2026-10-18: local/0043 written; it was meant for the .457 test kernel, which never booted, and
  went into the release kernel .472 instead.

Older versions and the dated history: [archive/KERNEL-MEMORY.md](archive/KERNEL-MEMORY.md).
