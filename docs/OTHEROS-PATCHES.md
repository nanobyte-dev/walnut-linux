# OtherOS++ and PS3 kernel patches, surveyed for 6.12

On 2026-10-18 we surveyed every PS3 kernel patch we could find outside our tree (glevand's OtherOS++ work, Geoff Levand's queue, T2 SDE, Gentoo, mainline after 6.12, unmerged postings to linuxppc-dev) and checked which ones our 6.12.111 kernel lacks and which are safe to carry. We ported 13 of them as `packages/patched/kernel/linux/patches/local/0020`-`0032`, and the user decided on the risky ones already in our series (four dropped, one switched off). Every candidate has a decision; the few things worth knowing for later are at the end ([Left open](#left-open)). The list of unported hardware ideas is in [backlog/hardware.md](backlog/hardware.md).

Risk classes follow the safety rules in [CLAUDE.md](../CLAUDE.md):

- **SAFE**: read-only or a plain software fix. No hypervisor, flash or SYSCON state changes.
- **CAREFUL**: changes hardware or hypervisor state at run time only (gone after a power cycle).
- **FORBIDDEN**: can write flash, EEPROM, SYSCON or other persistent state. Never enabled or ported. A driver that gives root a raw channel to such state is marked "FORBIDDEN-capable".

The "6.12?" column says whether the patch applies to our tree, checked with `git apply --check` forward and reverse in `build/work/linux`.

## Where the patches come from

| Source | What it holds | Checked |
|---|---|---|
| psx-place set (Sombat Tragoongoson), via [damiandudycz/ps3](https://github.com/damiandudycz/ps3) `files/kernel-patches/psx-place-Sombat-Tragoongoson/6.12` | glevand's OtherOS++ patches (gitbrew era, numbering 0010-0260) refreshed up to 6.12, plus 0280-0450 fixes | already our `packages/patched/kernel/linux/patches/ps3/` (36 patches) |
| glevand on GitHub (`glevand/ps3-linux`, `ps3-linux-patches`) | | gone: the repositories do not answer. The 2012 gists ([3.3.x](https://gist.github.com/2476441), [3.2.x](https://gist.github.com/2476405)) list the same 0010-0260 set |
| Geoff Levand, [kernel.org geoff/ps3-linux.git](https://git.kernel.org/pub/scm/linux/kernel/git/geoff/ps3-linux.git/) | `ps3-queue-v6.12`, `-v6.13`, `fix-gelic-skb`, `for-merge-net-v9`, `for-merge-hvc-v1` (shallow fetch, depth 60) | yes |
| T2 SDE (René Rebe), `svn.exactcode.de/t2/trunk/architecture/powerpc64/package/linux/` and `package/kernel/linux/` | glevand's set in T2 form, ps3fb/ps3vram changes, generic hotfixes | yes (current trunk) |
| Gentoo (Damian Dudycz), same repository, `dev-tools/kernel-patches/patches/{PSXPlace,T2SDE,New,damiandudycz}` | the two sets above split and renamed, plus his own mm and lv1 changes | yes |
| Mainline, commits since 2024-07 on every PS3 path (platforms/ps3, drivers/ps3, gelic, ps3disk, ps3vram, ps3rom, ps3fb, snd_ps3, ehci/ohci-ps3, ps3flash, rtc-ps3, spufs, spu_base, lv1call.h, ps3.h, boot/ps3.c, kernel/time.c, irq_64.c, ps3_defconfig) | GitHub API on torvalds/linux | yes |
| linuxppc-dev patchwork (patchwork.ozlabs.org, since 2024-01): "ps3", "gelic", "spufs", "spu", "lv1" | posted patches with their state | yes; lore.kernel.org refuses scripted access |
| psdevwiki OtherOS++ page | | refused (HTTP 403); nothing found elsewhere that is not in the sets above |

Not found anywhere: newer idle/decrementer fixes (our `local/0009` is the only one touching `ps3_power_save()`), snd_ps3 or ps3av fixes beyond cleanups, OHCI/EHCI PS3 fixes, a "PS3 extra memory" hotplug patch (the highmem region has been upstream since 2.6.2x), and any in-kernel temperature or fan driver. We wrote the last one ourselves: [THERMAL.md](THERMAL.md).

## Inventory

### A. glevand / psx-place set, already in our series (`packages/patched/kernel/linux/patches/ps3/`)

Config states are from the shipped `config-6.12.111+ps3.419`.

| Patch | Purpose | Risk | Decision |
|---|---|---|---|
| 0010 ps3stor-multiple-regions | ps3disk/ps3rom see every storage region (OtherOS++ region access); used for GameOS HDD reading | SAFE to read; writes to GameOS regions are possible through the block devices | keep |
| 0020 ps3stor-send-cmd-timeout | timeout instead of an endless wait on a storage command | SAFE | keep |
| 0030 ps3fb | `/dev/ps3gpu_*` (GPU FIFO, control, VRAM); RSX research, ps3rsx | CAREFUL (GPU state, run time) | keep |
| 0035 ps3flash (NG, NOR) | raw flash access | FORBIDDEN | built off (`PS3_FLASH*`, `PS3_NOR_FLASH`), keep off |
| 0040 ps3-partition | PS3 partition table parser | SAFE | compiled out (`PS3_PARTITION` unset) |
| 0050 ps3sysmgr-lpar-reboot | reboot asks the SM for an LPAR reboot instead of a system reboot | CAREFUL (SM request, run time) | keep |
| 0060 ps3sysmgr-char-device | `/dev/ps3sysmngr`: raw System Manager packets from userspace. Built in; while open, the kernel stops handling power-button and thermal events | FORBIDDEN-capable (any SM service; the SM fronts SYSCON; glevand's ps3sm-utils drive LEDs and the buzzer through it) | **dropped 2026-10-18** (decision 2) |
| 0070 ps3avmgr-char-device | `/dev/ps3avmgr`: raw AV manager packets. Built in, no use today | CAREFUL | keep, unused |
| 0080 ps3dispmgr | `/dev/ps3dispmngr`: raw dispatcher manager (update manager, storage manager, secure RTC). `=m`, autoloaded by its bus alias | FORBIDDEN-capable (update manager = firmware update and EEPROM writes) | **`PS3_DISP_MANAGER` off 2026-10-18** (decision 3); the patch stays in the series, nothing is built from it |
| 0090 ps3rom-vendor-specific-command | SCSI pass-through of vendor commands to the BD drive. Built in | CAREFUL, FORBIDDEN-capable (BD drive firmware commands) | **dropped 2026-10-18** (decision 4) |
| 0100 spu-enum-shared-param | enumerate the shared SPE as well (the parameter is `spu_base.enum_shared`) | SAFE | keep |
| 0110, 0120, 0130, 0190, 0260 lv1call additions | declarations for repository, storage-region, SPE and debug-console hcalls; needed by the others | SAFE | keep |
| 0140 ps3physmem | map all LPAR memory from userspace. `=m`, manual load | CAREFUL, FORBIDDEN-capable in principle (arbitrary memory writes) | keep, never autoloaded |
| 0150 ps3strgmngr | storage manager: create and delete storage regions | FORBIDDEN (persistent region table) | built off, keep off |
| 0160 ps3jupiter | glevand's direct Wi-Fi driver for the Jupiter chip over the internal USB | CAREFUL (needs 0170) | **dropped 2026-10-18** with 0170 and 0316 (decision 5) |
| 0170 gelic-disable-eurus-ctrl-iface | at gelic probe, clears this LPAR's bit in the HV repository node `ios.net.eurus.lpar` (`lv1_write_repository_node`). Runs every boot, and bypasses our `PS3_REPOSITORY_WRITE=n` choice | CAREFUL | **dropped 2026-10-18** (decision 1) |
| 0180 gelic-wireless-print-cmd-status | prints eurus command status; debugging | SAFE | keep |
| 0200, 0210 exports | export SPE irq and event-port helpers for 0230/0240 | SAFE | keep |
| 0220 ps3encdec | HDD encryption device | CAREFUL (keys) | built off |
| 0230 spuisofs, 0240 spuldrfs | load and run isolated SPU programs | CAREFUL (isolation mode, key material) | built off |
| 0250 ps3lv1call | arbitrary hypercalls from userspace. `=m`, manual load | FORBIDDEN-capable (any lv1 call, flash included) | keep, never autoloaded |
| 0270 udbg-lv1-console | early printk through the lv1 debug console | SAFE | built off (`PS3_LV1_CONS_UDBG` unset) |
| 0280 ps3_rtc_adj | `RTCAdj=` boot argument offsets the RTC | SAFE | keep |
| 0310 rtc-ps3 | `range_max` U64_MAX to S64_MAX | SAFE | keep |
| 0314 repository | 0110's new hcall signature in `repository.c` (write functions stay under `PS3_REPOSITORY_WRITE`) | SAFE | keep |
| 0316 jupiter-module-alias | module alias for 0160 | SAFE | dropped with 0160 |
| 0410 mmzone | relax the MAX_PAGE_ORDER vs SECTION_SIZE_BITS build check | SAFE | keep |
| 0420 ps3av_add | missing `#include <linux/fs.h>` for 0070 | SAFE | keep |
| 0430 gelic-wireless-miss-kfree | free the command on scan failure (T2's 0175 is the same) | SAFE | keep |
| 0440, 0450 lv1call/interrupt SRR clobber | mark SRR0/1 clobbered on every lv1 call; stability (bogus SRR warnings). Posted by René Rebe ([V2](https://patchwork.ozlabs.org/project/linuxppc-dev/patch/20251118.184901.486820791149579338.rene@exactco.de/), 2025-11-18, not merged); ours is identical to V2 | SAFE | keep |

None of the "built off" drivers is compiled. Already safe by configuration: `PS3_FLASH*`, `PS3_NOR_FLASH`, `PS3_STRGMNGR`, `PS3_ENCDEC`, `SPUISO_FS`, `SPULDR_FS` and `PS3_REPOSITORY_WRITE` are all off; `PS3_LV1CALL` and `PS3_PHYSMEM` are modules nothing loads automatically.

### B. Candidates from the other sources

All SAFE unless stated.

| Patch (source) | Purpose | 6.12? | Decision |
|---|---|---|---|
| net: ps3_gelic_net: handle skb allocation failures (Florian Fuchs, mainline 0f08f0b0fb5e) | an RX skb allocation failure no longer oopses in `gelic_net_poll()` or stalls RX; retries from a timer. Stability under memory pressure (NFS root/swap, our tight RAM) | after 6.12, needs `from_timer()` for `timer_container_of()` | **ported, local/0020** |
| net: ps3_gelic_net: napi_alloc_skb() and napi_gro_receive() (Florian Fuchs, mainline d8e08149a5ed) | NAPI skb cache and GRO on RX: more RX throughput, fewer retransmissions | yes (after 0020) | **ported, local/0021** |
| powerpc/ps3: cpu_relax() in ps3_create_spu() (Thorsten Blum, mainline 6050e3196d97) | boot-time spin lowers SMT priority | yes | **ported, local/0022** |
| spufs: take a reference on contexts pulled off the runqueue (Junrui Luo, [patchwork](https://patchwork.ozlabs.org/project/linuxppc-dev/patch/20260812-spufs-fixes-v1-3-449e201d9f64@outlook.com/), posted 2026-08-12) | use-after-free in the SPU scheduler when a context is closed while time-sliced. libspeq runs 8 contexts on 6 SPEs, so this path matters | yes | **ported, local/0023** |
| spufs: context state race in spu_acquire_saved() (same series) | two readers of saved-state files can end in `BUG_ON` | yes | **ported, local/0024** |
| spufs: mmap_lock/state_mutex lock inversion (same series) | deadlock in the problem-state fault path | yes | **ported, local/0025** |
| spufs: type confusion in cntl mmap fault handler (Zhenhao Wan, [patchwork](https://patchwork.ozlabs.org/project/linuxppc-dev/patch/20260814-spufs-groupa-v1-2-f38f7549ce20@gmail.com/)) | a fault on a `cntl` mapping used the `simple_attr` as the context (`simple_attr_open()` overwrites `private_data`; checked in our tree) | yes | **ported, local/0026** |
| spufs: gang->alive double-decrement (same series) | context creation in a gang dropped the gang's reference on success (bug from c134deabf478, which 6.12.111 has) | yes | **ported, local/0027** |
| spufs: don't leak kernel stack via spu_run (Junrui Luo, v2) | uninitialized `status` copied to userspace | yes | **ported, local/0028** |
| spufs: out-of-bounds read in spufs_wbox_info_read() | count scaled twice, up to 48 bytes of stack to userspace | yes | **ported, local/0029** |
| spufs: leak of spufs_fs_context in spufs_free_fc() | one allocation per mount attempt | yes | **ported, local/0030** |
| spufs: spu_context leak in coredump (v2) | reference leak per dumped context | yes | **ported, local/0031** |
| spufs: check permissions in spufs_setattr() (v2) | chmod/chown on spufs files without `setattr_prepare()` | yes | **ported, local/0032** |
| spufs: fix deadlock on gang creation failure (v2 4/5) | `unuse_gang()` under the parent lock | conflicts (written for the newer `start_creating_user_path()`) | not ported (we use no gangs) |
| spufs: don't hold state_mutex during user access (v2 5/5) | mailbox reads/writes drop the mutex around user copies; DoS only (userfaultfd) | yes | not ported (invasive for little gain) |
| spufs: coredump skip, memcg accounting, scnprintf | coredump detail, accounting feature, cosmetic | yes | not ported |
| powerpc/ps3: failure paths in ps3_create_spu() (Thorsten Blum, 2026-08) | error path when an SPE cannot be constructed | yes | not ported |
| cell: reference leak in create_spu() (Haoxiang Li) | error path | yes | not ported |
| powerpc/ps3: put_device() on device_register() failure (Tarun Sahu v4) | error path | conflicts | not ported |
| ps3/gelic: possible NULL dereference in release_tx_chain (Simon Horman RFC, 2024) | Smatch report, compile-tested only | yes | not ported (theoretical) |
| net/ps3_gelic_net: napi routines for RX SKB (Geoff, ps3-queue-v6.12, with two "fu" fixups) | `napi_alloc_frag_align()`/`napi_build_skb()` RX; unfinished upstream | yes | not ported (superseded by local/0021) |
| ps3/gelic: Fix SKB allocation (Geoff, fix-gelic-skb) | fix for a 6.8-rc1 bug, fixed upstream differently | n/a | not needed |
| net/ps3_gelic_net: Fix RX sk_buff length, dma_mapping_error (Geoff, for-merge-net-v9) | merged in 6.3 | already in | in tree |
| hvc_console: backends set I/O buffer size (Geoff) | no PS3 HVC backend exists | yes | not needed |
| ps3-debugging: DABR, IKCONFIG; ps3 defconfigs (Geoff) | debug and config | conflicts | not needed |
| ELFv2 lv1 hcall series (Geoff, 2024-01) | superseded by Piggin (6.8); we build ELFv1 for Petitboot's kexec | n/a | not needed |
| mainline after 6.12: dma_ioc0_map_pages failure path, repository.c build fix, ps3disk bvec index, spufs mem_mmap OOB, Al Viro's spufs leak/lifetime fixes | | already in 6.12.111 (stable) | in tree |
| ps3disk: bounce_size before it is set (Geert Uytterhoeven) | | conflicts: our 0010 already sets the limits after `bounce_size` | equivalent in tree |
| T2 1000-ps3disk-fix-bvec-memcpy | offset increment lost in 2021 | already in | in tree |
| T2 0009-ps3disk-blk_mq_queue_stopped | re-adds a removed blk-mq helper for T2's form of 0010 | yes | not needed |
| T2 0666-ps3fb-vram | ps3fb draws straight into the RSX DDR frame buffer: no XDR shadow, no blit kthread. XDR reservation 9 MB to the 512K command area, but its GPU allocation grows from 9 MB to 32 MB of VRAM, and ps3rsx reads ps3fb's command area and shares its handles. CAREFUL for us | conflicts with our 0030/0002-0005 | decision 6 below |
| T2 0667-ps3fb-cursor | hardware cursor ioctls (`lv1_gpu_context_attribute` 0x10b/0x10c); ps3rsx has its own KMS path. CAREFUL | not tried | not ported |
| T2 0700-ps3vram-mod-ps3fb | lets PS3_VRAM build with FB_PS3=m; we have FB_PS3=y | yes | not needed |
| T2 0020-ps3fb-use-fifo, 0600-ps3fb-ioctls (both disabled in T2) | older ps3fb FIFO/ioctl work, superseded by 0030. CAREFUL | | not ported |
| T2 ps3-gelic-skb-alloc (despite the name: `watermark_boost_factor` = 0 on PS3); Gentoo 0810 (same plus `min_free_kbytes` 4096) | no watermark boosting after fragmentation events; the patch is not needed, `vm.watermark_boost_factor` is a sysctl | | decision 7 below |
| T2 hotfix-execmem, -ether-unaligned, -drm-dma-coherent | sparc64 / ARM64 fixes | | not needed |
| Gentoo New/*, damiandudycz/* lv1 and ps3stor patches | the same patches split and renamed (0211-0213 = our 0010 per driver) | | covered by our series |

## What was ported

Thirteen patches in `packages/patched/kernel/linux/patches/local/` (0020-0032), each keeping the original `From:` and `Signed-off-by:` lines, an `Origin:` line and, for the unmerged ones, `Forwarded: not-needed`:

- **0020, 0021:** gelic RX allocation-failure handling and NAPI/GRO (upstream, Florian Fuchs). 0020 needed one change for 6.12: `from_timer()` instead of `timer_container_of()` (noted in the patch).
- **0022:** `cpu_relax()` in `ps3_create_spu()` (upstream, Thorsten Blum).
- **0023-0032:** ten spufs fixes posted in August 2026 (Junrui Luo, Zhenhao Wan) and not merged, since spufs has no maintainer. I read each against our 6.12 code before taking it, and the premises hold: `simple_attr_open()` overwriting `private_data`, `spufs_wbox_info_cnt()` returning bytes, `coredump_next_context()` taking a reference, `spufs_init_fs_context()` allocating two objects, `grab_runnable_context()` returning an unreferenced pointer with both callers dereferencing it. In 0023 the final `put_spu_context()` can now run in the scheduler. `destroy_spu_context()` deactivates the context itself, so that is safe, and it replaces a use-after-free.

None of them touches the hypervisor, flash or SYSCON. `make linux-deb` on 2026-10-18 (6.12.111+ps3.420~dev, both flavours) built with no warnings in any touched file.

**Boot results.** They booted in kernels .419 and .433. On .433 a 200 MB transfer over gelic and the spufs checks (libspeq tests, `make test-*` in ps3-spu, a game with SPU audio) passed. A gelic RX allocation failure under memory pressure, the case 0020 is for, has not been provoked on purpose.

## Decisions (the user, 2026-10-18)

Taken on kernel 6.12.111+ps3.4xx~dev, compile-tested at the time. Each item says what the issue was.

1. **0170 dropped, and with it 0160 (ps3jupiter) and 0316 (its alias).** 0170 writes the hypervisor repository on every boot: gelic is built in and at probe clears this LPAR's bit in the system LPAR's `ios.net.eurus.lpar` node (when the LAID is 0x1070000002000001 and hw config bit 0x40000 is set). That goes around `PS3_REPOSITORY_WRITE=n`, which `ps3.config` sets on purpose. The repository lives in hypervisor memory and the change is probably gone after a power cycle, but that was never confirmed. It served only the Jupiter driver, which we do not build. Nothing else depended on it; 0180 only prints eurus command status in the stock gelic wireless driver. Internal Wi-Fi via ps3jupiter was therefore dropped too; the stock gelic wireless path (`GELIC_WIRELESS=y`) is the alternative. (If you want to check an old kernel: `dmesg | grep -i eurus`, "was disabled" means it wrote.)
2. **0060 dropped (no `/dev/ps3sysmngr`).** Root could send any System Manager packet, and which SM services change persistent state is not documented. While the device is open the kernel also stops handling SM events, including the thermal alert our cooling rule depends on. `local/0040` was reworked not to use its misc device ([THERMAL.md](THERMAL.md)).
3. **`# CONFIG_PS3_DISP_MANAGER is not set`** in `packages/patched/kernel/linux/config/ps3.config`. The dispatcher manager carries the update manager (firmware update, EEPROM writes), and nothing in our tree uses it.
4. **0090 dropped.** It passed arbitrary vendor SCSI commands to the BD drive (root only). A grep of the repo found no user. `PS3_ROM` itself stays (`PS3_ROM=y`).
5. Internal Wi-Fi: see 1.
6. **T2's ps3fb-in-VRAM (0666): not taken.** It would give up to 8.5 MB of XDR back (the console shadow), but costs 23 MB more VRAM, which is scarce (VRAM doubles as swap, see [KERNEL-MEMORY.md](KERNEL-MEMORY.md)), and it conflicts with how ps3rsx shares ps3fb's command area. A reworked version that keeps 9 MB of VRAM is a design job, not a port. Instead `local/0043` frees 8 of the 9 MB of XDR only while ps3rsx owns the GPU, with no VRAM change (switch `ps3fb.shadow_release`, off until measured; [KERNEL-MEMORY.md](KERNEL-MEMORY.md)).
7. **`vm.watermark_boost_factor=0`** (T2, Gentoo): a sysctl in ps3-system's `60-ps3-memory.conf`, no kernel change. Measured 2026-10-18 on [Luanti](../gamedb/games/luanti/) with the kicker on (15000/0/15000/0): gameplay stalls 12.5 / 0.8 / 2.0 / 2.1 s, the first run an outlier. No clear gain, so it is not shipped (results row "smoke re-runs ... vm.watermark_boost_factor A/B"; protocol in [STALLS.md](STALLS.md)).

## Left open

Every candidate in the survey has a decision, and nothing here waits on the user. These are worth knowing for later:

- `/dev/ps3avmgr` (0070, CAREFUL) is built in and unused. It is the one raw manager channel left built in; the survey kept it.
- `ps3lv1call` (0250) and `ps3physmem` (0140) are modules that nothing loads. The survey's rule is to keep them manual-load only and, as [research/FLASH-SYSCON.md](research/FLASH-SYSCON.md) says, keep lv1call and the dispatcher manager blacklisted while ps3rsx runs.
- Decision 6's reworked ps3fb-in-VRAM (a 9 MB VRAM variant) is a design job nobody has started, and `local/0043` is measured only when its switch is turned on ([TESTS.md](TESTS.md), [KERNEL-MEMORY.md](KERNEL-MEMORY.md)).
- The 7th SPE needs no patch (0100 is in the series): boot with `spu_base.enum_shared=1`. Not tried yet; see [backlog/hardware.md](backlog/hardware.md).

Older versions and the dated history: [archive/OTHEROS-PATCHES.md](archive/OTHEROS-PATCHES.md).
