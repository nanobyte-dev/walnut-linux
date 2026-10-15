# Kernel RAM cost on the PS3

Research notes (2026-10-15): where the kernel's RAM goes (boot log, `System.map`, `.config`, source
of Linux 6.12.112 with our patches), and what each config or boot change saves. This replaces the
"~5-10 MB est." for a kernel trim in HARDWARE-LEVERAGE.md.

How the numbers were obtained:

- **boot log**: `dmesg` of the running PS3 (6.12.112-ps3-debug-202610151803), read only.
- **measured, variant**: I built `vmlinux` variants from the same patched source (the 15:03 build's
  tree, copied to `build/kexp/src`; one `O=` tree per variant, flags in `build/kexp/cfg/*.config`,
  runner `build/kexp/run-variant.sh`, same builder image). The unmodified config reproduces the
  shipped `vmlinux` section for section, so deltas are real. "Resident" = sum of the sizes of all
  alloc sections outside the init range, ignoring the 16 MiB padding (the padding gets its own row).
- **computed**: from the named source file or from `size`/`nm` on the build output.
- Nothing was booted: every kernel row below still needs one reboot test.

## 1. What the 35 MB "reserved" is

`Memory: 143960K/245760K available (6536K kernel code, 972K rwdata, 9848K rodata, 3244K init, 496K bss,
35012K reserved, 65536K cma-reserved)`.

The line is printed from `page_alloc_init_late()` (mm/mm_init.c:2325), after SMP bring-up and before
the CMA area is activated. `reserved` = physpages - totalram - totalcma (mm/mm_init.c:2650): everything
memblock still holds at that point, CMA excluded. Check against the running system: MemTotal 213992 kB
= 245760 - 35012 + 3244, the 3244K being the init memory freed at 5.6 s ("Freeing unused kernel
image"). So the steady-state reserve is **31768K**.

| Item | KB | Source |
|---|---:|---|
| Kernel text (`_stext`..`_etext`) | 6536 | `System.map`: `_etext` = 0x662000 |
| Kernel rodata, real content | 3648 | measured, variant rwx: `__end_rodata` - `__start_rodata` once the padding is gone |
| **Alignment padding between rodata and init** | **6200** | `__end_rodata` is aligned to 16 MiB: arch/powerpc/kernel/vmlinux.lds.S:257 `ALIGN(STRICT_ALIGN_SIZE)`, `DATA_SHIFT` = 24 because `STRICT_KERNEL_RWX && PPC64` (arch/powerpc/Kconfig:872-874). Last rodata section ends at 0x9f17f0, `__init_begin` is 0x1000000. Nothing frees it. |
| Init (freed at 5.6 s) | 3244 | of which `.rela.dyn` (RELOCATABLE) 2864K, computed from `readelf -S` |
| rwdata (`.data` etc.) | 972 | `_sdata`..`_edata` |
| bss | 496 | `__bss_start`..`__bss_stop` |
| **Kernel image total** | **21100** | `_end` = 0x149b000; reserved whole by `memblock_reserve(PHYSICAL_START, __pa(_end) - PHYSICAL_START)`, arch/powerpc/kernel/prom.c:844 |
| ps3fb videomemory | 9216 | boot log `ps3fb videomemory: 9437184 bytes`; `memblock_alloc` in arch/powerpc/platforms/ps3/setup.c:115-128, size from `CONFIG_FB_PS3_DEFAULT_SIZE_M`=9 or `ps3fb=` (setup.c:139-148) |
| struct page array (memmap) | 3840 | computed: 61440 pages x 64 B (`pahole -C page vmlinux`: size 64). Classic SPARSEMEM (`SPARSEMEM_VMEMMAP` is off), 15 sections of 16 MiB x 256K. Covers the CMA and ps3fb pages too. |
| Dentry hash (32768 x 8 B) | 256 | boot log; `HASH_EARLY` memblock allocation, fs/dcache.c:3123-3127 |
| Inode hash (16384 x 8 B) | 128 | boot log; fs/inode.c:2423-2427 |
| Mount + mountpoint hash | 8 | boot log |
| IRQ stacks (2 per CPU) + emergency stacks (3 per CPU), 16K each | 160 | computed: arch/powerpc/kernel/setup_64.c:727-740, 774-800; `THREAD_SHIFT`=14 |
| Per-CPU first chunk (static 37016 + dyn 40808 B per CPU) | ~152 | boot log `percpu:` line; the unused tail of the 1 MiB allocation is given back |
| Not attributed (DT, pgdat, sparse tables, memblock) | ~152 | by difference |
| **Sum** | **35012** | |

Not in Linux's RAM, on purpose:

- **HTAB**: the hypervisor owns it. `htab_initialize()` sets `htab_address = NULL` for
  `FW_FEATURE_PS3_LV1` (arch/powerpc/mm/book3s64/hash_utils.c:1089-1093) and allocates nothing.
  `CONFIG_PS3_HTAB_SIZE`=20 (1 MB, boot log `ppc64_pft_size = 0x14`) is passed to
  `lv1_construct_virtual_address_space` (arch/powerpc/platforms/ps3/mm.c:167). Lowering it changes
  nothing for Linux's 240 MB (`phys_mem_size = 0xf000000`); it would only make the hash table
  smaller.
- **crashkernel**: no `crashkernel=` argument, so `reserve_crashkernel()` reserves nothing.
- **initrd**: none (`root=PARTUUID=...` on the command line, no initrd in the log).
- **CMA 64 MiB** (`cma: Reserved 64 MiB at 0x0a000000`) is counted separately. It is not lost: the
  pages go to the buddy allocator as movable memory (MemTotal includes them). On the PS3 now
  `CmaTotal 65536 kB`, `CmaFree 52 kB`, i.e. page cache fills it and the GART takes chunks back by
  migration. It costs nothing in memmap or reserve, so shrinking it saves no RAM (section 3).
- **ps3flash bounce buffer**: `PS3_FLASH` is off.

## 2. The config

Facts from `build/kernel/obj/.config` (ps3_defconfig + kernel/config/ps3.config):

| Option | State | Cost |
|---|---|---|
| `SLUB_DEBUG` | y, `SLUB_DEBUG_ON` unset, no `slub_debug=` | Code only, no per-object metadata (the boot log says `heap alloc:off`). The estimate in HARDWARE-LEVERAGE.md was wrong here. |
| `KALLSYMS_ALL` | y | Biggest single item, below |
| `DYNAMIC_DEBUG`, `IKCONFIG(+PROC)` | y | below |
| `DEBUG_INFO` | y | No RAM cost (the staged `vmlinux` is stripped); only build time |
| `FTRACE`, `PROVE_LOCKING`, `LOCKDEP`, `DEBUG_SPINLOCK`... | off | already trimmed in ps3.config |
| `DEBUG_KERNEL` | y, and cannot be turned off | `EXPERT=y` selects it (init/Kconfig:1549), and `EXPERT` is needed for ELFv1 |
| `CC_OPTIMIZE_FOR_SIZE` | y | already the small setting |
| `NR_CPUS` | 2 | already minimal |
| `LOG_BUF_SHIFT` | 17 | 576K static, below |
| `STACKPROTECTOR_STRONG`, `INIT_STACK_ALL_ZERO`, `RANDOMIZE_KSTACK_OFFSET`, `DEBUG_STACKOVERFLOW` | y | code size and speed |
| `THREAD_SHIFT` | 14 (16K stacks) | `KernelStack` 2384 kB on the PS3 |
| `BPF_SYSCALL`, `NETFILTER`, `FTRACE`, `KPROBES`, `AUDIT`, `SECURITY` | off | fine |
| `CONFIG_MODULES` drivers | 101 `=m` | already modular: Bluetooth, sound, DRM, cfg80211, zram, spufs |

### Measured deltas (one variant per row, vs. the shipped config)

| Change | Resident image | Init (boot only) | Notes |
|---|---:|---:|---|
| `STRICT_KERNEL_RWX=n` (`DATA_SHIFT` falls to 12) | **-6200K** (image end 21100K -> 14892K) | 0 | content change only -5K |
| `KALLSYMS_ALL=n` | **-1013K** | 0 | `rodata` 2556K -> 1544K |
| `KALLSYMS=n` (not advised) | -1685K | 0 | no symbol names in oopses or `perf` |
| `LOG_BUF_SHIFT=15` | -432K | 0 | static ring buffer = 4.5 x 2^shift: buffer + 2^(shift-5) descriptors x 112 B. Shift 16 = -288K (computed). |
| `DYNAMIC_DEBUG=n` + `IKCONFIG=n` | -320K | -173K | `__dyndbg` table 67K, `kernel_config_data` 21K, call-site code |
| `DEBUG_FS=n` | -259K | -129K | also removes the blk-mq/BT/CIFS debugfs bits and ps3rsx's drm debugfs files |
| IPV6(+SIT), IO_URING, QUOTA, KEXEC(+CRASH_DUMP), HUGETLBFS off; `NFS_V4=m` | **-988K** | -237K | per piece (computed from `size` of the objects): IPv6 388K, io_uring 142K, hugetlb 61K, quota 49K, kexec 42K + 16K bss, NFSv4 149K + GSS 59K (the latter two move into modules, loaded only when used) |
| `STACKPROTECTOR=n`, `INIT_STACK_NONE`, `RANDOMIZE_KSTACK_OFFSET=n` | -275K | 0 | text only; also a little faster |
| `COMPAT=n` | -197K | -30K | vdso32 45K plus compat syscalls |
| `DEBUG_MISC/MEMORY_INIT/STACKOVERFLOW`, `PM_DEBUG`, `SCHED_DEBUG`, `DEBUG_BUGVERBOSE`, `DETECT_HUNG_TASK`, `RCU_TRACE` off | -130K | -15K | |
| `SLUB_DEBUG=n` | -99K | 0 | loses `/proc/slabinfo`, which this note used |
| **Recommended set** (rwx, KALLSYMS_ALL, SLUB_DEBUG, DYNAMIC_DEBUG, IKCONFIG, shift 16, prune set, dbg2 off) | **-8892K** (non-init image 17856K -> 8964K) | -428K | variant `rec`: `end` = 11780K instead of 21100K |

All 11 first variants were built from the same tree; the control variant (`ctl`) matched the shipped
image exactly (text 6536K, rodata 9848K, init 3244K, rwdata 972K, bss 496K).

### Module memory (the "modules 3.5 MB")

`/proc/vmallocinfo` on the PS3: 88 `load_module` areas, 3644K. `/proc/modules` sums to 3292K for 29
modules. The loaded size is far above the `.ko` section sizes (1957K for the 28 non-ps3rsx modules)
because of three per-module costs. My model (computed from the `.ko` files with `readelf`, rules from
kernel/module/kallsyms.c:78-103 and arch/powerpc/kernel/module_64.c:104-115, 254) gives 3272K
against 3208K measured (2% off):

| Part | KB (28 loaded modules) |
|---|---:|
| Section bytes (text, rodata, data), page-rounded in 3 groups per module | 2096 |
| Kept symbol table + string table + type table (`KALLSYMS_ALL`: all alloc symbols) | 918 |
| ELFv1 call stubs, 48 B per distinct callee | 182 |

- `KALLSYMS_ALL=n` keeps only text symbols: 918K -> 277K, **-641K** of module memory on top of the
  image's -1013K.
- `DYNAMIC_DEBUG=n`: `__dyndbg` sections total 43K (bluetooth alone 34K).
- A small module costs 2-3x its raw size (uhid: 7.8K -> 20K); the big ones cost 1.5-2x (bluetooth
  568K -> 848K, drm_kms_helper 110K -> 220K). So **moving a driver from `=y` to `=m` costs RAM if
  it is loaded in every session**, and saves RAM only for drivers that stay unloaded. Conversely a
  module that is always loaded would be cheaper built in (not measured; roughly the symbol-table
  and stub share, ~30%).
- Loaded and unused right now: `cfg80211` (483K, refcount 0, loaded at t=18.3 s by something in
  userspace, not by a dependency of Bluetooth, per `modules.dep`), `sg` 45K, `joydev` 28K.

## 3. Boot-time tunables

| Tunable | Today | Alternative | Saving |
|---|---|---|---:|
| `dhash_entries=` (fs/dcache.c:3104-3112) | 32768 = 256K | 8192 = 64K. The PS3 had 5838 active dentries (slabinfo) | 192K |
| `ihash_entries=` (fs/inode.c:2401) | 16384 = 128K | 4096 = 32K. ~4300 inodes live | 96K |
| `CONFIG_INET_TABLE_PERTURB_ORDER` (net/ipv4/Kconfig:405, needs `EXPERT`, which we have) | 16 -> "Table-perturb hash table entries: 65536 (262144 bytes)" | 8 | **255K** (runtime, zeroed pages) |
| TCP ehash/bind, UDP, IP idents, futex | 16K + 64K + 8K + 32K + 64K | no knob worth using (`thash_entries=` could save ~60K) | - |
| Log buffer | `LOG_BUF_SHIFT`=17, 576K | 16 (288K) or 15 (144K). The PS3's whole `dmesg` after 34 min is 43K (561 lines) incl. an oops; netconsole and journald catch the rest | 288K / 432K |
| `vm.min_free_kbytes` | 8192 (rootfs/customize.sh:167); kernel default for this RAM is ~1.7 MB | leave: it is a stall guard, not waste | - |
| `THREAD_SHIFT` | 14 | 13 would give back ~1.2 MB of the 2.4 MB stacks, but ppc64 ELFv1 frames are big and root-on-USB plus NFS plus swap paths are deep | not advised |

### ps3fb: how big must it be, and can the console move to VRAM?

- Size is chosen by `CONFIG_FB_PS3_DEFAULT_SIZE_M`=9 or `ps3fb=<size>`; it is a 1 MiB-aligned
  `memblock_alloc` (setup.c:115-148). The driver refuses less than 2 MiB (drivers/video/fbdev/ps3fb.c:2342).
  The first 512K is the GPU command area (`GPU_FB_START`, ps3fb.c:46), the frame starts after it
  (`smem_len = size - 512K`, ps3fb.c:2487).
- A frame needs `max(ddr_frame_size, xdr_frame_size)`, and `num_frames = smem_len / that`
  (ps3fb.c, `ps3fb_set_par`). Computed:

| Console mode | Frame | + 512K | `ps3fb=` minimum | Saves vs 9M |
|---|---:|---:|---:|---:|
| 1080p (`mode:133`) | 8160K (1088 lines x 7680) | 8672K | **9M** (today; 8M gives 0 frames) | 0 |
| 720p (`mode:131`) | 3680K (736 x 5120) | 4192K | **5M** | **4 MB** |
| 480p (`mode:130`) | 1440K | 1952K | 2M | 7 MB |

- The 720p console means the HDMI output mode is 720p for X too: the mode is shared (ps3fb sets it,
  ps3rsx KMS follows, `docs/RSX.md`). Whether the TV scaling is acceptable is the user's call.
- A smaller console at 1080p output would need a ps3fb patch (a window centred in the mode, and
  `num_frames` computed from the XDR frame only). Not written.
- **Moving the console into VRAM through ps3rsx is not a small change.** `gpu/ps3rsx/console.c` only
  hands the GPU back and forth between ps3rsx and ps3fb; the console is still drawn by ps3fb into
  XDR and blitted by the GPU. ps3rsx registers no fbdev (`DRM_FBDEV_EMULATION=y` is on, but
  `grep fbdev gpu/ps3rsx` finds nothing). A CPU-drawn console in VRAM would run into the 10 MB/s
  VRAM reads (fbcon's scroll copy reads); DRM's generic fbdev would keep a shadow buffer in XDR,
  which is the same 8 MB again. ps3rsx also reads ps3fb's reset program from the start of ps3fb's
  buffer (exec.c:353), so the first 512K must stay either way.
- Idea with a real payoff but real risk: allocate the console frame from a CMA area only while the
  console is visible, and release it while X owns the screen (VT in graphics mode). Saves up to
  8.5 MB during games; needs ps3fb's GPU iomap and vsync blit task to cope with the buffer
  disappearing.

### CMA size (`ps3rsx_cma=64M`)

The GART takes 1 MiB chunks from the area on demand (gpu/ps3rsx/gart.c:3-10); between uses the pages
hold page cache. So CMA costs no reserve, and shrinking it does not return RAM. Smaller values only
shrink the cache that can live there and the GART ceiling. 6-14 MB of GART was measured in practice,
so 32M would still work; the one real downside of 64M is that it is movable-only memory (the
non-CMA part of the zone is ~145 MB (MemTotal 209 MB incl. CMA), with ~34 MB of unmovable kernel allocations in use today, so there is room).
Leave it.

## 4. Ranked changes

Savings are steady-state RAM. "Reboot" = needs a boot test on the PS3 (all kernel rows do; nothing
here was booted).

| # | Change | Saves | Risk | Where |
|---|---|---:|---|---|
| 1 | `# CONFIG_STRICT_KERNEL_RWX is not set` (and `STRICT_MODULE_RWX` can stay) | **6.0 MB** (measured, variant) | Low. Kernel text stays writable through the linear map; irrelevant here. The image file also shrinks from 21 to ~15 MB (faster kexec load). Verify Petitboot's old kexec-tools still load it. | kernel config, reboot |
| 2 | Console at 720p: `ps3fb=5M`, `mode:131` | **4 MB** (computed) | Output resolution becomes 720p for X as well | boot args, reboot |
| 3 | `# CONFIG_KALLSYMS_ALL is not set` | **1.0 MB** image + **0.64 MB** modules (measured + modelled) | Low: oopses still show function names; data symbols disappear | kernel config, reboot |
| 4 | Drop IPV6, IO_URING, QUOTA, KEXEC, HUGETLBFS; `NFS_V4=m` | **1.0 MB** (measured, variant) | Low-medium: some userspace may expect IPv6 sockets or io_uring (libraries fall back). NFS root (v3) stays. Nothing here uses our kexec (netboot kexec runs in Petitboot's kernel). | kernel config, reboot |
| 5 | Rootfs: stop zram once ps3vswap is up | 1.5 MB table (vmalloc `disksize_store`, 384M x 16 B) plus its compressed pages | Low | rootfs/customize.sh:97-107 |
| 6 | Rootfs: seccomp filters from systemd units: 126 BPF programs x 8K in vmalloc | 1.0 MB (`/proc/vmallocinfo`) | Low: drop `SystemCallFilter=` on resident services, or `SECCOMP=n` | rootfs / kernel |
| 7 | Keep `cfg80211` from loading (`install cfg80211 /bin/false`); find who loads it first | 483K now (refcount 0) | Low; a USB Wi-Fi stick would stop working | rootfs |
| 8 | `LOG_BUF_SHIFT=16` (15 if you trust netconsole) | 288K (432K) | Low | kernel config |
| 9 | `DYNAMIC_DEBUG=n`, `IKCONFIG=n` | 320K image + 43K modules | Loses runtime `dev_dbg` toggling: keep them in the debug flavour | kernel config |
| 10 | `INET_TABLE_PERTURB_ORDER=8` | 255K | None in practice | kernel config |
| 11 | `STACKPROTECTOR=n`, `INIT_STACK_NONE`, `RANDOMIZE_KSTACK_OFFSET=n` | 275K and a little speed | Hardening lost | kernel config |
| 12 | `DEBUG_FS=n` | 259K | ps3rsx's debugfs files go; check the module still builds | kernel config |
| 13 | `COMPAT=n` | 197K | Breaks any 32-bit ppc binary | kernel config |
| 14 | `dhash_entries=8192 ihash_entries=4096` | 288K | Longer hash chains on trees with 100k+ files | boot args |
| 15 | `dbg2` set (DEBUG_MISC etc.) | 130K | Loses hung-task detection | kernel config |
| 16 | `SLUB_DEBUG=n` | 99K | Loses `/proc/slabinfo`: keep it for memory work | kernel config |

Rows 1, 3, 4, 8, 9, 10, 15 together (the `rec` variant) take the non-init image from 17856K to
8964K (-8.7 MB) and the modules by about 0.7 MB, so the steady-state reserve goes from 31.8 MB to
about 22 MB, or about 18 MB with row 2.

Does not help or is not worth it:

- Shrinking the CMA area, `CONFIG_PS3_HTAB_SIZE`, `MEMCG=n` (would cut struct page to 56 B, 0.5 MB,
  but game mode needs `memory.low`), `FLATMEM` (a few KB), 64K pages (page cache waste at 240 MB),
  `THREAD_SHIFT=13` (risk), `RELOCATABLE=n` (its 2.8 MB of relocations are init memory, freed at 5.6 s;
  only the boot-time peak would drop).
- Turning `=y` drivers into modules: HID (212K), USB, SCSI, ext4, the PS3 platform code are loaded
  in every session, and a loaded module costs more than the same code built in. FAT/VFAT (69K) is the
  only sizeable candidate that stays unused most of the time; it saves at most that, only if
  nothing loads it.

## 5. How to reproduce

```
build/kexp/run-variant.sh <name> <fragment names>     # inside the builder container, cwd = repo
```

Fragments live in `build/kexp/cfg/`. `DEBUG_INFO=n` is `CONFIG_DEBUG_INFO_NONE=y` (the line
`# CONFIG_DEBUG_INFO is not set` has no effect: `DEBUG_INFO` is a derived symbol). The release flavour
could be a second `KERNEL_FRAGMENTS` entry in config.mk next to `ps3.config`, built as a
separate `KERNEL_LOCALVERSION`.
