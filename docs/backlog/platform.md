# Backlog: the system around the games

Platform work not done: the kernel guard and the SSD root's follow-ups, game mode and the power
paths at the TV, the desktop, input, network, display, the dev loop, and, last, the release work.
How the desktop and game mode work today is in [DESKTOP.md](../DESKTOP.md), the SSD image in
[GAMEOS-HDD.md](../GAMEOS-HDD.md), the packages and releases in [PACKAGING.md](../PACKAGING.md) and
[the archived release plan](../archive/RELEASE-CANDIDATES-2026-10-19.md). Items are in rank order; the rank is the one in
[the ranked list](README.md). Terms: [glossary](../README.md#glossary).

Effort: S = a day or less, M = days, L = weeks. "User" means the user at the console or TV.

## Storage

### The kernel guard on the PS3

Rank 1. local/0045 booted and checked: `ps3da` read-only for good, `ps3db` writable only inside the
ranges a boot tool registers and seals once per boot, `ps3dc` writable.

**Why.** Since 2026-10-19 the root file system lives in an image file on GameOS's disk (`dev_hdd0`,
`ps3db`), written through dm-linear devices over the file's blocks. The boot tools verify every
block's stamp and recheck the map before they create a device, but the running kernel (.472) has no
guard: a wrong table or a stray write could reach GameOS's data, and that would be a permanent
change to the console, which the safety rules forbid. The guard makes the kernel refuse every write
to `ps3db` outside the image's sealed ranges.

- **Payoff:** safety for GameOS's disk at every SSD boot. **Effort:** S (built).
- **Risk:** low: every write test writes back data already there, so even a broken guard changes
  nothing; a refusal at boot stops in the rescue shell, and the USB entry boots an older kernel.
- **Needs:** host (a kernel deb with 0045, the initramfs rebuilt for that release with
  `guard=required`, the new `ps3-imgmap` copied), the user (a reboot into the guard kernel), then
  PS3.
- **Status:** built and tested on the PC (suites `ps3disk-guard`, `ssdimg`, `ssdimg-vm` with a module
  running the patch's own guard code), in the series since 1abb2a1, not booted. Scheduled: TESTS.md
  "Next" (the user reboots into the guard kernel).
- **Source:** [archive/GAMEOS-HDD.md, "The kernel guard"](../archive/GAMEOS-HDD.md#the-kernel-guard-local0045-built-2026-10-19-in-the-series-not-booted-yet).

**To start.** GAMEOS-HDD.md's five steps: the kernel deb (`make linux-deb`, then `make
ps3rsx-modules`) and the tools to the PS3; on the USB root, `dmesg | grep 'write guard'`, the
policies in `/sys/block/ps3d?/write_guard/`, `blockdev --setrw /dev/ps3da` refused, and a write-back
of the image's header block 5 refused with EIO; Phase A's `ps3-imgmap create` sealing its 513
ranges; the initramfs for the guard kernel; the first SSD boot on it with `refused` still 0 after
the desktop and a bench run.

### SSD root follow-ups

Rank 34. What the SSD image still needs before it can stand without the USB disk.

- **The image header** (designed 2026-10-19, not built): a header in the image's first MiB (magic,
  UUID, the extent list's sha256, a generation, room for a 1016 KiB boot bundle), so the boot map can
  be checked against the image itself and the state needn't live on the USB root. Writing it
  collides with the guard: the design's option 2 spends one boot on `ps3-imgmap header-write`
  (register only the header's blocks, seal, write, read back), and every initramfs and Phase A tool
  must know the relaxed rule before any header is written.
- **`/boot` on dev_hdd1, kept by webMAN** (draft `packages/walnut/system/ps3-boot/webman/autoexec.bat`, not tried): Linux
  writes kernels to `/dev_hdd1/linux/boot/` and `/dev_hdd1/kboot.conf`; at every GameOS start webMAN
  MOD's `autoexec.bat` mirrors them to `/dev_hdd0/linux/boot/` and copies them back if GameOS cleared
  the cache partition. To check: that webMAN's `copy` takes a directory and how long the mirror
  takes, that Petitboot sees `ps3dc` (its shell's `/proc/partitions`), and that GameOS leaves files
  at dev_hdd1's root (a file left there through a few GameOS sessions and games tells).
- **Kernel updates in one step** for both roots (a hook that rebuilds the SSD initramfs and its boot
  entry).
- **An early seal on USB-root boots:** an optional unit that registers an empty range list and seals
  the guard early, so a USB-root boot can't write `ps3db` at all (from the guard's design; not built).
- **Open questions:** whether
  GameOS ever moves a file's blocks (no defragmenter is known; a GameOS fsck after an unclean
  power-off might; the boot recheck catches it either way); whether the installed Petitboot really
  ignores `kboot.conf`'s `default=` and timeout (read from its image; to be confirmed at the TV).

- **Effort:** M each. **Risk:** low-medium (boot paths; the USB entry stays the fallback).
- **Needs:** host, PS3, the user for the boots. **Status:** designs and a draft, nothing run.
- **Source:** PLAN 1.14; [archive/GAMEOS-HDD.md, "Later"](../archive/GAMEOS-HDD.md#later-for-a-release-not-needed-for-the-benchmark), ["The image header"](../archive/GAMEOS-HDD.md#the-image-header-design-2026-10-19-not-built), ["/boot on dev_hdd1 kept alive by webMAN"](../archive/GAMEOS-HDD.md#boot-on-dev_hdd1-kept-alive-by-webman-proposed-2026-10-18-not-tried).

### The SSD root image is nearly full

Rank 77. The root is a 15 GiB ext4 in a 16 GiB file and about 83% full (2026-10-20). Many more
games, or Nexuiz (about 800 MB) and Xonotic (1.2 GB), need room first.

- Growing the file needs a GameOS-side upload of a larger stamped image (the user's step; the
  16 GiB file is uploaded to `dev_hdd0` and mapped, [GAMEOS-HDD.md](../GAMEOS-HDD.md)), so
  it is decided before the next batch of games, not during one.
- **Payoff:** room for the games on the list. **Effort:** M. **Needs:** the user. **Status:** not
  started.

### ps3disk multi-region (packages/patched/kernel/linux/patches/ps3/0010): two bugs

Rank 78. Reported 2026-10-18, not fixed. Both are in `ps3disk_probe` and `ps3disk_remove` of
[0010-ps3stor-multiple-regions.patch](../../packages/patched/kernel/linux/patches/ps3/0010-ps3stor-multiple-regions.patch);
neither has been hit on the console (the probe succeeds and remove runs at shutdown).

- **The probe error path can loop forever.** `devidx` is `unsigned int`, and `fail_free_tag_set`
  runs `for (; devidx >= 0; devidx--)`, which is always true; at 0 it wraps and walks
  `priv->gendisk[]` off its end. It starts when `blk_mq_alloc_sq_tag_set`, `blk_mq_alloc_disk` or
  `device_add_disk` fails. A signed index (or a count-down loop on `devidx + 1`) fixes it.
- **Remove can let an interrupt touch freed memory.** `ps3disk_remove` deletes every gendisk and
  frees its tag set, but leaves the pointers in `priv->gendisk[]` and calls `ps3stor_teardown`
  (which frees the interrupt) last. `ps3disk_interrupt` still walks `priv->gendisk[]` and calls
  `blk_mq_run_hw_queues(gdisk->queue)` on each non-NULL entry, so a completion arriving in that
  window uses a freed queue. `.shutdown` is the same function. Fix: clear each pointer under
  `priv->lock` as it is released (or tear the interrupt down first).
- **Payoff:** a kernel that survives a failed probe and a clean remove. **Effort:** S (host build;
  boot test by the user). **Risk:** low; the disk paths are the guard's too, so test with the guard
  on. **Status:** the fix is being written as kernel local/0048 for the next kernel build; untested
  until that kernel boots.

### A Linux region on the internal SSD

Rank 61. A real OtherOS++ region (`ps3dd`) instead of an image file; plus a watch on the USB disk.

**Why.** The user asked for 50-100 GB of the SSD for Linux. On Evilnat's CFW the documented way is
the Recovery Menu's "Restore PS3 System" (which leaves a fixed 22 GiB gap after `dev_hdd1`, whatever
the drive's size) and glevand's `create_hdd_region.sh` from the Petitboot shell. There is no
lossless shrink: everything on `dev_hdd0` and `dev_hdd1`, users and settings go, so the backups come
first (XMB Backup Utility, xai_plugin's licences, a second NOR dump, a raw read-only copy from
Linux). The checklist is 7-9 hours of wall time, about 1.5 of them hands-on for the user. The image
file (Phase B) gets the SSD's speed without the reformat. More than 22 GiB needs a custom
`emer_init` PUP, a flash write: out of scope (hardware.md). Also from PLAN 4.12: a SMART watch on the
USB disk and a look at its write load (swap writes), since it carries root, swap and every game.

- **Payoff:** medium (a cleaner layout than an image file). **Effort:** L.
- **Risk:** high for GameOS's data (the reformat), medium after it.
- **Needs:** the user's decision, the user at the console for most steps.
- **Status:** researched, not decided.
- **Source:** PLAN 4.12; [research/HDD-REGION.md](../research/HDD-REGION.md); [archive/GAMEOS-HDD.md, "Checklist"](../archive/GAMEOS-HDD.md#checklist).

## Game mode, power, and the console at the TV

### Game mode at the TV

Rank 6. Game mode the way players meet it, with the user at the TV.

**Why.** This is how players will run every game, and almost none of it has run on the PS3. Seen
so far: the menu path (`ps3-game`), the wallpaper and services getting out of the way, and a slow
return after [OpenArena](../../gamedb/games/openarena/) (about 30 s, which led to the desktop prefetch). Installed (ps3-desktop
0.474), never tried at the TV: the escape keys (Ctrl+Alt+Esc; the PS button held 3 s; Alt+Tab and
Win+D thawing the desktop), the automatic start (`ps3-game-detect`: a terminal game, F11), [Luanti](../../gamedb/games/luanti/)
from the menu with sound, the desktop prefetch's return time, `bench/harness/gamemode-check.sh`'s
numbers, and the `ps3-game-kick` unit (it skips itself on a kernel with `ps3_idle_prep` on).

- **Payoff:** high: the players' path. **Effort:** S.
- **Risk:** low (a frozen desktop is what the escape keys are for).
- **Needs:** the user. **Status:** scheduled: TESTS.md "Next" (game mode at the TV, with the user).
- **Source:** PLAN 1.10; [archive/DESKTOP-PLAN.md, "Game mode"](../archive/DESKTOP-PLAN.md#game-mode-2026-10-17).

### Power paths of local/0041

Rank 15. The shutdown poll fix, exercised.

**Why.** local/0041 makes the final shutdown poll wait for the System Manager's shutdown command;
without it a power-button press or thermal event during shutdown leaves the console at "System
Halted" until a hard power-off. It is booted (in .433 and .472), but the paths it exists for were
never exercised, and upstream draft 3 waits on it.

- **Payoff:** safety of shutdown. **Effort:** S. **Risk:** low (worst case a hard power-off).
- **Needs:** the user. **Status:** scheduled: TESTS.md "Next" (power paths, with the user).
- **Source:** PLAN 1.8; [archive/THERMAL.md, "PS3 test steps"](../archive/THERMAL.md#ps3-test-steps) 5.

**To start.** Each from the booted kernel, watching the console and netconsole: `sudo poweroff`
(off a few seconds after "System Halted, OK to turn off power", no "no shutdown command after 5 s");
`sudo reboot` (back to Petitboot/GameOS); `sudo poweroff` and tap the power button about once a
second until it is off (never hold it); a short press of the power button from the desktop.

### Retire the stopgap kicker

Rank 27. `ps3-kick` out of game mode once no supported kernel lacks the idle fix.

**Why.** The 10 ms kicker wakes PPE thread 1 a hundred times a second; the kernel fix (local/0009)
makes it dead weight (clean on/off in batch 16: gameplay stalls 0.4 s with the fix against 8-9 s
without). The bench runs without it since batch 16, and game mode's unit skips itself when
`ps3_idle_prep` is on. What is left is removing it for good (ps3-desktop's `ps3-game-mode` step 7,
`PS3_GAME_KICK`; `ps3-kick` in ps3-system), which waits on when `.178` goes (decision D13).

- **Payoff:** cleanup. **Effort:** S. **Risk:** none on fixed kernels.
- **Needs:** host. **Status:** waiting on decision D13.
- **Source:** PLAN 1.2; [archive/STALLS.md, "The fix"](../archive/STALLS.md#the-fix).

### DualShock 3 in play

Rank 53. SDL mappings per game, rumble, LEDs, and a couch session with only the controller (menu,
game mode, the escape by the PS button). Pairing works over one USB plug; whether the console is
usable from the sofa is untested.

- **Effort:** M. **Needs:** the user. **Status:** never tried.
- **Source:** PLAN 4.7.

### Internal Wi-Fi

Rank 54. NetworkManager owns eth0 now (ps3-system), and the applet works for the wired port. The
internal Wi-Fi doesn't show up at all: no wlan0, NetworkManager says the Wi-Fi hardware is missing.
Under OtherOS++ the hypervisor keeps the Wi-Fi chip (Jupiter/Eurus, USB 054c:036f, which is also the
Bluetooth) for GameOS: it answers NO_ENTRY for the wireless VLAN (tools/lv1net.py), so gelic's
wireless never starts. glevand's route around it is ps3/0160 (ps3jupiter, a direct driver for the
chip) with 0170, which clears this LPAR's bit in the hypervisor repository at every boot; both were
dropped on 2026-10-18 ([OTHEROS-PATCHES.md](../OTHEROS-PATCHES.md) decision 1). Bluetooth on the same
chip works. Until then: a USB Wi-Fi dongle (the image carries no firmware packages, so most need one).

- **Effort:** M. **Needs:** the user (the call on the repository write). **Status:** backlog (the user,
  2026-10-23: reconsider later).
- **First step:** prove 0170's repository write is gone after a power cycle (read the node back with
  lv1_read_repository_node before and after), the reversibility question that dropped it.
- **Source:** PLAN 4.8; docs/release/TESTING-0.1.md 25.

### Display variety

Rank 55. Only one 1080p monitor has been used so far: 720p (decision D2, see
[memory.md, "The 720p console"](memory.md#the-720p-console)), overscan on TVs, 50 Hz modes.

- **Effort:** M. **Needs:** the user, a TV. **Status:** never tried.
- **Source:** PLAN 4.10.

### Hardware cursor and tearing

Rank 56. The desktop has a software cursor and some tearing. X uses the cursor plane but nothing
shows; the 0x10d init froze the PS3 once (probably a wrong first argument: RPCS3 shows a3=1), so
it is tested supervised and last in a session. xserver 21.1's modesetting has no TearFree.

- **Effort:** M. **Risk:** a freeze (the 0x10d init). **Needs:** the user. **Status:** never tried.
- **Source:** PLAN 4.11; [archive/DESKTOP-PLAN.md, "Open issues"](../archive/DESKTOP-PLAN.md#open-issues-2026-10-14-after-the-performance-round).

### Remote Start by Wake-on-LAN, idle auto-off

Rank 57. Does Remote Start (Remote Play settings) answer Wake-on-LAN? That would allow remote
power-on and save trips to the XMB. An idle auto-off would keep a forgotten console from running
for hours with nothing but SYSCON cooling it.

- **Effort:** S-M. **Needs:** the user. **Status:** never tried.
- **Source:** PLAN 4.9; [archive/PLAN.md, "Open questions"](../archive/PLAN.md#open-questions).

### Desktop applications

Rank 58. The browser, video in mpv (no hardware decoder; SPU decoding is parked), the desktop's
responsiveness. The `dcbz` memcpy trap is [graphics.md](graphics.md#the-dcbz-memcpy-trap).

- **Effort:** M. **Needs:** PS3. **Status:** never measured.
- **Source:** PLAN 4.14.
- **A browser with JavaScript:** [research/BROWSERS.md](../research/BROWSERS.md) (2026-10-28). The user picked WebKitGTK and then our own 2.52.6 build (PLAN 4.16): `webkit2gtk` with the big-endian fixes and the `badwolf` shell are packaged and measured on the PC; the PS3 test is next. Video in the browser after the RSX 0x105 fix; Skia on big-endian (WebKitGTK 2.54+) below.

### Skia on big-endian

Rank 80. WebKitGTK 2.52 is the last branch with a Cairo renderer; from 2.54 Skia is the only one, and
Skia stops a big-endian build in its config header ("not endian-savvy enough"). Our browser engine
(`webkit2gtk 2.52.6-1+ps3.N`, [research/BROWSERS.md](../research/BROWSERS.md)) therefore gets no
upstream security fixes. A big-endian Skia would unlock WebKitGTK 2.54 and later (and WPE), and
Ladybird, whose other blockers (Qt 6, memory) are smaller.

- **Effort:** unknown, probably L. **Needs:** host, then PS3. **Status:** not started; research first.
- **Start with:** what Firefox does (it byte-swaps around Skia's little-endian-only code,
  Mozilla bug 1504834) and the 69 patches of [zeldin/chromium_be](https://github.com/zeldin/chromium_be),
  which carry Skia for Chromium on big-endian; the size of Skia's pixel-format and SkRasterPipeline code
  that assumes BGRA/RGBA byte order; whether WebKit's Skia use (CPU raster, no GPU on GL 2.1) needs only
  a subset; whether upstream Skia takes such patches.
- **Source:** the user, 2026-10-28 (to the backlog, for later).

### GameOS auto-mount at boot

Rank 59. Mount GameOS's disks read-only at boot, or keep `ps3-gameos-mount` on demand: a convenience
for the user's music and videos on `dev_hdd0`, against boot time and one more thing touching GameOS's
disk. Asked on 2026-10-18, no answer yet.

- **Effort:** S. **Needs:** the user's decision, then host. **Status:** waiting on decision D9.
- **Source:** PLAN 4.5.

### Dev loop questions

Rank 60. Each wedge or kernel test costs a trip through the XMB.

- Does a Petitboot-only reboot clear a wedged RSX, instead of a full console reboot?
- Can the CPU still read VRAM after a graph exception? The VRAM swap's fallback assumes it.
- kexec from our Linux crashed the console once: find the handover step that breaks it (it would
  remove most XMB trips).

- **Effort:** S each. **Risk:** low (kexec crashed the console once; supervised).
- **Needs:** the user. **Status:** never tried.
- **Source:** PLAN 4.15; [archive/DRIVER-PLAN.md, "Later"](../archive/DRIVER-PLAN.md#later-pushing-the-hardware-limits-after-the-baseline-runs).

### Kernel patches surveyed, not ported

Rank 63. OTHEROS-PATCHES.md's SAFE candidates left out, and one test never made.

- spufs: the gang-creation deadlock fix (conflicts; we use no gangs), not holding `state_mutex` during
  user access (a DoS only, invasive), coredump skip, memcg accounting, scnprintf;
- error paths: `ps3_create_spu()`'s failure paths, a reference leak in `create_spu()`, `put_device()`
  on a `device_register()` failure (conflicts), a possible NULL dereference in gelic's
  `release_tx_chain` (a Smatch report, compile-tested only);
- T2's ps3fb cursor ioctls (CAREFUL, not tried; ps3rsx has its own KMS path);
- a gelic RX allocation failure under memory pressure, the case local/0020 exists for, was never
  provoked on purpose.

- **Payoff:** low. **Effort:** S. **Needs:** host, a reboot. **Status:** not ported.
- **Source:** [archive/OTHEROS-PATCHES.md, "B. Candidates"](../archive/OTHEROS-PATCHES.md#b-candidates-from-the-other-sources).

## Release work (last, the user's order of 2026-10-18)

### Release image session and the stable publish

Rank 65. Boot the fresh image, check it, then publish the stable suite.

**Why.** The stable `ps3` suite still holds the 2026-10-16 morning builds; everything since is only
in ps3-dev. On 2026-10-18 the user decided "test first, then publish": every package rebuilt as an
official version (kernel `6.12.111+ps3.472` with ps3rsx 0.443, Mesa .476, ps3-system 0.489, ...),
and a fresh image built from them (2.9 GiB, sha256 `65dae499d6bcab5effcf5233320b2f8f3124a8b7bb9fc8cbe0d403db67c8aa71`, a copy in
`/mnt/data_linux/Temp/ps3/release-20261018/`). The image session's checks: the kernel (dmesg clean,
`ps3_idle_prep` Y, temperatures, fan on SYSCON automatic, local/0044's "writes to the OS area in flash
disabled" line and no "Could not update FLASH ROM" 12 minutes after an NTP sync, `shadow_fast` N,
`rsx-suite.sh all`), networkd and ssh enabled with a DHCP renewal, both GameOS disks mounting
read-only with no NLS retry, the splash from Petitboot to the desktop, local/0041's power paths;
game mode with the user; a smoke run of every recipe; a 5-game bench against row 68 (a regression
check, not an A/B). The NEEDS-CHECK packages each need their one check (xmoto, classicube and its
data split on an upgrade, supertuxkart, teeworlds, ps3-system and ps3-desktop, ps3-boot's
`kboot.conf`, plymouth's cold boot).

- **Payoff:** the games reachable from stable. **Effort:** M.
- **Risk:** the publish is forever (official versions are kept): back up `build/aptly` first, an
  explicit `DEBS=` list, no `~dev` version, the user's OK.
- **Needs:** the user (the session and the OK), then host.
- **Status:** superseded: the LAN `ps3` suite this publish was for was retired on 2026-10-26;
  stable is walnut.nanobyte.dev's, published by CI with the owner's approval (docs/PACKAGING.md).
  The image session itself is still open (decisions D1, D14).
- **Source:** PLAN 5.1, 5.4; [archive/RELEASE-CANDIDATES.md, "Release 2026-10-18"](../archive/RELEASE-CANDIDATES.md#release-2026-10-18-test-first-then-publish).

### Upstream submissions

Rank 66. Five drafts in [docs/upstream/](../upstream/README.md), none sent: [Teeworlds](../../gamedb/games/teeworlds/) (FIONBIO), Yamagi
(the KISS table size), the Mesa translate LUT, and the two kernel patches (the idle fix, which has
its with-fix numbers and can drop RFC once rebased, and the sys-manager shutdown poll, which waits
on the power-path test). Mesa nv30 patches are tested on the Core 2 + 7600 GT box (stock x86
nouveau, piglit/deqp) before they are sent; that box's setup is the user's (CLAUDE.md).

- **Effort:** S each. **Needs:** the user sends. **Status:** waiting on decision D10.
- **Source:** PLAN 5.8.

### The GameOS installer

Rank 67. A GameOS app (PKG, PSL1GHT) so users never need a PC shell: install, install a new
instance, check and repair; the root image from a USB stick in zstd chunks (FAT32's 4 GiB limit),
written straight into a USB disk or a stamped file on the internal disk; boot files on
`/dev_hdd1/linux/<instance>/` with the autoexec mirror; first boot grows ext4 and makes a swap file;
"reboot into Linux" as xai_plugin's OtherOS boot does. Also `make iso` and `make disk-image` (a raw
`.img.xz`, `systemd-repart` growing it at first boot), and the first-boot assumptions `customize.sh`
makes (user, hostname, keys). Open: whether a GameOS app can write a USB disk raw (lv2 storage
syscalls), and PSL1GHT's own PPU GCC, an exception to "one GCC everywhere" the user decides.

- **Effort:** L. **Needs:** host, PS3. **Status:** design only.
- **Source:** PLAN 5.3; [archive/GAMEOS-HDD.md, "The GameOS installer"](../archive/GAMEOS-HDD.md#the-gameos-installer-design-the-user-2026-10-18-nothing-built).

### A repository for others

Rank 68. A public host, signing and a published key, mirrors.

- **Effort:** M. **Needs:** host, the user. **Status:** done except mirrors: walnut.nanobyte.dev
  (2026-10-21), signed, built by CI; licensed content in its password-protected `licensed` suite
  (docs/PACKAGING.md); the LAN repository on tibi-server was retired on 2026-10-26. **Source:** PLAN 5.2.

### End-user docs

Rank 69. Booting through Petitboot (the fan and boot-flag steps), game mode and its keys (the escape
keys kill the game, so unsaved progress is lost: PLAN 4.13), which games work, how to add licensed
data, safety notes for OtherOS++. Nobody but the two of us could set this up from the repo today.

- **Effort:** M. **Needs:** host. **Status:** not written. **Source:** PLAN 5.5, 4.13.

### Security update policy

Rank 70. The sid snapshot plus Debian's live ports, when our kernel and packages rebase, SSH
exposure on a default install. A frozen snapshot ages; users need to know what gets fixed and when.

- **Effort:** S, then ongoing. **Needs:** host. **Status:** not written. **Source:** PLAN 5.6.

### Other PS3 models

Rank 71. Everything is tested on one CECH-20xx: fats and other slims (SYSCON, flash type, thermal
zones, OtherOS++ support), and the QCFW/BadWDSD news for 25xx-40xx.

- **Effort:** M (research). **Needs:** host. **Status:** not started.
- **Source:** PLAN 5.7; [research/FLASH-SYSCON.md](../research/FLASH-SYSCON.md).

### Host tests run automatically

Rank 72. `make test` and `make test-slow` run every host suite with a summary table
(`tools/run-tests.sh`, since c00446a), but only when someone remembers to: a CI job or a hook.

- **Effort:** S-M. **Needs:** host. **Status:** not started. **Source:** PLAN 5.9.

Older versions and the dated history: [archive/PLAN.md](../archive/PLAN.md),
[archive/DESKTOP-PLAN.md](../archive/DESKTOP-PLAN.md), [archive/GAMEOS-HDD.md](../archive/GAMEOS-HDD.md),
[archive/RELEASE-CANDIDATES.md](../archive/RELEASE-CANDIDATES.md),
[archive/OTHEROS-PATCHES.md](../archive/OTHEROS-PATCHES.md), [archive/IDEAS.md](../archive/IDEAS.md).
