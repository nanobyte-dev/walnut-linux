# The console and how we work with it

This page describes the one PS3 we use: its model, firmware, disk and the tools around it. It also covers how a test session runs, what goes wrong in practice, which backups we hold and how we would recover. Read it before a session that touches the PS3. The hard safety rules are in [CLAUDE.md](../CLAUDE.md) and stay the authority; this page summarizes them and never loosens them.

Related pages: [THERMAL.md](THERMAL.md) for temperatures and the fan, [GAMEOS-HDD.md](GAMEOS-HDD.md) for the internal disk, [TESTS.md](TESTS.md) for what is scheduled, [research/FLASH-SYSCON.md](research/FLASH-SYSCON.md) for what writing flash or SYSCON would unlock. Terms are in the [glossary](README.md#glossary).

## The console

| Item | Value |
|---|---|
| Model | CECH-20xx (DYN-001, the first Slim), a NOR console; PyPS3checker reports "Matching SKU: CECH-20xxx (DYN-001)" |
| Firmware | 4.92 CEX Cobra 8.5D (Evilnat). ros0 is 4.92 Evilnat, ros1 is 4.75 CEX (FERROX-based), the bank left over from an earlier install |
| webMAN | MOD 1.47.48, at http://10.0.0.76/ |
| Flash | one 16 MB NOR (Samsung K8Q2815UQB, Macronix MX29GL128E or Spansion S29GL128P) |
| SYSCON | SW2-301 (Sherwood family, NEC/Renesas 78K0R core, D79F0086, LQFP-128), firmware 1.11.0, patch 0832 |
| OtherOS++ | xai_plugin "Install OtherOS": VFLASH regions resized, Petitboot (NOR image) in VFLASH region 5 |
| Internal disk | an upgraded SSD, not the stock 2.5" HDD ([GAMEOS-HDD.md](GAMEOS-HDD.md) has the details) |
| Screen | a Logitech BRIO webcam points at the monitor. HDMI capture is blocked by HDCP in the XMB |
| USB disk | AXAGON adapter, JMicron JMS567 (152d:0567): Bulk-Only Transport only, at USB 2 speed, queue depth 1 |

The SSD matters for two reasons. Results measured with the root on it (`root=/dev/mapper/ps3root` in `bench.txt`) are better on disk-bound phases than a stock console would see ([TESTS.md](TESTS.md)). And on a NOR console part of the firmware lives on the internal disk (below), so a disk swap is not just a storage change.

This is not a Mullion (CXR713/714) console. Mullion is the fat CECHA to K line, with an external SPI EEPROM you can read with a Bus Pirate. Most fan-table and EEPROM how-tos on the forums were written for Mullion and do not carry over to a SW2.

### Where things live on a NOR Slim

This part changes the risk picture. The sources are psdevwiki, consolemods and xai_plugin's source; details are in [research/FLASH-SYSCON.md](research/FLASH-SYSCON.md).

- The 16 MB NOR holds the flash header and region table, `asecure_loader` (metldr), eEID, cISD, cCSD, the revoke lists, **ros0 and ros1** (CoreOS: lv0, lv1, lv2 and friends, 7 MB each), cvtrm, CELL_EXTNOR_AREA, CRL/DRL and, in the last 256 KB, **bootldr**. Several of these are per-console.
- `dev_flash`, `dev_flash2` (xRegistry), `dev_flash3` and the OtherOS area are not in the NOR. On NOR consoles they live in VFLASH, a region of the internal disk. That is why a NOR console asks for a firmware reinstall from USB after a disk swap and a NAND console does not.
- xai_plugin's "Install OtherOS" on NOR rewrites the VFLASH partition table and writes the OS area header, the boot flag and the Petitboot image into VFLASH region 5. "Set OtherOS/GameOS Boot Flag" rewrites the same OS area header.
- So, as far as we can tell, nothing in this project has written the NOR chip. The OtherOS++ install, the region resize and every session's boot flag went to the SSD.
- The SYSCON EEPROM holds what the NOR does not: which ROS bank is active, the per-bank revoke flags, the SPU count, product and recovery flags, the error log, the thermal (fan) config and the SYSCON firmware patch. On a Sherwood the EEPROM is virtualized inside the SoC's own flash (0x8000 bytes on SW2), with no external chip.

### The kernel's OS area write

On 2026-10-15 the kernel logged `os_area_queue_work_handler: Could not update FLASH ROM`. We traced it in the 6.12 sources (`arch/powerpc/platforms/ps3/os-area.c`, `drivers/rtc/rtc-ps3.c`, `drivers/char/ps3flash.c`) and the built `.457` config. Nothing was run on the PS3 for this analysis.

- **What the PS3 RTC is.** lv1's clock is read-only to a guest. The kernel's wall clock is the lv1 RTC plus `rtc_diff`, an offset Linux keeps. Setting the RTC in Linux (`rtc-ps3`'s `set_time`) only computes a new `rtc_diff`; it never reaches lv1 or SYSCON, and GameOS's own clock cannot be changed this way.
- **The trigger.** A new `rtc_diff` goes to `ps3_os_area_set_rtc_diff()`, which queues `os_area_queue_work_handler`. The handler updates the device-tree property `linux,rtc_diff` and calls `update_flash_db()`, which reads the first 4 KiB of the OS area through the flash ops, sets the Linux-owned database entry (the RTC offset), reformats the database first if it does not verify, and writes the 1 KiB database back. On our image the kernel itself sets the RTC: the config has `CONFIG_RTC_SYSTOHC=y`, and `systemd-timesyncd` clears `STA_UNSYNC` once it has NTP time, which turns on the kernel's 11-minute mode. The first write comes within seconds to minutes of the first sync, then every 11 minutes, each time the offset has moved by a second.
- **Where it would go.** The flash ops come from `ps3flash`, the flash device lv1 gives the OtherOS LPAR. On a NOR console that device is VFLASH on the SSD, and the region is the OS area: header, params with the boot flag, the database, then the Petitboot image. `ps3flash` writes through a 256 KiB bounce buffer as read-modify-write, so a 1 KiB database update rewrites the whole first 256 KiB block (header, boot flag and the start of Petitboot). A crash or power cut during it would leave Petitboot not starting and the boot flag stuck. "Install OtherOS" puts that right. It never touches the NOR.
- **Why it fails today.** `CONFIG_PS3_FLASH`, `PS3_FLASH_NG` and `PS3_NOR_FLASH` are off in `packages/patched/kernel/linux/config/ps3.config`, so nothing registers flash ops, `os_area_flash_read()` returns -ENODEV, `update_flash_db()` gives up before writing, and the handler prints the warning. Nothing was ever written.
- **The fix, `local/0044`** (`ps3: os-area: never write the OS area unless ps3_os_area.write=1`). The bool `ps3_os_area.write` defaults to 0 (`/sys/module/ps3_os_area/parameters/write`). With it off, `os_area_flash_write()` returns -EPERM before it takes the flash ops, and the work item still updates `linux,rtc_diff` in the device tree (memory only, what a kexec'd kernel reads) but returns before touching flash, without a warning. Reading is untouched. A late initcall logs once at boot: `ps3_os_area: writes to the OS area in flash disabled, RTC changes last until reboot`. `RTC_SYSTOHC` stays on: with the patch its writes only move the in-memory offset, so `/dev/rtc0` keeps agreeing with the system clock during the boot. The patch is defence in depth, safe if a later config or module turns `ps3flash` back on.
- **Where our kernel's `rtc_diff` comes from.** Petitboot's own Linux reads the hypervisor's copy of the OS area and clears its header, so in our kexec'd kernel `ps3_os_area_init()` takes `linux,rtc_diff` and `linux,av_multi_out` from the device tree Petitboot passes on. Petitboot prefers the database value, then the params segment's value, then 946684800 (1970 to 2000). Without a write, every boot starts from the same value.
- **Consequences.** Clock corrections made under Linux last until the next reboot, as they always did, since the write always failed. At boot the clock is lv1's RTC plus the stored offset, and timesyncd steps it to NTP time once the network is up. timesyncd also keeps `/var/lib/systemd/timesync/clock`, so the clock never starts before the last shutdown. Without a network the clock could be off by however far the stored offset is from the truth. GameOS is not affected: it keeps its own offset.
- **State.** The patch is in the series and in kernel `.472`, and the passive check passed on 2026-10-20 (the boot of 2026-10-19 with the SSD root, about 13 h up): the kernel log has `ps3_os_area: writes to the OS area in flash disabled, RTC changes last until reboot` and no `Could not update FLASH ROM`, timesyncd is synchronized (`NTPSynchronized=yes`), and `/sys/module/ps3_os_area/parameters/write` is `N`. Nothing tried to write the OS area.

## A session

**Getting to Linux needs the user.** We cannot do it alone, and we never reboot, kexec or flip the flag ourselves. The steps:

1. The fan is in SYSCON automatic mode in webMAN (or a fixed speed of at least 60%).
2. The user sets the OtherOS flag in the XMB.
3. Petitboot boots the default kernel, or the user picks another.
4. Every reboot from Linux lands in GameOS, so a reboot ends the session unless the user sets the flag again.
5. At the end of each session the flag goes back to GameOS.

The rules behind this, summarized from [CLAUDE.md](../CLAUDE.md): no permanent alterations beyond installing Linux (OtherOS++, Petitboot, the HDD region); never leave the console in a state it cannot recover from, and back up the NAND/NOR flash and verify the dump before anything touches flash; GameOS stays the default boot OS (the flag may be OtherOS temporarily and goes back every session); no SYSCON writes except the fan-policy service in [THERMAL.md](THERMAL.md), no eFuse or flash patches beyond the documented install; hardware mods only if asked; keep Linux sessions supervised and stop on any "PS3 Thermal Alert"; when unsure whether a step is reversible, stop and ask. Fan policy from Linux is allowed only as CLAUDE.md describes it.

**Deploying.** `make <pkg>-deb`, then `make deploy PKGS="..."`. Kernels go with `make deploy KERNELS=1 PKGS="linux-image-ps3 ps3rsx-modules-ps3"` and install next to the running one. `DRY_RUN=1` shows what would change. ps3rsx can be reloaded only with X stopped (`systemctl stop getty@tty1`, then start it again) and, with VRAM swap on, after `swapoff /dev/ps3vswap0`.

**Kernel changes need a reboot**, so batch them. kexec from our Linux crashed the console.

**A graph exception wedges the GPU** until a console reboot. Risky GPU tests go last.

**Testing block or memory code.** Raise the console log level (`echo 7 > /proc/sys/kernel/printk`) so a hang prints full stacks over netconsole. Put every SSH command under `timeout`. Verify data with a pattern (write, drop caches, read back) before using a device as swap, then under a memory hog, then in a game.

**Observing.**

- netconsole: `make listen`.
- The BRIO webcam: `tools/webcam-grab.sh`, only when the user is not using it. `tools/bench-cam.sh` takes photos 30 s and 60 s into every bench run; `tools/webcam-audio.sh` records from the camera's microphone (off until the user says the room is free).
- fps: `GALLIUM_HUD=fps GALLIUM_HUD_VISIBLE=false GALLIUM_HUD_DUMP_DIR=/tmp/hud`.
- Blocked threads: sample `/proc/<pid>/task/*/stack`.

**Gotchas.**

- `perf record -a -g` once hung the PS3. Short supervised runs (`-- sleep 5`) work.
- Keep netconsole quiet in normal use (loglevel 4), and never use `trace=1` through a GL program.
- Programs that wait for a key need stdin held open over SSH (`sleep N |`).
- `pkill -f` inside SSH commands kills the SSH shell itself. Use `pgrep -x`.
- The PS3's clock is UTC; the PC's is EEST.
- Restarting the session uncleanly leaves stale X sockets: X comes up on `:1` or `:2` until reboot.

## Backups and recovery

### What we have

| Backup | State (computed 2026-10-18) |
|---|---|
| `backups/FLASH-NOR-FW4.92-20261013-232254.bin` | 16,777,216 bytes, SHA-256 kept with the dump (not published). A software dump by xai_plugin "Dump Flash" on 2026-10-13, before the OtherOS install |
| `backups/FLASH-NOR-FW4.92-20261013-232254.bin.checklog.txt` | PyPS3checker: 155 checks, 0 dangers, 0 warnings; SKU match CECH-20xx (DYN-001); ros0 4.92 Evilnat Cobra CEX, ros1 4.75 CEX patched, both hashes recognised; per-console nonce and EXTNOR hashes match |
| `backups/cfw_settings-20261013-232254.log` | xai_plugin log. It contains console IDs |

`backups/` is private: never commit or upload it, never paste the settings log anywhere, never put the dump in git or LFS. It holds per-console keys and IDs.

Gaps:

- **One copy, on this PC's nearly full /home.** The data-safety rule asks for more. Plan: two more copies, encrypted (`age` or `gpg -c`), on two different media (a USB stick kept away from the PC, and the backup server only if it is private). Check each with `sha256sum` against the hash above.
- **One dump, never cross-checked.** A software dump read twice gives the same bytes, so a second one only proves the read path is stable. It is still worth doing: take a second xai dump, `cmp` it with the first (identical while no firmware has been installed since) and run PyPS3checker on it. If a hardware flasher is ever wired up, its first job is a read compared byte for byte with this file.
- **Not in the NOR dump, so not backed up anywhere:** the SYSCON EEPROM (bank flag, SPU count, tokens, thermal config, error log, patch); VFLASH on the SSD (dev_flash, dev_flash2 with the xRegistry settings, dev_flash3, region 5 with Petitboot and the boot flag); GameOS's disk regions (games, saves, trophies, licences), which need the XMB's backup utility or a copy of `/dev_hdd0/home` and `exdata`; and the Blu-ray drive's pairing data, which sits in eEID (in the NOR) and in the drive.
- **Which bank is active.** The dump has two CoreOS banks and the SYSCON says which one boots. After a firmware (PUP) install the bank flips. Restoring the 2026-10-13 dump after that would boot an old CoreOS against the new `dev_flash` on the SSD, and the console would probably ask for a firmware reinstall. So record the active bank with each dump (xai_plugin "Show ROS Bank", read-only), and take a fresh dump after every PUP install. The active bank right now is not recorded.

### Checking a dump

PyPS3checker (littlebalup/PyPS3tools) reads regular and byte-reversed NOR dumps. It checks structure, per-console consistency, region repeats and known ROS and trvk hashes. Exit 0 is clean, 2 warnings, 3 danger. Our 0/0 is the best result it gives. psdevwiki's signs of corruption: misspelled region names (such as "eIIH" for eEID), a bootldr that is all FF, a bootldr not starting with 00 00. For hardware reads, read two or three times and compare: any difference is a wiring problem.

### Hardware recovery on DYN-001

We have no flasher yet. What a NOR recovery would take:

- DYN-001 uses psdevwiki's NOR test-point "layout 3": pads between the south bridge and the NOR on the underside of the board, plus ground and 3.3 V. Supported flashers are a Teensy++ 2.0 with NORway, Progskeet 1.x, PNM, E3 / E3 Linker (clip) and a TSOP56 clip. NORway's changelog lists DYN-001's three NOR parts.
- NORway on a Teensy++ 2.0 runs at 3.3 V (cut the 5 V trace, bridge the 3 V pads), connects straight to the PC without a hub, and drives TRISTATE (PE7) to keep the console's own chipset off the bus. The PS3 stays plugged into its PSU in standby. Sequence: start NORway, power the console on (it must not boot), `dump`, `release`, power off. A 16 MB dump takes about a minute, a full write about 4.5 minutes. The quade.co Slim guide wired about 50 thin wires and took a little over an hour. An "unknown" manufacturer or chip in NORway's output means bad wiring.
- consolemods says a hardware flasher is "NOT recommended unless the console is bricked". Some DYN-001 boards need a resistor near the clip edge shaved for the E3 clip.
- The PS3 boots only with its own per-console data. eEID, bootldr and metldr are encrypted with keys inside that Cell, so a dump from another console does not help. Our dump is the only way back for a damaged per-console region.
- Teensy++ 2.0 boards are hard to buy new. Budget for a clone, or a Progskeet or E3 clip from the used market. Buy it, wire it and compare a read with our dump before any write is ever needed. Buying it counts as a hardware mod, so only when asked ([backlog/hardware.md](backlog/hardware.md)).

Softer recovery levels, mildest first: drop a boot parameter or power-cycle; the XMB, the Recovery Menu (hold power from off until the double beep: Restore Default Settings, Restore File System, Rebuild Database, Restore PS3 System, PUP install from USB) or xai_plugin; a hardware flasher on the NOR; the SYSCON UART for EEPROM bytes. Some failures have no path with public tools, which is why the rules forbid the writes that cause them.

### The SYSCON EEPROM backup that is missing

- From GameOS, read-only, no hardware: xai_plugin "Dump SYSCON Error Log"; "Dump EEPROM" (6 x 256 bytes through the Update Manager window at 0x2F00, 0x3000, 0x48000, 0x48800, 0x48C00 and 0x48D00; it pokes six lv1 words to lift the access checks and restores them afterwards, a RAM-only change). That covers the flags (bank, SPU count, product and recovery modes), not the thermal config. "Show ROS Bank" and "Check 8th SPE" are single-byte reads.
- From Linux, read-only: graf_chokolo's `ps3dm_scm /dev/ps3dmproxy` after `dmpatch.sh`. It needs the dispatcher-manager patch and ps3dm tools we do not build, and xai_plugin does the same from the XMB.
- The full EEPROM including the thermal config: only over the SYSCON UART (three wires soldered to the DYN-001 pads, a 3.3 V USB serial adapter, authentication, then `EEP GET` over the whole 0x8000 bytes). That is a hardware mod and needs the user's explicit go. It is not done. See [backlog/hardware.md](backlog/hardware.md).

### Pre-flight checklist for any persistent write

No persistent write is planned. If the user ever asks for one, all of this comes first:

1. The user has asked for this specific write, and has changed CLAUDE.md's safety rules to allow it. Agents do not decide this.
2. A fresh NOR dump taken after the last PUP install; PyPS3checker with 0 dangers; a second dump `cmp` equal; the active ROS bank recorded; SHA-256 recorded; three encrypted copies, one off the PC.
3. For SYSCON writes: a full SYSCON EEPROM dump over UART, read twice and compared; the factory thermal config from psdevwiki (CRC32 8752BF08) checked against our own block.
4. The recovery tool for that class tested by reading first: the flasher reads the NOR and matches the software dump; the UART authenticates and reads back known bytes.
5. Power: a UPS or at least a stable outlet, nobody touching the switch; for PUPs, the PUP's MD5 checked; nothing else running.
6. Cooling: webMAN fan on SYSCON, or fixed at 60% or more, and a supervised session.
7. The way back written down step by step before starting, and the write done once, by hand, with the user present.

Older versions and the dated history: [archive/PLAN.md](archive/PLAN.md) ("Test loop", "The console") and [archive/FLASH-SYSCON.md](archive/FLASH-SYSCON.md) (sections 1 and 4).
