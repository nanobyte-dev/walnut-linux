# Flash, SYSCON and hypervisor writes: what's safe, what it buys, how to recover

Research notes (2026-10-18). Nothing in this file was tried on the console. The question:
writing to flash, the SYSCON or the hypervisor would unlock more for us, so what is safe, how
would we do it, and how do we get back if it goes wrong. CLAUDE.md's PS3 safety rules still
apply: no syscon writes, no flash patches beyond the OtherOS++ install, hardware mods only on
request. This file is input for a decision by the owner, and it changes none of those rules.

How to read it: **[fact]** lines carry a source (section 8). **[inference]** lines are my
reasoning from those facts and need checking before anyone acts on them. **[unverified]** marks
claims I found only in search snippets or forum posts. psdevwiki and psx-place block scripted
fetches; the psdevwiki pages were read from Wayback Machine snapshots (2025), xai_plugin and
PS3MFW from their GitHub source.

## 1. This console

| Item | Value | Source |
|---|---|---|
| Model / board | CECH-20xx (first Slim), DYN-001 | PLAN.md, PyPS3checker "Matching SKU: CECH-20xxx (DYN-001)" |
| Flash | 1 × 16 MB NOR (Samsung K8Q2815UQB, Macronix MX29GL128E or Spansion S29GL128P) | psdevwiki DYN-00x |
| SYSCON | **SW2-301** (Sherwood family, NEC/Renesas 78K0R core, D79F0086, LQFP-128), firmware 1.11.0, patch 0832 | psdevwiki SW2-301, Syscon Hardware (`VER` → `S1E 01 0B 00 0832` on DYN-001) |
| SYSCON EEPROM | No external chip: Sherwood keeps its EEPROM virtualized inside the SoC's flash (0x8000 bytes on SW2), with wear levelling, so offsets move | psdevwiki Syscon Hardware, Sherwood, Platform ID |
| Firmware | 4.92 Evilnat CEX Cobra 8.5; ros0 = 4.92 Evilnat, ros1 = 4.75 CEX (FERROX-based), the bank left from an earlier install | checklog |
| OtherOS++ | xai_plugin "Install OtherOS": VFLASH regions resized, Petitboot (NOR image) in VFLASH region 5 | xai_plugin otheros.cpp, cfw_settings log |
| Internal disk | A replacement SSD, which matters: on NOR consoles part of the firmware lives on the disk (below) | checklog, consolemods "Upgrading your Storage" |

It is not a Mullion (CXR713/714) console. Mullion is the fat CECHA–K line, with an external SPI
EEPROM that can be read with a Bus Pirate. Most fan-table and EEPROM how-tos on the forums were
written for Mullion, and they don't carry over to a SW2. **[fact]** psdevwiki Syscon Hardware:
"an EEPROM programming interface (only on Mullion)"; consolemods: Mullion in A–K, Sherwood from L.

### Where things live on a NOR Slim

This is the part that surprised me, and it changes the risk picture:

- **[fact]** The 16 MB NOR holds: flash header and region table, `asecure_loader` (metldr), eEID,
  cISD, cCSD, the revoke lists `trvk_prg0/1` and `trvk_pkg0/1`, **ros0 and ros1** (CoreOS: lv0,
  lv1, lv2 kernel and friends, 7 MB each, at 0x0C0000 and 0x7C0000), cvtrm, CELL_EXTNOR_AREA,
  CRL/DRL and, in the last 256 KB at 0xFC0000, **bootldr**. psdevwiki Flash marks asecure_loader,
  eEID, cISD, cCSD, cvtrm and bootldr as per-console.
- **[fact]** `dev_flash`, `dev_flash2` (xRegistry), `dev_flash3` and the OtherOS area are not in the
  NOR. On NOR consoles they live in VFLASH, a region of the internal disk. That's why a NOR console
  asks for a firmware reinstall from USB after a disk swap, and a NAND one doesn't
  (consolemods, psx-place).
- **[fact]** xai_plugin's "Install OtherOS" on NOR rewrites the **VFLASH** partition table
  (`setup_vflash()`: regions 6 and 7) and writes the OS area header, the boot flag and the Petitboot
  image into **VFLASH region 5** (`install_petitboot()`). On NAND it patches the NAND's own table
  instead. "Set OtherOS/GameOS Boot Flag" (`set_flag()`) rewrites the same OS area header.
  Source: xai_plugin `otheros.cpp`.
- **[inference]** So, so far, nothing in this project has written the NOR chip. The OtherOS++ install,
  the resize and every session's boot flag went to the SSD. The kernel's failed write on
  2026-10-15 ("os_area_queue_work_handler: Could not update FLASH ROM", PLAN.md open questions)
  targets that same OS area (the RTC offset is kept there). Worst case, that write breaks the boot
  flag or Petitboot, which "Install OtherOS" puts back. It does not touch the NOR.
- **[fact]** The SYSCON EEPROM holds what the NOR doesn't: **which ROS bank is active**
  (`os_bank_indicator`, 0x48C24 in the Update Manager view: ros0 if 0xFF, else ros1, NOR only),
  the per-bank revoke flags, **the SPU count** (`restrict_spu`, 0x48C30: 0x06 default, 0x07
  enables the 8th SPE, 0xFF unlimited), product, recovery and QA flags, tokens tied to EID0, the
  error log, the thermal (fan) config and the syscon firmware patch. Sources: psdevwiki SC EEPROM,
  Syscon Thermal Configs; xai_plugin `functions.h`, `cfw_settings.h`.

## 2. Inventory: what's writable and what writing it would unlock

"Unlock" means for our goal: an open Linux on the PS3 that runs games well. The two limits that
matter today are RAM (240 MB, PPE-bound frames: HARDWARE-LEVERAGE.md) and heat (no fan control
under Linux: CLAUDE.md).

| # | Piece | How it's written | What it would unlock for us | Persistence |
|---|---|---|---|---|
| A | **7th SPE** (the "shared" SPE) | **No write.** Kernel parameter `spu_base.enum_shared=1`; our kernel already carries the patch (`linux/patches/ps3/0100-spu-enum-shared-param.patch`) | One more SPE for speq/libspeq: 6 → 7, ~17% more SPU throughput | Per boot |
| B | lv1 patches in RAM (runtime) | lv1 poke (hypercall 183) from GameOS (xai_plugin "Rebug Tools", webMAN) or from Linux if the running lv1 has peek/poke | Anything lv1 gates: 4 KB GPU IO pages (`gpu-4kb-iopage`), RSX clocks (xai "Overclock Tools" pokes lv1), SS service access | Gone at power-off / cold boot |
| C | lv1 patches in the flashed CoreOS (`lv1.self` in ros0/ros1) | A CFW PUP built with PS3MFW `patch_lv1.tcl`-style patches, installed from the XMB or Recovery | Same list as B, active from the first instruction of every boot, including the OtherOS boot | Until the next PUP |
| D | ROS bank (any CFW/OFW update) | PUP install from XMB or Recovery Menu; it writes the inactive bank and flips the bank flag in SYSCON | A newer Evilnat (4.93: "LV1 patch for custom fan speed on PS2 EMU"). Nothing for Linux I could find | Until the next PUP |
| E | VFLASH region table and region 5 (OS area, Petitboot, boot flag) | xai_plugin "Install OtherOS" / "Set boot flag"; Linux via `ps3flash` (we build it out) | Already done. A newer Petitboot; a bigger region 5 | On the SSD |
| F | HDD region table (OtherOS++ HDD region size) | `create_hdd_region.sh` from Petitboot, ps3stor tools, or PUPs with a preset split | More disk for Linux (we run from USB/NFS today, so little) | On the SSD; GameOS reformats if its region is damaged |
| G | SYSCON EEPROM via the hypervisor's Update Manager window (from GameOS) | `update_mgr_write_eprom()` (xai_plugin: 8th SPE, recovery-mode flag, product mode, QA) | **8th SPE** (0x48C30: 0x06 → 0x07), recovery-mode flag | Persistent, in the SYSCON |
| H | SYSCON thermal config (fan tables, shutdown thresholds) | SYSCON UART in SW (auth) mode: `EEP SET` / tools such as jw0710's GUI, Kohada | A fan curve that suits Linux, with no webMAN loop needed | Persistent, in the SYSCON |
| I | SYSCON firmware patch area | Sony's syscon updates; UART `firmud` | Nothing for us | Persistent |
| J | NOR per-console regions (bootldr, eEID, metldr, cISD/cCSD, cvtrm) | Hardware flasher, or software NOR writers (PS3Xploit flash writer, CFW installers) | Nothing for us | Persistent |
| K | eFuses / Cell keys | Not writable by any known tool | Nothing | Permanent |

What each one gives us, in plain terms:

- **A, 7th SPE.** **[fact]** psdevwiki OtherOS++: "Add `spu_base.enumerate_shared=1` to your kboot
  entry … Enabling 7th SPU on Linux will make most of SS services unusable because they use the 7th
  SPU". Our patch spells the parameter `enum_shared` (`module_param(enum_shared, …)` in
  `spu_base.c`), and SPU-OFFLOAD-PLAN.md runs with `spu_base.enum_shared=0`, so the switch is already
  in our kernel and just off. **[inference]** The SS services are GameOS-facing hypervisor services
  (Dispatcher Manager clients, isolated SPU modules). GameOS's disk regions are decrypted by
  ENCDEC, a hardware block, so `ps3-gameos-mount` should keep working, but that needs a test.
  Nothing persistent changes: a boot without the parameter is the off-switch.
- **B/C, lv1 patches.** **[fact]** The PS3MFW patch list (patch_lv1.tcl) and xai_plugin's "Rebug
  Tools" offer the same set: mmap of any memory, HTAB write, MFC_SR1 mask, DABR, ENCDEC 0x85,
  **4 KB GPU IO pages** ("Allow 4kb IO page size for GPU GART memory"; the patch forces a
  `li r0,0x1000`), dispatcher-manager access, repository nodes in any LPAR, storage ACL skip, and
  others. **[fact]** xai_plugin applies its patches at runtime with `lv1_poke32()` and, for the
  EEPROM dump, restores the original words afterwards (`eeprom.cpp`). **[inference]** The
  interesting one for us is the 4 KB GPU IO page: today ps3rsx reserves a 64 MB GART at boot,
  iomapped as 1 MB pages (DRIVER-PLAN.md). With 4 KB IO pages the GART could map scattered
  ordinary pages on demand, which would give back part of that 64 MB. That's a guess about how
  the patch behaves; nobody I found documents it. **[fact]** No patch in either list exposes raw
  RSX registers, more XDR, or more VRAM. VRAM is already the full 256 MB, and the XDR size comes
  from lv1's repository (Linux `mm.c`: "Assume a single highmem region").
- **[inference] Runtime vs flashed, for Linux.** Every trip to Linux is a reboot with the OtherOS flag
  set (the owner reboots from the XMB; xai's flag tool uses a hard reboot). A hard reboot runs lv0
  again, which reloads lv1 from the NOR. So patches poked from GameOS are gone by the time Petitboot
  runs, and whatever OtherOS++ capabilities Linux has (it reads GameOS's disk regions:
  GAMEOS-HDD.md) must come from the lv1 in Evilnat's flashed CoreOS. Evilnat 4.92's feature list
  includes "Peek and Poke (LV1)" and "Compatibility with OtherOS" **[unverified, snippet]**. If
  peek/poke is in the flashed lv1, Linux could apply a patch like the 4 KB IO page itself, in RAM,
  with no flash write. Whether that's early enough (before lv1 sets up the GPU IO space) is unknown.
- **D, firmware updates.** Low value for Linux. The one practical effect: a PUP install flips the
  ROS bank and rewrites dev_flash on the SSD, so **after any PUP the NOR dump is out of date**
  (section 4).
- **G, 8th SPE.** **[fact]** psdevwiki SC EEPROM: SPU num at 0x48C30, "can be set to 0x07 to enable
  the 8 SPE". xai_plugin's "Toggle 8th SPE" writes it through the Update Manager
  (`update_mgr_write_eprom(SPE_8TH_EEPROM_OFFSET, …)`). **[unverified]** A Level1Techs user enabled
  it on two consoles: one worked, the other's 8th SPE was "definitely defective". **[inference]**
  The 8th SPE is off because Sony sold Cells with one bad or unvalidated SPE. On a console where
  it's bad, lv1 or GameOS could crash at boot. Then the in-XMB toggle can't be reached to undo it,
  and the way back is the SYSCON UART. Whether Linux would even get that SPE (exclusive, or a second
  shared one) is also unknown.
- **H, fan curve.** **[fact]** Sherwood thermal config: 0x200 bytes at 0x250, checksum at 0x7FE
  covering 0x000–0x7FD. The factory config for DYN-001 + SW2-301 is published on psdevwiki (CRC32
  8752BF08), so a known-good image exists to restore. **[unverified, forum]** People report that SW
  syscons ignore custom curves that don't fit the firmware's logic, and one CECHL owner got a triple
  beep and red light from a bad value until it was fixed. jw0710's GUI lists its fan-curve editor as
  not supported on SW. **[inference]** The win is real (the fan would react under Linux without
  webMAN), but a runtime fan policy from Linux, if the System Manager allows it, gets most of it with
  no persistent write. THERMAL.md (another agent's research) is the place to settle that first.
- **I–K.** Nothing for us at all, and the worst failure modes. Never.

## 3. Risk per piece, and how you'd get back

Recovery classes, from mild to final:

1. **Per boot**: drop a boot parameter or power-cycle.
2. **Software**: from the XMB, Safe/Recovery Menu (reinstall PUP, Restore File System), or xai_plugin.
3. **Hardware flasher**: the NOR rewritten from our dump by a Teensy++/NORway, Progskeet or E3.
4. **SYSCON UART**: solder three wires to the SYSCON test pads, authenticate, rewrite EEPROM bytes.
5. **Permanent**: no path with public tools.

| # | Piece | Who has done it, with what | Failure modes | Worst-case recovery |
|---|---|---|---|---|
| A | 7th SPE | OtherOS++ users, glevand's patch (we carry it) | SS-service users break; a kernel lockdep warning on some 3.x kernels **[unverified]** | 1 |
| B | lv1 runtime poke | Rebug/Evilnat toolboxes, xai_plugin | Wrong address crashes lv1 → hard hang, power-cycle | 1 |
| C | lv1 in a flashed CoreOS | Gitbrew (3.41/3.55 OtherOS++ PUPs), Rebug (3.55 OtherOS++ builds); PS3MFW patterns marked "cannot find" for newer firmware | A patched lv1 that won't boot: the console fails before the XMB | 2 (Recovery Menu, if lv2 still starts) or 3 |
| D | ROS bank (PUP) | Everyone with CFW | Interrupted install (power loss), bad PUP, PUP for the wrong model | 2 usually; 3 if both banks or the bank flag go bad |
| E | VFLASH / OS area | xai_plugin, Rebug toolbox, our own install | Petitboot won't start, boot flag stuck, dev_flash damaged | 2: reinstall Petitboot; reinstall PUP; Restore PS3 System (formats the disk) |
| F | HDD region table | dblsaiko/glevand scripts | GameOS sees a damaged region and reformats the disk, deleting the Linux region | 2, with data loss |
| G | 8th SPE / UM EEPROM flags | xai_plugin, Rebug toolbox | Bad SPE → crashes or no boot; wrong flag (product mode, QA) → odd boot modes | 2 if GameOS still boots, else 4 |
| H | Thermal config | psx-place users, jw0710 GUI (Mullion), Kohada | Bad checksum → no boot; bad curve → overheating; SW ignoring the table | 4 (UART still answering after a bad write is **[unverified]**) |
| I | SYSCON firmware | Sony; developers with patched scuartd | Failed patch write | 5 for us: zecoxao lists "re-pairing syscon" as future work, so no donor SYSCON can replace it today **[inference]** |
| J | NOR per-console regions | PS3Xploit flash writers, E3/NORway users | Wrong dump, a write cut short, bad wiring | 3, **only with our own verified dump** |
| K | eFuses / Cell keys | n/a | n/a | 5 |

Notes on the classes:

- **[fact]** The Recovery Menu (hold power from off until the double beep) offers Restore Default
  Settings, Restore File System, Rebuild Database, Restore PS3 System and a PUP install from USB.
  xai_plugin's "Toggle Recovery Mode" sets the same flag from the XMB, on NOR models only.
  Source: consolemods "Safe Mode", xai_plugin `cfw_settings.cpp` (`RECOVERY_MODE_FLAG_OFFSET`).
- **[fact]** The SYSCON UART is 3.3 V TTL at 57600 baud. Commands are `<command>:<checksum>`,
  where the checksum is the byte sum & 0xFF. Without auth, `EEP GET`, `EEP SET`, `FAN` and friends
  answer `NG F0000005`. Auth is an AUTH1/AUTH2 AES challenge-response, and the keys are public in
  db260179/ps3syscon. psdevwiki shows DYN-001's UART test pads in a photo (Syscon Hardware). Source:
  psdevwiki Syscon Hardware, github db260179/ps3syscon.
- **[fact]** The PS3 boots only with its own per-console data: eEID, bootldr and metldr are
  encrypted with keys inside that Cell. A dump from another console doesn't help, and Sony's keys
  for producing new per-console data are not public (psdevwiki Flash; zecoxao 2026, section 5).
  Our dump is the only way back for class J.

## 4. Recovery for this console

### What we have

| Backup | State (computed locally 2026-10-18) |
|---|---|
| `backups/FLASH-NOR-FW4.92-20261013-232254.bin` | 16,777,216 bytes, SHA-256 `(the NOR dump SHA-256, kept with the dump, not published)`. Software dump by xai_plugin "Dump Flash" on 2026-10-13, **before** the OtherOS install |
| `….bin.checklog.txt` | PyPS3checker: 155 checks, **0 dangers, 0 warnings**; SKU match CECH-20xx (DYN-001); ros0 4.92 Evilnat Cobra CEX, ros1 4.75 CEX patched, both hashes recognised; per-console nonce and EXTNOR hashes match |
| `cfw_settings-….log` | xai_plugin log (contains console IDs: private, never paste it anywhere) |

Gaps:

- **Only one copy, on this PC's nearly full /home.** The user's data-safety rule asks for more.
  Make two more copies, encrypted (e.g. `age` or `gpg -c`), on two different media (a USB stick
  kept away from the PC, and the backup server **only if it's private**). Never git, never LFS:
  the dump holds per-console keys and IDs. Check each copy with `sha256sum` against the hash above.
- **One dump, never cross-checked.** A software dump read twice gives the same bytes, so a second
  software dump only proves the read path is stable. Still worth doing: take a second xai dump,
  `cmp` it with the first (expect identical while no firmware has been installed since), and run
  PyPS3checker on it. If a hardware flasher is ever wired up, its first job is a read compared
  byte-for-byte with this file.
- **Not in the NOR dump, so not backed up anywhere:**
  - the SYSCON EEPROM: bank flag, SPU count, tokens, thermal config, error log, syscon patch;
  - VFLASH on the SSD: dev_flash, dev_flash2 (xRegistry, settings), dev_flash3, region 5 (Petitboot
    and the boot flag);
  - GameOS's disk regions (games, saves, trophies, licences): those need the XMB's backup utility,
    or a copy of `/dev_hdd0/home` and `exdata`;
  - the Blu-ray drive's pairing data, which sits in eEID (that one is in the NOR) and in the drive.
- **[inference] Which bank is active.** The dump has two CoreOS banks, and the SYSCON says which one
  boots. After any PUP install the bank flips. Restoring the 2026-10-13 dump after that would boot
  an old CoreOS against new dev_flash on the SSD, and the console would probably ask for a firmware
  reinstall. So record the active bank with each dump (xai_plugin "Show ROS Bank", read-only), and
  **take a fresh dump after every PUP install**.

### Checking a dump

- **[fact]** PyPS3checker (littlebalup/PyPS3tools) reads regular and byte-reversed NOR dumps (E3
  dumps are reversed). It checks structure, per-console consistency, region repeats and known ROS
  and trvk hashes. Exit 0 = clean, 2 = warnings, 3 = danger. Our 0/0 is the best result it gives.
  BwE's validator does more checks; it's Windows-only, and its repo didn't load for me.
- **[fact]** psdevwiki's corruption signs: misspelled region names (e.g. "eIIH" for eEID), a bootldr
  that's all FF, a bootldr not starting with 00 00.
- **[fact]** For hardware reads: read two or three times and compare. Any difference means a wiring
  problem (NORway README; quade.co guide).

### Hardware: what a NOR recovery takes on DYN-001

- **[fact]** DYN-001 uses psdevwiki's NOR test-point "layout 3": pads between the South Bridge and
  the NOR on the board's underside, plus ground and 3.3 V. Supported flashers: Teensy++ 2.0 with
  NORway (judges), Progskeet 1.x, PNM, E3 / E3 Linker (clip), and a TSOP56 360-clip. NORway's
  changelog lists DYN-001's three NOR parts.
- **[fact]** NORway: the Teensy++ 2.0 runs at 3.3 V (cut the 5 V trace, bridge the 3 V pads),
  connects straight to the PC (no hub), and drives TRISTATE (PE7) to hold the console's own chipset
  off the bus. The PS3 stays plugged into its PSU in standby. Sequence: start NORway, power the
  console on (it must not boot), `dump`, `release`, power off. A 16 MB dump takes ~1 min, a full
  write ~4.5 min. The quade.co Slim guide wired ~50 thin wires and took a little over an hour;
  "unknown" manufacturer or chip in NORway's output means bad wiring.
- **[fact]** consolemods (E3): a hardware flasher is "NOT recommended unless the console is
  bricked"; some DYN-001 boards need a resistor near the clip edge shaved for the E3 clip.
- **[unverified]** The NORway repo has an `rp2350b` folder (only an image), and no maintained Pico
  NOR flasher for the PS3 turned up. The Xbox 360 "PicoFlasher" is a different tool. BadWDSD
  (RP2040) is a modchip for 25xx/30xx/40xx, not a flasher, and doesn't support DYN-001.
- **[inference]** Teensy++ 2.0 boards are hard to buy new; budget for a clone, or a Progskeet or
  E3 clip from the used market. Keep it simple: buy the flasher, wire it and do the read comparison
  before any write ever happens, so it's a tested tool and not an emergency purchase.

### SYSCON EEPROM backup (the missing half)

- **From GameOS, read-only, no hardware:**
  - xai_plugin "Dump SYSCON Error Log";
  - "Dump EEPROM": **[fact]** 6 × 256 bytes through the Update Manager window (0x2F00, 0x3000,
    0x48000, 0x48800, 0x48C00, 0x48D00). It pokes six lv1 words to lift the access checks and puts
    them back afterwards, a RAM-only change. This covers the flags (bank, SPU count, product and
    recovery modes), not the thermal config.
  - "Show ROS Bank" and "Check 8th SPE" are single-byte reads.
- **From Linux under OtherOS++, read-only:** **[fact]** psdevwiki SC EEPROM shows graf_chokolo's
  `ps3dm_scm /dev/ps3dmproxy 0x48000 0xFF` after `dmpatch.sh`. That needs the dispatcher-manager
  patch and ps3dm tools we don't build. Not worth it while xai_plugin does the same from the XMB.
- **Full EEPROM, including the thermal config:** only over the SYSCON UART on a Sherwood (no external
  chip to clip onto). That's three wires (TX, RX, GND) soldered to the DYN-001 pads, a 3.3 V USB
  serial adapter, then auth and `EEP GET` over the whole 0x8000 bytes. It's a hardware mod, so it
  needs the owner's explicit go.

### Pre-flight checklist (before any persistent write)

1. The owner has asked for this specific write, and CLAUDE.md's safety rules have been changed by
   the owner to allow it. Agents don't decide this.
2. Fresh NOR dump taken **after** the last PUP install; PyPS3checker 0 dangers; a second dump `cmp`
   equal; active ROS bank recorded; SHA-256 recorded; three copies (one off the PC), encrypted.
3. For SYSCON writes: a full SYSCON EEPROM dump over UART, read twice and compared; the factory
   thermal config from psdevwiki (CRC32 8752BF08) checked against our own block.
4. The recovery tool for that class tested by **reading** first: the hardware flasher reads the NOR
   and matches the software dump; the UART authenticates and reads back known bytes.
5. Power: the console on a UPS or at least a stable outlet, no one touching the switch; for PUPs, the
   PUP's MD5 checked; nothing else running.
6. Cooling: webMAN fan on SYSCON, or fixed ≥ 60%, and a supervised session (CLAUDE.md).
7. The way back written down step by step before starting, and the write done once, by hand, with
   the owner present.

## 5. zecoxao's 2026 lv0ldr key write-up

- **[fact]** zecoxao's write-up ("the construct", zecoxao.github.io/writeup) and BiteYourConsole
  (2026-09-27): jestero's team recovered **one console's per-console lv0ldr body key, from a CECHL**
  (a late fat model). The SPU ROM was imaged after decapping the Cell. It holds the AES code for
  lv0ldr and metldr, not the key; the key is read through SPU channel 66, which locks after use.
  The attack glitches the register clean-up at the right moment, then reads SPU **register 7**,
  which holds a value from the key expansion of the channel-66-derived key. Running AES backwards
  from it gives the body key and the CMAC key. Tools credited: anergistic, lvx, custom SYSCON UART
  tools. A `decrypt_bootldr.py` run on `CECHL_bootldr.bin` shows MAC PASS.
- **[fact]** Stated as not done ("all theoretical now but should be practical at some point"): the
  channel 66 key itself (the root), signing custom lv0ldr/metldr, decrypting lv0ldr.2/metldr.2,
  CFW and unbricking across models, skipping dead Blu-ray or Wi-Fi boards, **re-pairing SYSCON and
  Blu-ray boards**. Slim 3000 and Super Slim are named as future targets via kafuu's qCFW.
- **[inference] What it changes for us:** nothing today. It's one key from one fat console, by a
  decap-and-glitch lab process. Our Slim already runs CFW. A lost or corrupted NOR still can't be
  rebuilt without our own dump. If the channel-66 root key is ever recovered, rebuilding per-console
  data (bootldr/metldr) and re-pairing a donor SYSCON could become possible. That would turn some
  class 5 failures into class 3 or 4, but it's a hope, not a plan. Keep treating the dump as
  irreplaceable.
- **[fact] Related, 2025–26:** BadHTAB (aomsin2526, an lv1 exploit, ~5–10% success) and BadWDSD/qCFW
  (an RP2040 modchip plus flash-written "Stagex" and qCFW for 25xx/30xx/40xx) bring CFW-like
  firmware to models that can't run CFW. We don't need either. **[unverified]** A 2026 psx-place post
  claims Cell/XDR clock changes through Sherwood SYSCON on a few models, with the master clock
  address still unknown.
- **[fact] Permanent things:** I found no source saying PS3 firmware burns eFuses during updates.
  The root of the chain is in the Cell (bootldr and metldr keys can't be changed on existing
  hardware), and the eid_root_key is per-console. xai_plugin can dump it ("Dump ERK"). That's useful
  for reading our own encrypted storage later, and it's just as private as the NOR dump.

## 6. Recommendation, ranked

Value for this project at acceptable risk, in order. The first four write nothing persistent.

(Status 2026-10-18 evening: 4 is done another way: the System Manager answers the Linux LPAR, and
the fan policy driver (local/0040, 0042, docs/THERMAL.md) set SYSCON automatic and full speed on
kernel .433; the sysmgr character device mentioned in 4 is dropped from our kernels. 1, 2 and 3 have
not been tried; 5-7 stay off without the owner's request.)

1. **Use the 7th SPE (no write).** Add a Petitboot entry with `spu_base.enum_shared=1` next to the
   normal one, and check `/sys/devices/system/spu` shows 7. Then re-run the speq benchmarks and
   `ps3-gameos-mount` in the same boot as the 6-SPE baseline (same-boot rule). Off-switch: boot the
   other entry. This is the only real hardware unlock on the list that costs nothing.
2. **Close the backup gaps (read-only, no hardware).** From the XMB with xai_plugin: a second "Dump
   Flash" (`cmp` with the first, PyPS3checker), "Show ROS Bank", "Check 8th SPE", "Dump EEPROM",
   "Dump SYSCON Error Log", and optionally "Dump ERK". Store them encrypted in three places, out of
   git. Write the bank and hashes into a private note next to the dump.
3. **Probe the running lv1 from Linux, read-only.** Is lv1 peek (hypercall 182) present at Linux
   time? Then peek the `gpu-4kb-iopage` site to see whether Evilnat's flashed lv1 already carries it.
   If peek is there and the patch isn't, poking it is a RAM-only change (class 1). It might let
   ps3rsx shrink or drop the 64 MB GART reservation, which matters more than anything else on this
   list for RAM. Ask the owner before the first poke; a wrong one hangs the console.
4. **Fan: runtime first.** Settle in THERMAL.md whether Linux can read temperatures and set the
   fan policy through the System Manager (OtherOS++ advertises full System Manager access, and our
   kernel has the sysmgr character device patch). That gets the fan answer without touching the
   SYSCON.
5. **Buy and test the recovery tools, without writing.** A NOR flasher (Teensy++ 2.0 + NORway,
   or an E3 clip) wired and proven by a read that matches the dump; a 3.3 V UART adapter on the
   SYSCON pads, proven by auth and `EEP GET`. This is a hardware mod, so only when the owner asks.
   It's the prerequisite for 6 and 7, and it makes every later accident class 3/4 instead of a
   gamble.
6. **Only after 5, and only if 4 fails: a gentler fan curve in the SYSCON thermal config.** Start
   from our own dumped block and the published factory one, change only the RSX table, fix the
   checksum at 0x7FE, and verify by readback before powering on. Keep a UART session ready to put
   the factory block back.
7. **Maybe later: the 8th SPE (0x48C30 = 0x07).** Only with 5 done, a UART restore tested, and a
   measured reason (7 SPEs saturated). The SPE may be defective; that's why it's off.

Not worth it: a newer CFW PUP for its own sake (no Linux gain, and it outdates the NOR dump); a
custom-built lv1 in CoreOS (no patch we need is out of reach of runtime pokes, and the PS3MFW
patterns are stale past 4.55); RSX overclocking (we're PPE-bound, and nothing cools it under Linux);
a bigger OtherOS HDD region (we boot from USB/NFS; resizing again wipes GameOS's disk).

**Never:**
- SYSCON firmware or patch-area writes (`firmud`, scuartd updates).
- Product mode, QA or token edits beyond xai_plugin's own documented toggles.
- Anything that writes bootldr, eEID, metldr, cISD/cCSD or cvtrm.
- A NOR image from another console, or a "rebuilt" one.
- BadWDSD/qCFW on this model.
- eFuse or Cell-key experiments.
- Loading `ps3flash`, `ps3physmem` or `ps3_lv1call` write paths on a normal session (we already
  build `PS3_FLASH`, `PS3_NOR_FLASH`, `REPOSITORY_WRITE`, `ENCDEC` out; keep it that way, and keep
  lv1call/disp_manager blacklisted while ps3rsx runs, as DRIVER-PLAN.md says).

## 7. Open questions

- Does `spu_base.enum_shared=1` on our 6.12 kernel give 7 SPEs, and does anything we use break?
- Is lv1 peek/poke (182/183) present in Evilnat 4.92's flashed lv1 at Linux time? Which of the
  "Rebug Tools" patches are baked in?
- What does `gpu-4kb-iopage` change in lv1 exactly (which call's page size), and can it be applied
  after boot?
- Which ROS bank is active right now?
- Does a Sherwood SW2 still answer on UART after a thermal config with a bad checksum?
- ~~Can the System Manager set the fan policy from Linux under OtherOS++?~~ Yes: SYSCON automatic and
  full speed from Linux on kernel .433 (2026-10-18, THERMAL.md).
- What makes the kernel try to write the OS area ("os_area_queue_work_handler: Could not update FLASH
  ROM", 2026-10-15)? Still not looked at (PLAN.md open questions).

## 8. Sources

psdevwiki pages were read from Wayback Machine snapshots (2025); the live site blocks scripts.

- psdevwiki: [DYN-00x](https://www.psdevwiki.com/ps3/DYN-001),
  [SW2-301](https://www.psdevwiki.com/ps3/SW2-301),
  [Sherwood](https://www.psdevwiki.com/ps3/Sherwood),
  [Syscon Hardware](https://www.psdevwiki.com/ps3/Syscon_Hardware) (UART, auth, platforms),
  [SC EEPROM](https://www.psdevwiki.com/ps3/SC_EEPROM) (UM offsets 0x48C24, 0x48C30, Linux dump),
  [Syscon Thermal Configs](https://www.psdevwiki.com/ps3/Syscon_Thermal_Configs) and
  [its talk page](https://www.psdevwiki.com/ps3/Talk:Syscon_Thermal_Configs),
  [Flash](https://www.psdevwiki.com/ps3/Flash) (NOR layout, per-console regions),
  [Hardware Flashers: testpoints layout 3](https://www.psdevwiki.com/ps3/Hardware_Flashers:testpoints_layout_3),
  [OtherOS++](https://www.psdevwiki.com/ps3/OtherOS%2B%2B) (features, 7th SPU, HDD region),
  [Platform ID](https://www.psdevwiki.com/ps3/Platform_ID), [Syscon Firmware](https://www.psdevwiki.com/ps3/Syscon_Firmware),
  [Validating flash dumps](https://www.psdevwiki.com/ps3/Validating_flash_dumps) (snippet only).
- xai_plugin (Evilnat), [github.com/xXEvilnatXx/xai_plugin](https://github.com/xXEvilnatXx/xai_plugin):
  README (option list), `xai_plugin/otheros.cpp` (VFLASH setup, Petitboot, boot flag),
  `eeprom.cpp` (runtime lv1 patches for the EEPROM dump), `cfw_settings.cpp` and `functions.h`
  (8th SPE toggle, ROS bank, recovery flag), `overclock.cpp` (RSX clocks by lv1 poke).
- PS3MFW [`tasks/patch_lv1.tcl`](https://github.com/anonymous-ElmerFudd/PS3MFW/blob/master/tasks/patch_lv1.tcl)
  (lv1 patch list, gpu-4kb-iopage); [Rebug: OtherOS-enabled FW](https://rebug.me/create-otheros-enabled-rebug-fw/);
  [dblsaiko/PS3-OtherOS](https://github.com/dblsaiko/PS3-OtherOS);
  [CheezeCake/ps3linux-patches](https://github.com/CheezeCake/ps3linux-patches) (spu-enum-shared).
- Linux [`arch/powerpc/platforms/ps3/mm.c`](https://github.com/torvalds/linux/blob/master/arch/powerpc/platforms/ps3/mm.c),
  [`spu.c`](https://github.com/torvalds/linux/blob/master/arch/powerpc/platforms/ps3/spu.c).
- Flashers and checking: [NORway](https://github.com/hjudges/NORway) (README, changelog);
  [PyPS3tools](https://github.com/littlebalup/PyPS3tools); [quade.co Slim Teensy guide](https://quade.co/2017/ps3-slim-custom-firmware-install/);
  consolemods [E3 Flasher](https://consolemods.org/wiki/PS3:E3_Flasher), [Safe Mode](https://consolemods.org/wiki/PS3:Safe_Mode),
  [Upgrading your Storage](https://consolemods.org/wiki/PS3:Upgrading_your_Storage),
  [Recovering Data](https://consolemods.org/wiki/PS3:Recovering_Data), [Syscon Diagnosis](https://consolemods.org/wiki/PS3:Syscon_Diagnosis)
  (consolemods pages via search snippets).
- SYSCON UART tools: [db260179/ps3syscon](https://github.com/db260179/ps3syscon),
  [jw0710/PS3_Syscon_Reader_QoL_Update](https://github.com/jw0710/PS3_Syscon_Reader_QoL_Update);
  psx-place threads [Fantable in SW Syscons not working?](https://psx-place.com/threads/fantable-in-sw-syscons-not-working.45019/),
  [Modifying SYSCON Fan settings](https://www.psx-place.com/threads/modifying-syscon-fan-settings-better-than-webman.34626/)
  (snippets).
- 2026 key work: [zecoxao write-up](https://zecoxao.github.io/writeup/);
  [BiteYourConsole, 2026-09-27](https://www.biteyourconsole.net/2026/09/27/scena-ps3-dopo-20-anni-la-scena-ps3-recupera-la-prima-chiave-body-per-console-del-lv0ldr-su-una-cechl/).
- Other: [BadWDSD](https://github.com/aomsin2526/BadWDSD);
  [psx-place: Cell overclocking via syscon](https://psx-place.com/threads/cell-overclocking-via-syscon-is-now-possible.49529/) (snippet);
  [Level1Techs thread on the 8th SPE](https://forum.level1techs.com/t/playstation-2/149481?page=4) (snippet).
