> Superseded on 2026-10-19 by docs/OTHEROS-PATCHES.md; kept unchanged for its history.

# OtherOS++ and PS3 kernel patches: survey for 6.12

Survey of 2026-10-18 (agent `oospp`). Question: which PS3 kernel patches exist outside our tree
(glevand's OtherOS++ work, Geoff Levand's queue, T2 SDE, Gentoo, mainline after 6.12, patches posted
to linuxppc-dev and never merged), which of them we lack on 6.12.111, and which are safe to carry.
Risk classes follow the PS3 safety rules in CLAUDE.md:

- **SAFE**: read-only or a plain software fix; no hypervisor, flash or syscon state changes.
- **CAREFUL**: changes hardware or hypervisor state at run time only (gone after a power cycle).
- **FORBIDDEN**: can write flash, EEPROM, syscon or other persistent state. Never enabled or ported.
  A driver that *gives root a raw channel* to such state is marked "FORBIDDEN-capable".

"6.12?" says whether the patch applies to our tree (Debian 6.12.111 + our series), checked with
`git apply --check` forward and reverse in `build/work/linux`.

## Where the patches come from

| Source | What it holds | Checked |
|---|---|---|
| psx-place set (Sombat Tragoongoson), via [damiandudycz/ps3](https://github.com/damiandudycz/ps3) `files/kernel-patches/psx-place-Sombat-Tragoongoson/6.12` | glevand's OtherOS++ patches (gitbrew era, numbering 0010-0260) refreshed up to 6.12, plus 0280-0450 fixes | already our `linux/patches/ps3/` (36 patches) |
| glevand on GitHub (`glevand/ps3-linux`, `ps3-linux-patches`) | | gone: the repositories don't answer (no such repo); the 2012 gists ([3.3.x](https://gist.github.com/2476441), [3.2.x](https://gist.github.com/2476405)) list the same 0010-0260 set |
| Geoff Levand, [kernel.org geoff/ps3-linux.git](https://git.kernel.org/pub/scm/linux/kernel/git/geoff/ps3-linux.git/) | `ps3-queue-v6.12`, `-v6.13`, `fix-gelic-skb`, `for-merge-net-v9`, `for-merge-hvc-v1` (shallow fetch, depth 60) | yes |
| T2 SDE (René Rebe), `svn.exactcode.de/t2/trunk/architecture/powerpc64/package/linux/` and `package/kernel/linux/` | glevand's set in T2 form, ps3fb/ps3vram changes, generic hotfixes | yes (current trunk) |
| Gentoo (Damian Dudycz), same repository, `dev-tools/kernel-patches/patches/{PSXPlace,T2SDE,New,damiandudycz}` | the two sets above split and renamed, plus his own mm and lv1 changes | yes |
| Mainline, commits since 2024-07 on every PS3 path (platforms/ps3, drivers/ps3, gelic, ps3disk, ps3vram, ps3rom, ps3fb, snd_ps3, ehci/ohci-ps3, ps3flash, rtc-ps3, spufs, spu_base, lv1call.h, ps3.h, boot/ps3.c, kernel/time.c, irq_64.c, ps3_defconfig) | GitHub API on torvalds/linux | yes |
| linuxppc-dev patchwork (patchwork.ozlabs.org, since 2024-01): "ps3", "gelic", "spufs", "spu", "lv1" | posted patches with their state (accepted / new / superseded) | yes; lore.kernel.org refuses scripted access |
| psdevwiki OtherOS++ page | | refused (HTTP 403); nothing found elsewhere that isn't in the sets above |

Not found anywhere: newer idle/decrementer fixes (our local/0009 is the only one touching
`ps3_power_save()`), snd_ps3 or ps3av fixes beyond cleanups, OHCI/EHCI PS3 fixes, a "PS3 extra
memory" hotplug patch (the highmem region has been upstream since 2.6.2x), and any in-kernel
temperature or fan driver.

## Inventory

### A. glevand / psx-place set, already in our series (`linux/patches/ps3/`)

Config states from the shipped `config-6.12.111+ps3.419`.

| Patch | Purpose | Upstream? | 6.12? | Relevance | Risk | Decision |
|---|---|---|---|---|---|---|
| 0010 ps3stor-multiple-regions | ps3disk/ps3rom see every storage region (OtherOS++ region access) | no | in series (refreshed) | storage; GameOS HDD reading | SAFE to read; writes to GameOS regions are possible through the block devices | keep |
| 0020 ps3stor-send-cmd-timeout | timeout instead of an endless wait on a storage command | no | in series | stability | SAFE | keep |
| 0030 ps3fb | /dev/ps3gpu_* (GPU FIFO, control, VRAM) | no | in series | RSX research, ps3rsx | CAREFUL (GPU state, run time) | keep |
| 0035 ps3flash (NG, NOR) | raw flash access | no | in series | none | FORBIDDEN | built off (`PS3_FLASH*`, `PS3_NOR_FLASH`), keep off |
| 0040 ps3-partition | PS3 partition table parser | no | in series | none | SAFE | compiled out (`PS3_PARTITION` unset) |
| 0050 ps3sysmgr-lpar-reboot | reboot asks the SM for an LPAR reboot instead of a system reboot | no | in series | boot path | CAREFUL (SM request, run time) | keep |
| 0060 ps3sysmgr-char-device | /dev/ps3sysmngr: raw System Manager packets from userspace | no | in series, **built in** | thermal/fan work (see below) | **FORBIDDEN-capable** (any SM service: the kernel itself uses only requests, attributes and the next boot op; glevand's ps3sm-utils drive LEDs and the buzzer through it, and the SM fronts syscon); while open, the kernel stops handling power-button and thermal events | **dropped 2026-10-18** (decision 2) |
| 0070 ps3avmgr-char-device | /dev/ps3avmgr: raw AV manager packets | no | in series, built in | none today | CAREFUL | keep, unused |
| 0080 ps3dispmgr | /dev/ps3dispmngr: raw dispatcher manager (update manager, storage manager, secure RTC...) | no | in series, `=m`, **autoloaded** by its bus alias | none today | **FORBIDDEN-capable** (update manager = firmware update and EEPROM writes) | **`PS3_DISP_MANAGER` off 2026-10-18** (decision 3; the patch stays in the series, nothing is built from it) |
| 0090 ps3rom-vendor-specific-command | SCSI pass-through of vendor commands to the BD drive | no | in series, built in | none | CAREFUL, FORBIDDEN-capable (BD drive firmware commands) | **dropped 2026-10-18** (decision 4) |
| 0100 spu-enum-shared-param | enumerate the shared SPE as well | no | in series | SPU count | SAFE | keep |
| 0110, 0120, 0130, 0190, 0260 lv1call additions | declarations for repository, storage-region, SPE and debug-console hcalls | no | in series | needed by the others | SAFE (declarations) | keep |
| 0140 ps3physmem | map all LPAR memory from userspace | no | in series, `=m`, manual load | research | CAREFUL, FORBIDDEN-capable in principle (arbitrary memory writes) | keep, never autoloaded |
| 0150 ps3strgmngr | storage manager: create/delete storage regions | no | in series | none | FORBIDDEN (persistent region table) | built off, keep off |
| 0160 ps3jupiter | glevand's direct Wi-Fi driver for the Jupiter chip over the internal USB | no | in series | Wi-Fi without the HV's eurus interface | CAREFUL (needs 0170) | **dropped 2026-10-18** with 0170 and the alias patch 0316 (decision 5) |
| 0170 gelic-disable-eurus-ctrl-iface | at gelic probe, clears this LPAR's bit in the HV repository node `ios.net.eurus.lpar` (`lv1_write_repository_node`) | no | in series, **runs every boot** (gelic built in) | only useful with 0160 | **CAREFUL**, and it bypasses our `PS3_REPOSITORY_WRITE=n` choice | **dropped 2026-10-18** (decision 1) |
| 0180 gelic-wireless-print-cmd-status | prints eurus command status | no | in series | debugging | SAFE | keep |
| 0200, 0210 exports | export SPE irq and event-port helpers for 0230/0240 | no | in series | none | SAFE | keep |
| 0220 ps3encdec | HDD encryption device | no | in series | none | CAREFUL (keys) | built off |
| 0230 spuisofs, 0240 spuldrfs | load and run isolated SPU programs | no | in series | none | CAREFUL (isolation mode, key material) | built off |
| 0250 ps3lv1call | arbitrary hypercalls from userspace | no | in series, `=m`, manual load | research | **FORBIDDEN-capable** (any lv1 call, flash included) | keep, never autoloaded |
| 0270 udbg-lv1-console | early printk through the lv1 debug console | no | in series | early debugging | SAFE | built off (`PS3_LV1_CONS_UDBG` unset) |
| 0280 ps3_rtc_adj | `RTCAdj=` boot argument offsets the RTC | no | in series | clock | SAFE | keep |
| 0310 rtc-ps3 | `range_max` U64_MAX -> S64_MAX | no | in series | clock | SAFE | keep |
| 0314 repository | 0110's new hcall signature in repository.c (write functions stay under `PS3_REPOSITORY_WRITE`) | no | in series | needed | SAFE | keep |
| 0316 jupiter-module-alias | module alias for 0160 | no | in series | with 0160 | SAFE | keep |
| 0410 mmzone | relax the MAX_PAGE_ORDER vs SECTION_SIZE_BITS build check | no | in series | build | SAFE | keep |
| 0420 ps3av_add | missing `#include <linux/fs.h>` for 0070 | no | in series | build | SAFE | keep |
| 0430 gelic-wireless-miss-kfree | free the command on scan failure (T2's 0175 is the same) | no | in series | leak fix | SAFE | keep |
| 0440, 0450 lv1call/interrupt SRR clobber | mark SRR0/1 clobbered on every lv1 call | posted by René Rebe ([V2](https://patchwork.ozlabs.org/project/linuxppc-dev/patch/20251118.184901.486820791149579338.rene@exactco.de/), 2025-11-18, not merged) | in series; identical to V2 | stability (bogus SRR warnings) | SAFE | keep |

### B. Candidates from the other sources

| Patch (source) | Purpose | Upstream? | 6.12? | Relevance | Risk | Decision |
|---|---|---|---|---|---|---|
| net: ps3_gelic_net: handle skb allocation failures (Florian Fuchs, mainline 0f08f0b0fb5e) | an RX skb allocation failure no longer oopses in `gelic_net_poll()` or stalls RX; retries from a timer | yes, after 6.12; not in 6.12.111 | needs `from_timer()` for `timer_container_of()` | **stability** under memory pressure (NFS root/swap, our tight RAM) | SAFE | **ported, local/0020** |
| net: ps3_gelic_net: napi_alloc_skb() and napi_gro_receive() (Florian Fuchs, mainline d8e08149a5ed) | NAPI skb cache and GRO on RX: more RX throughput, fewer retransmissions | yes, after 6.12 | yes (after 0020) | NFS performance | SAFE | **ported, local/0021** |
| powerpc/ps3: cpu_relax() in ps3_create_spu() (Thorsten Blum, mainline 6050e3196d97) | boot-time spin lowers SMT priority | yes, after 6.12 | yes | trivial | SAFE | **ported, local/0022** |
| spufs: take a reference on contexts pulled off the runqueue (Junrui Luo, [patchwork](https://patchwork.ozlabs.org/project/linuxppc-dev/patch/20260812-spufs-fixes-v1-3-449e201d9f64@outlook.com/)) | use-after-free in the SPU scheduler when a context is closed while time-sliced | no (posted 2026-08-12) | yes | **libspeq runs 8 contexts on 6 SPEs**, i.e. this path | SAFE | **ported, local/0023** |
| spufs: context state race in spu_acquire_saved() (same series) | two readers of saved-state files can end in `BUG_ON` | no | yes | SPU tools reading regs/mbox | SAFE | **ported, local/0024** |
| spufs: mmap_lock/state_mutex lock inversion (same series) | deadlock in the problem-state fault path | no | yes | SPU mmaps | SAFE | **ported, local/0025** |
| spufs: type confusion in cntl mmap fault handler (Zhenhao Wan, [patchwork](https://patchwork.ozlabs.org/project/linuxppc-dev/patch/20260814-spufs-groupa-v1-2-f38f7549ce20@gmail.com/)) | a fault on a `cntl` mapping used the `simple_attr` as the context (`simple_attr_open()` overwrites `private_data`; checked in our tree) | no | yes | any problem-state mmap from libspeq later | SAFE | **ported, local/0026** |
| spufs: gang->alive double-decrement (same series) | context creation in a gang dropped the gang's reference on success (bug from c134deabf478, which 6.12.111 has) | no | yes | gangs (unused today) | SAFE | **ported, local/0027** |
| spufs: don't leak kernel stack via spu_run (Junrui Luo, v2) | uninitialized `status` copied to userspace | no | yes | correctness | SAFE | **ported, local/0028** |
| spufs: out-of-bounds read in spufs_wbox_info_read() | count scaled twice, up to 48 bytes of stack to userspace | no | yes | correctness | SAFE | **ported, local/0029** |
| spufs: leak of spufs_fs_context in spufs_free_fc() | one allocation per mount attempt | no | yes | leak | SAFE | **ported, local/0030** |
| spufs: spu_context leak in coredump (v2) | reference leak per dumped context | no | yes | SPU program crashes | SAFE | **ported, local/0031** |
| spufs: check permissions in spufs_setattr() (v2) | chmod/chown on spufs files without `setattr_prepare()` | no | yes | correctness | SAFE | **ported, local/0032** |
| spufs: fix deadlock on gang creation failure (v2 4/5) | `unuse_gang()` under the parent lock | no | conflicts (written for the newer `start_creating_user_path()`) | gangs only | SAFE | not ported (we use no gangs) |
| spufs: don't hold state_mutex during user access (v2 5/5) | mailbox reads/writes drop the mutex around user copies | no | yes | DoS only (userfaultfd) | SAFE | not ported (invasive for little gain) |
| spufs: coredump skip, memcg accounting, scnprintf | coredump detail, accounting feature, cosmetic | no | yes | low | SAFE | not ported |
| powerpc/ps3: failure paths in ps3_create_spu() (Thorsten Blum, 2026-08) | error path when an SPE can't be constructed | no | yes | error path only | SAFE | not ported |
| cell: reference leak in create_spu() (Haoxiang Li) | error path | no | yes | error path only | SAFE | not ported |
| powerpc/ps3: put_device() on device_register() failure (Tarun Sahu v4) | error path | no | conflicts | error path only | SAFE | not ported |
| ps3/gelic: possible NULL dereference in release_tx_chain (Simon Horman RFC, 2024) | Smatch report, compile-tested only; moves the dereference into the error branch | no | yes | theoretical | SAFE | not ported |
| net/ps3_gelic_net: napi routines for RX SKB (Geoff, ps3-queue-v6.12, with two "fu" fixups) | `napi_alloc_frag_align()`/`napi_build_skb()` RX | no (unfinished) | yes | superseded by local/0021 | SAFE | not ported |
| ps3/gelic: Fix SKB allocation (Geoff, fix-gelic-skb) | fix for a 6.8-rc1 bug | fixed upstream differently | n/a | none | SAFE | not needed |
| net/ps3_gelic_net: Fix RX sk_buff length, dma_mapping_error (Geoff, for-merge-net-v9) | | yes (6.3) | already in | | SAFE | in tree |
| hvc_console: backends set I/O buffer size (Geoff) | buffer sizes per HVC backend | no | yes | no PS3 HVC backend exists | SAFE | not needed |
| ps3-debugging: DABR, IKCONFIG; ps3 defconfigs (Geoff) | debug and config | no | conflicts | none | SAFE | not needed |
| ELFv2 lv1 hcall series (Geoff, 2024-01) | lv1 calls under ELFv2 | superseded (Piggin, 6.8) | n/a | we build ELFv1 for Petitboot's kexec | SAFE | not needed |
| mainline after 6.12: dma_ioc0_map_pages failure path, repository.c build fix, ps3disk bvec index, spufs mem_mmap OOB, Al Viro's spufs leak/lifetime fixes | | yes | already in 6.12.111 (stable) | | SAFE | in tree |
| ps3disk: bounce_size before it is set (Geert Uytterhoeven) | | yes | conflicts: our 0010 already sets the limits after `bounce_size` | | SAFE | equivalent in tree |
| T2 1000-ps3disk-fix-bvec-memcpy | offset increment lost in 2021 | yes | already in | storage | SAFE | in tree |
| T2 0009-ps3disk-blk_mq_queue_stopped | re-adds a removed blk-mq helper for T2's form of 0010 | no | yes | our 0010 doesn't need it | SAFE | not needed |
| T2 0666-ps3fb-vram | ps3fb draws straight into the RSX DDR frame buffer: no XDR shadow, no blit kthread | no | conflicts with our 0030/0002-0005 | **memory**: XDR reservation 9 MB -> the 512K command area; but its GPU allocation grows from 9 MB to 32 MB of VRAM, and ps3rsx reads ps3fb's command area and shares its handles | SAFE (driver logic), CAREFUL for us | decision 6 |
| T2 0667-ps3fb-cursor | hardware cursor ioctls (`lv1_gpu_context_attribute` 0x10b/0x10c) | no | not tried | ps3rsx has its own KMS path | CAREFUL | not ported |
| T2 0700-ps3vram-mod-ps3fb | lets PS3_VRAM build with FB_PS3=m | no | yes | we have FB_PS3=y | SAFE | not needed |
| T2 0020-ps3fb-use-fifo, 0600-ps3fb-ioctls (both disabled in T2) | older ps3fb FIFO/ioctl work | no | | superseded by 0030 | CAREFUL | not ported |
| T2 ps3-gelic-skb-alloc (despite the name: `watermark_boost_factor` = 0 on PS3); Gentoo 0810 (same plus `min_free_kbytes` 4096) | no watermark boosting after fragmentation events | no | patch not needed: `vm.watermark_boost_factor` is a sysctl | memory, stalls (kswapd bursts) | SAFE | decision 7 (measure first) |
| T2 hotfix-execmem, -ether-unaligned, -drm-dma-coherent | sparc64 / ARM64 fixes | | | not our architecture | | not needed |
| Gentoo New/*, damiandudycz/* lv1 and ps3stor patches | the same patches split and renamed (0211-0213 = our 0010 per driver) | | | | | covered by our series |

## What was ported

13 patches in `linux/patches/local/` (0020-0032; 0010 is reserved by the thermal agent), each with
the original `From:`/`Signed-off-by:` lines, an `Origin:` line and, for the unmerged ones,
`Forwarded: not-needed`:

- 0020, 0021: gelic RX allocation-failure handling and NAPI/GRO (upstream, Florian Fuchs). 0020
  needed one change for 6.12: `from_timer()` instead of `timer_container_of()` (noted in the patch).
- 0022: `cpu_relax()` in `ps3_create_spu()` (upstream, Thorsten Blum).
- 0023-0032: ten spufs fixes posted in August 2026 (Junrui Luo, Zhenhao Wan) and not merged
  (spufs has no maintainer). I read each against our 6.12 code before taking it: the premises hold
  (`simple_attr_open()` overwriting `private_data`, `spufs_wbox_info_cnt()` returning bytes,
  `coredump_next_context()` taking a reference, `spufs_init_fs_context()` allocating two objects,
  `grab_runnable_context()` returning an unreferenced pointer with both callers dereferencing it).
  In 0023 the final `put_spu_context()` can now run in the scheduler; `destroy_spu_context()`
  deactivates the context itself, so that is safe, and it replaces a use-after-free.

None of them touches the hypervisor, flash or syscon. Build: `make linux-deb` on 2026-10-18
(6.12.111+ps3.420~dev, both flavours) built with no warnings in any touched file (gelic built in,
spufs as a module, ps3/spu.c); the test debs are in `build/keep/debs-linux-6.12.111+ps3.420dev-oospp`,
not deployed. Nothing is booted yet: they need a PS3 run before the next
stable kernel (gelic: an `scp` of a large file plus a game, watching dmesg; spufs: the libspeq
tests, `make test-*` in ps3-spu, and a game with SPU audio). (2026-10-18 evening: booted in kernels
.419 and .433; on .433 a 200 MB transfer over gelic and the spufs checks passed. A gelic RX allocation
failure under memory pressure, the case 0020 is for, has not been provoked on purpose.)

## For the user's decision (CAREFUL and FORBIDDEN-capable items)

Decided 2026-10-18 by the user (kernel 6.12.111+ps3.4xx~dev, compile-tested, not booted):
1 drop 0170 and with it 0160 (ps3jupiter) and 0316 (its module alias); 2 drop 0060 (no
`/dev/ps3sysmngr`; local/0040 was reworked to not use its misc device); 3 `PS3_DISP_MANAGER` unset;
4 drop 0090. The decision notes under each item below say what was done. `ps3-sm-temp.sh` and the
raw-device parts of docs/THERMAL.md talk to `/dev/ps3sysmngr` and no longer have a device.

1. **0170 writes the hypervisor repository on every boot.** gelic is built in, and at probe it
   clears this LPAR's bit in the system LPAR's `ios.net.eurus.lpar` node (when the LAID is
   0x1070000002000001 and hw config bit 0x40000 is set). It goes around `PS3_REPOSITORY_WRITE=n`,
   which ps3.config sets on purpose. The repository lives in hypervisor memory and I expect the
   change to be gone after a power cycle, but I could not confirm that, so treat it as unknown.
   It only serves the Jupiter driver (0160), which we don't build. Check
   `dmesg | grep -i eurus` on the PS3: "was disabled" means it wrote, "already disabled" means an
   earlier boot did and it survived at least a reboot. Recommendation: drop 0170 from the series
   (or put the write behind a module parameter that defaults to off).
   **Decision 2026-10-18: dropped** (series, with 0160 and 0316). Nothing else depended on it;
   0180 only prints eurus command status in the stock gelic wireless driver.
2. **/dev/ps3sysmngr (0060) is built in.** Root can send any System Manager packet; the SM fronts
   syscon, and which of its services change persistent state is not documented. Also, while it is open the
   kernel stops handling SM events, *including the thermal alert* our cooling rule depends on.
   The thermal agent reads temperatures in-kernel instead; if nothing uses the raw device, make it
   opt-in (module parameter) or drop 0060.
   **Decision 2026-10-18: dropped.** local/0040 used 0060's `struct ps3sm` only to return -EBUSY
   while the device was open; that and the misc device are gone, the hwmon code is otherwise unchanged.
3. **/dev/ps3dispmngr (0080) loads automatically** (`PS3_DISP_MANAGER=m`, bus alias `ps3:13:0`).
   The dispatcher manager carries the update manager (firmware update, EEPROM writes). Nothing in
   our tree uses it. Recommendation: `PS3_DISP_MANAGER` off, or blacklist the module.
   **Decision 2026-10-18: `# CONFIG_PS3_DISP_MANAGER is not set`** in linux/config/ps3.config.
4. **0090 ps3rom vendor commands** pass arbitrary vendor SCSI commands to the BD drive (built in
   with `PS3_ROM=y`). Root only; nothing uses it. Keep or drop; worth knowing.
   **Decision 2026-10-18: dropped** (grep of the repo found no user; `PS3_ROM` itself stays).
5. **Internal Wi-Fi via ps3jupiter (0160)**: needs 0170's repository write. Not recommended
   unless Wi-Fi is wanted; the stock gelic wireless path (`GELIC_WIRELESS=y`) is the alternative.
   **Decision 2026-10-18: 0160 dropped** (with 0170 and 0316).
6. **T2's ps3fb-in-VRAM (0666)**: up to 8.5 MB of XDR back (the console shadow), but 23 MB more
   VRAM, which is scarce here (vswap), and it conflicts with how ps3rsx shares ps3fb's command
   area. A reworked version that keeps 9 MB of VRAM is possible; it is a design job, not a port.
   Instead (2026-10-18): local/0043 frees 8 of the 9 MB of XDR only while ps3rsx owns the GPU,
   no VRAM change (switch `ps3fb.shadow_release`, off until measured; docs/KERNEL-MEMORY.md 6).
7. **`vm.watermark_boost_factor=0`** (T2, Gentoo): a sysctl in ps3-system's
   `60-ps3-memory.conf`, no kernel change. Might reduce kswapd bursts after fragmentation; measure
   it against its off-switch in one boot (docs/STALLS.md protocol) before shipping.
   **Measured 2026-10-18** (Luanti, kicker on, 15000/0/15000/0): gameplay stalls 12.5 / 0.8 / 2.0 /
   2.1 s, the first run an outlier: no clear gain, not shipped (results row "smoke re-runs ...
   vm.watermark_boost_factor A/B").

Already safe by configuration, keep as is: `PS3_FLASH*`, `PS3_NOR_FLASH`, `PS3_STRGMNGR`,
`PS3_ENCDEC`, `SPUISO_FS`, `SPULDR_FS`, `PS3_REPOSITORY_WRITE` all off; `PS3_LV1CALL` and
`PS3_PHYSMEM` are modules nothing loads automatically.
