# Flash, SYSCON and hypervisor writes: what they would unlock and what they risk

This is research only. Nothing in it was tried on the console, and it changes none of the rules in [CLAUDE.md](../../CLAUDE.md) (no SYSCON writes, no flash patches beyond the OtherOS++ install, hardware mods only on request). The question was whether writing flash, the SYSCON or the hypervisor would unlock more for us, what is safe, and how we would get back if it went wrong. It is input for a decision by the user.

Facts about this console (model, firmware, where things live on a NOR Slim, our backups, the recovery state and the pre-flight checklist) are in [CONSOLE.md](../CONSOLE.md). Terms are in the [glossary](../README.md#glossary). Unported ideas from here are in [backlog/hardware.md](../backlog/hardware.md).

How to read it: **[fact]** lines carry a source (see [Sources](#sources)). **[inference]** lines are my reasoning from those facts and need checking before anyone acts on them. **[unverified]** marks claims found only in search snippets or forum posts. psdevwiki and psx-place block scripted fetches, so the psdevwiki pages were read from Wayback Machine snapshots (2025); xai_plugin and PS3MFW were read from their GitHub source.

## Inventory: what is writable and what it would unlock

"Unlock" means for our goal: an open Linux on the PS3 that runs games well. The two limits that matter today are RAM (240 MB, PPE-bound frames; see [HARDWARE-LEVERAGE.md](HARDWARE-LEVERAGE.md)) and heat. Heat control is now settled without any persistent write: Linux reads the temperatures and sets the fan policy through the System Manager ([THERMAL.md](../THERMAL.md)).

| # | Piece | How it is written | What it would unlock for us | Persistence |
|---|---|---|---|---|
| A | **7th SPE** (the "shared" SPE) | No write. Kernel parameter `spu_base.enum_shared=1`; our kernel carries the patch (`linux/patches/ps3/0100-spu-enum-shared-param.patch`) | One more SPE for libspeq: 6 to 7, about 17% more SPU throughput | Per boot |
| B | lv1 patches in RAM (runtime) | lv1 poke (hypercall 183) from GameOS (xai_plugin "Rebug Tools", webMAN), or from Linux if the running lv1 has peek/poke | Anything lv1 gates: 4 KB GPU IO pages (`gpu-4kb-iopage`), RSX clocks (xai "Overclock Tools" pokes lv1), SS service access | Gone at power-off / cold boot |
| C | lv1 patches in the flashed CoreOS (`lv1.self` in ros0/ros1) | A CFW PUP built with PS3MFW `patch_lv1.tcl`-style patches, installed from the XMB or Recovery | Same list as B, active from the first instruction of every boot, including the OtherOS boot | Until the next PUP |
| D | ROS bank (any CFW/OFW update) | PUP install from XMB or Recovery Menu; writes the inactive bank and flips the bank flag in SYSCON | A newer Evilnat (4.93: "LV1 patch for custom fan speed on PS2 EMU"). Nothing for Linux that I could find | Until the next PUP |
| E | VFLASH region table and region 5 (OS area, Petitboot, boot flag) | xai_plugin "Install OtherOS" / "Set boot flag"; Linux via `ps3flash` (we build it out) | Already done. A newer Petitboot; a bigger region 5 | On the SSD |
| F | HDD region table (OtherOS++ HDD region size) | `create_hdd_region.sh` from Petitboot, ps3stor tools, or PUPs with a preset split | More disk for Linux (we run from USB/NFS/SSD image today) | On the SSD; GameOS reformats if its region is damaged |
| G | SYSCON EEPROM via the hypervisor's Update Manager window (from GameOS) | `update_mgr_write_eprom()` (xai_plugin: 8th SPE, recovery-mode flag, product mode, QA) | **8th SPE** (0x48C30: 0x06 to 0x07), recovery-mode flag | Persistent, in the SYSCON |
| H | SYSCON thermal config (fan tables, shutdown thresholds) | SYSCON UART in SW (auth) mode: `EEP SET`, or tools such as jw0710's GUI, Kohada | A fan curve that suits Linux, with no webMAN loop needed | Persistent, in the SYSCON |
| I | SYSCON firmware patch area | Sony's SYSCON updates; UART `firmud` | Nothing for us | Persistent |
| J | NOR per-console regions (bootldr, eEID, metldr, cISD/cCSD, cvtrm) | Hardware flasher, or software NOR writers (PS3Xploit flash writer, CFW installers) | Nothing for us | Persistent |
| K | eFuses / Cell keys | Not writable by any known tool | Nothing | Permanent |

What each gives us, in plain terms:

- **A, the 7th SPE.** **[fact]** psdevwiki OtherOS++: "Add `spu_base.enumerate_shared=1` to your kboot entry ... Enabling 7th SPU on Linux will make most of SS services unusable because they use the 7th SPU". Our patch spells the parameter `enum_shared` (`module_param(enum_shared, ...)` in `spu_base.c`), so the switch is in our kernel and just off. **[inference]** The SS services are GameOS-facing hypervisor services (Dispatcher Manager clients, isolated SPU modules). GameOS's disk regions are decrypted by ENCDEC, a hardware block, so `ps3-gameos-mount` should keep working, but that needs a test. Nothing persistent changes: a boot without the parameter is the off-switch.
- **B and C, lv1 patches.** **[fact]** The PS3MFW patch list (`patch_lv1.tcl`) and xai_plugin's "Rebug Tools" offer the same set: mmap of any memory, HTAB write, MFC_SR1 mask, DABR, ENCDEC 0x85, **4 KB GPU IO pages** ("Allow 4kb IO page size for GPU GART memory"; the patch forces a `li r0,0x1000`), dispatcher-manager access, repository nodes in any LPAR, storage ACL skip, and others. **[fact]** xai_plugin applies its patches at runtime with `lv1_poke32()` and, for the EEPROM dump, restores the original words afterwards (`eeprom.cpp`). **[inference]** The interesting one for us is the 4 KB GPU IO page. The ps3rsx GART takes 1 MiB chunks from the 64 MiB CMA area on demand, 6-14 MB of it in games, and gives them back when idle, so it costs RAM only while the GPU uses it (`ps3rsx/src/gart.c`; [KERNEL-MEMORY.md](../KERNEL-MEMORY.md) "CMA size"). With 4 KB IO pages the GART could map scattered ordinary pages instead of contiguous chunks. That would save the rounding of partly used chunks and the CMA migrations a new chunk needs, and maybe let the CMA area shrink, which by itself returns no RAM. So a few MB at most (an estimate), not a large win, and a guess about how the patch behaves that nobody documents. **[fact]** No patch in either list exposes raw RSX registers, more XDR or more VRAM. VRAM is already the full 256 MB, and the XDR size comes from lv1's repository (Linux `mm.c`: "Assume a single highmem region").
- **[inference] Runtime versus flashed, for Linux.** Every trip to Linux is a reboot with the OtherOS flag set (the user reboots from the XMB; xai's flag tool uses a hard reboot). A hard reboot runs lv0 again, which reloads lv1 from the NOR. So patches poked from GameOS are gone by the time Petitboot runs, and whatever OtherOS++ capabilities Linux has (it reads GameOS's disk regions, [GAMEOS-HDD.md](../GAMEOS-HDD.md)) must come from the lv1 in Evilnat's flashed CoreOS. Evilnat 4.92's feature list includes "Peek and Poke (LV1)" and "Compatibility with OtherOS" **[unverified, snippet]**. If peek/poke is in the flashed lv1, Linux could apply a patch like the 4 KB IO page itself, in RAM, with no flash write. Whether that is early enough (before lv1 sets up the GPU IO space) is unknown.
- **D, firmware updates.** Low value for Linux. The one practical effect: a PUP install flips the ROS bank and rewrites `dev_flash` on the SSD, so after any PUP the NOR dump is out of date ([CONSOLE.md](../CONSOLE.md#backups-and-recovery)).
- **G, the 8th SPE.** **[fact]** psdevwiki SC EEPROM: SPU num at 0x48C30, "can be set to 0x07 to enable the 8 SPE". xai_plugin's "Toggle 8th SPE" writes it through the Update Manager (`update_mgr_write_eprom(SPE_8TH_EEPROM_OFFSET, ...)`). **[unverified]** A Level1Techs user enabled it on two consoles: one worked, the other's 8th SPE was "definitely defective". **[inference]** The 8th SPE is off because Sony sold Cells with one bad or unvalidated SPE. On a console where it is bad, lv1 or GameOS could crash at boot. Then the in-XMB toggle cannot be reached to undo it, and the way back is the SYSCON UART. Whether Linux would even get that SPE (exclusive, or a second shared one) is also unknown.
- **H, the fan curve.** **[fact]** Sherwood thermal config: 0x200 bytes at 0x250, checksum at 0x7FE covering 0x000-0x7FD. The factory config for DYN-001 + SW2-301 is published on psdevwiki (CRC32 8752BF08), so a known-good image exists to restore. **[unverified, forum]** People report that SW SYSCONs ignore custom curves that do not fit the firmware's logic, and one CECHL owner got a triple beep and red light from a bad value until it was fixed. jw0710's GUI lists its fan-curve editor as not supported on SW. **[inference]** The win would be real, but the runtime fan policy from Linux, which now works, gets most of it with no persistent write.
- **I to K.** Nothing for us, and the worst failure modes. Never.

## Risk per piece, and how you would get back

Recovery classes, from mild to final:

1. **Per boot**: drop a boot parameter or power-cycle.
2. **Software**: the XMB, Safe/Recovery Menu (reinstall PUP, Restore File System), or xai_plugin.
3. **Hardware flasher**: the NOR rewritten from our dump by a Teensy++/NORway, Progskeet or E3.
4. **SYSCON UART**: solder three wires to the SYSCON test pads, authenticate, rewrite EEPROM bytes.
5. **Permanent**: no path with public tools.

| # | Piece | Who has done it, with what | Failure modes | Worst-case recovery |
|---|---|---|---|---|
| A | 7th SPE | OtherOS++ users, glevand's patch (we carry it) | SS-service users break; a kernel lockdep warning on some 3.x kernels **[unverified]** | 1 |
| B | lv1 runtime poke | Rebug/Evilnat toolboxes, xai_plugin | Wrong address crashes lv1: hard hang, power-cycle | 1 |
| C | lv1 in a flashed CoreOS | Gitbrew (3.41/3.55 OtherOS++ PUPs), Rebug (3.55 OtherOS++ builds); PS3MFW patterns marked "cannot find" for newer firmware | A patched lv1 that will not boot: the console fails before the XMB | 2 (Recovery Menu, if lv2 still starts) or 3 |
| D | ROS bank (PUP) | Everyone with CFW | Interrupted install (power loss), bad PUP, PUP for the wrong model | 2 usually; 3 if both banks or the bank flag go bad |
| E | VFLASH / OS area | xai_plugin, Rebug toolbox, our own install | Petitboot will not start, boot flag stuck, `dev_flash` damaged | 2: reinstall Petitboot; reinstall PUP; Restore PS3 System (formats the disk) |
| F | HDD region table | dblsaiko/glevand scripts | GameOS sees a damaged region and reformats the disk, deleting the Linux region | 2, with data loss |
| G | 8th SPE / UM EEPROM flags | xai_plugin, Rebug toolbox | Bad SPE: crashes or no boot; wrong flag (product mode, QA): odd boot modes | 2 if GameOS still boots, else 4 |
| H | Thermal config | psx-place users, jw0710 GUI (Mullion), Kohada | Bad checksum: no boot; bad curve: overheating; SW ignoring the table | 4 (the UART still answering after a bad write is **[unverified]**) |
| I | SYSCON firmware | Sony; developers with patched scuartd | Failed patch write | 5 for us: zecoxao lists "re-pairing syscon" as future work, so no donor SYSCON can replace it today **[inference]** |
| J | NOR per-console regions | PS3Xploit flash writers, E3/NORway users | Wrong dump, a write cut short, bad wiring | 3, only with our own verified dump |
| K | eFuses / Cell keys | n/a | n/a | 5 |

Notes on the classes:

- **[fact]** The Recovery Menu (hold power from off until the double beep) offers Restore Default Settings, Restore File System, Rebuild Database, Restore PS3 System and a PUP install from USB. xai_plugin's "Toggle Recovery Mode" sets the same flag from the XMB, on NOR models only. Source: consolemods "Safe Mode", xai_plugin `cfw_settings.cpp` (`RECOVERY_MODE_FLAG_OFFSET`).
- **[fact]** The SYSCON UART is 3.3 V TTL at 57600 baud. Commands are `<command>:<checksum>`, where the checksum is the byte sum & 0xFF. Without auth, `EEP GET`, `EEP SET`, `FAN` and friends answer `NG F0000005`. Auth is an AUTH1/AUTH2 AES challenge-response, and the keys are public in db260179/ps3syscon. psdevwiki shows DYN-001's UART test pads in a photo (Syscon Hardware).
- **[fact]** The PS3 boots only with its own per-console data: eEID, bootldr and metldr are encrypted with keys inside that Cell. A dump from another console does not help, and Sony's keys for producing new per-console data are not public (psdevwiki Flash; zecoxao 2026, below). Our dump is the only way back for class J.

## zecoxao's 2026 lv0ldr key write-up

- **[fact]** zecoxao's write-up ("the construct", zecoxao.github.io/writeup) and BiteYourConsole (2026-09-27): jestero's team recovered one console's per-console lv0ldr body key, from a CECHL (a late fat model). The SPU ROM was imaged after decapping the Cell. It holds the AES code for lv0ldr and metldr, not the key; the key is read through SPU channel 66, which locks after use. The attack glitches the register clean-up at the right moment, then reads SPU register 7, which holds a value from the key expansion of the channel-66-derived key. Running AES backwards from it gives the body key and the CMAC key. Tools credited: anergistic, lvx, custom SYSCON UART tools. A `decrypt_bootldr.py` run on `CECHL_bootldr.bin` shows MAC PASS.
- **[fact]** Stated as not done ("all theoretical now but should be practical at some point"): the channel 66 key itself (the root), signing custom lv0ldr/metldr, decrypting lv0ldr.2/metldr.2, CFW and unbricking across models, skipping dead Blu-ray or Wi-Fi boards, re-pairing SYSCON and Blu-ray boards. Slim 3000 and Super Slim are named as future targets via kafuu's qCFW.
- **[inference]** What it changes for us: nothing today. It is one key from one fat console, by a decap-and-glitch lab process. Our Slim already runs CFW. A lost or corrupted NOR still cannot be rebuilt without our own dump. If the channel-66 root key is ever recovered, rebuilding per-console data (bootldr/metldr) and re-pairing a donor SYSCON could become possible. That would turn some class 5 failures into class 3 or 4, but it is a hope, not a plan. Keep treating the dump as irreplaceable.
- **[fact]** Related, 2025-26: BadHTAB (aomsin2526, an lv1 exploit, about 5-10% success) and BadWDSD/qCFW (an RP2040 modchip plus flash-written "Stagex" and qCFW for 25xx/30xx/40xx) bring CFW-like firmware to models that cannot run CFW. We need neither. **[unverified]** A 2026 psx-place post claims Cell/XDR clock changes through Sherwood SYSCON on a few models, with the master clock address still unknown.
- **[fact]** Permanent things: I found no source saying PS3 firmware burns eFuses during updates. The root of the chain is in the Cell (bootldr and metldr keys cannot be changed on existing hardware), and the eid_root_key is per-console. xai_plugin can dump it ("Dump ERK"), which would be useful for reading our own encrypted storage later, and is just as private as the NOR dump.

## Recommendation, ranked

Value for this project at acceptable risk, in order. The first three write nothing persistent. The fourth (the fan) is done another way: the System Manager answers the Linux LPAR, and the fan policy driver (`local/0040`, `0042`, [THERMAL.md](../THERMAL.md)) set SYSCON automatic and full speed on kernel .433. Items 1 to 3 are not tried, and 5 to 7 stay off without the user's request.

1. **Use the 7th SPE (no write).** Add a Petitboot entry with `spu_base.enum_shared=1` next to the normal one, and check `/sys/devices/system/spu` shows 7. Then re-run the speq benchmarks and `ps3-gameos-mount` in the same boot as the 6-SPE baseline (same-boot rule). Off-switch: boot the other entry. This is the only real hardware unlock on the list that costs nothing.
2. **Close the backup gaps (read-only, no hardware).** From the XMB with xai_plugin: a second "Dump Flash" (`cmp` with the first, PyPS3checker), "Show ROS Bank", "Check 8th SPE", "Dump EEPROM", "Dump SYSCON Error Log", and optionally "Dump ERK". Store them encrypted in three places, out of git. Write the bank and hashes into a private note next to the dump.
3. **Probe the running lv1 from Linux, read-only.** Is lv1 peek (hypercall 182) present at Linux time? Then peek the `gpu-4kb-iopage` site to see whether Evilnat's flashed lv1 already carries it. If peek is there and the patch is not, poking it is a RAM-only change (class 1). It would let ps3rsx map 4 KB pages into the GART instead of 1 MiB CMA chunks, a few MB at most, so the probe's value is mostly the yes/no on runtime lv1 patches. Ask the user before the first poke; a wrong one hangs the console.
4. **Fan: runtime first.** Done, see above.
5. **Buy and test the recovery tools, without writing.** A NOR flasher (Teensy++ 2.0 + NORway, or an E3 clip) wired and proven by a read that matches the dump; a 3.3 V UART adapter on the SYSCON pads, proven by auth and `EEP GET`. This is a hardware mod, so only when the user asks. It is the prerequisite for 6 and 7, and it makes every later accident class 3 or 4 instead of a gamble.
6. **Only after 5, and only if 4 fails: a gentler fan curve in the SYSCON thermal config.** Start from our own dumped block and the published factory one, change only the RSX table, fix the checksum at 0x7FE, and verify by readback before powering on. Keep a UART session ready to put the factory block back.
7. **Maybe later: the 8th SPE (0x48C30 = 0x07).** Only with 5 done, a UART restore tested, and a measured reason (7 SPEs saturated). The SPE may be defective; that is why it is off.

Not worth it: a newer CFW PUP for its own sake (no Linux gain, and it outdates the NOR dump); a custom-built lv1 in CoreOS (no patch we need is out of reach of runtime pokes, and the PS3MFW patterns are stale past 4.55); RSX overclocking (we are PPE-bound, and nothing cools it under Linux); a bigger OtherOS HDD region (resizing again wipes GameOS's disk).

**Never:**

- SYSCON firmware or patch-area writes (`firmud`, scuartd updates).
- Product mode, QA or token edits beyond xai_plugin's own documented toggles.
- Anything that writes bootldr, eEID, metldr, cISD/cCSD or cvtrm.
- A NOR image from another console, or a "rebuilt" one.
- BadWDSD/qCFW on this model.
- eFuse or Cell-key experiments.
- Loading `ps3flash`, `ps3physmem` or `ps3_lv1call` write paths on a normal session. We already build `PS3_FLASH`, `PS3_NOR_FLASH`, `REPOSITORY_WRITE` and `ENCDEC` out; keep it that way, and keep lv1call and disp_manager blacklisted while ps3rsx runs ([DRIVER.md](../DRIVER.md)).

## Open questions

- Does `spu_base.enum_shared=1` on our 6.12 kernel give 7 SPEs, and does anything we use break?
- Is lv1 peek/poke (182/183) present in Evilnat 4.92's flashed lv1 at Linux time? Which of the "Rebug Tools" patches are baked in?
- What does `gpu-4kb-iopage` change in lv1 exactly (which call's page size), and can it be applied after boot?
- Which ROS bank is active right now?
- Does a Sherwood SW2 still answer on UART after a thermal config with a bad checksum?
- What `rtc_diff` does Petitboot hand our kernel, and does Petitboot's own kernel ever write the OS area? (`xxd /proc/device-tree/linux,rtc_diff`; we do not control the installed Petitboot image.) The kernel's own OS-area write is stopped by `local/0044`, see [CONSOLE.md](../CONSOLE.md#the-kernels-os-area-write).

Answered since the first draft: the System Manager can set the fan policy from Linux under OtherOS++ (SYSCON automatic and full speed on kernel .433, 2026-10-18); the kernel's OS-area write comes from the 11-minute RTC sync once timesyncd has NTP time.

## Sources

psdevwiki pages were read from Wayback Machine snapshots (2025); the live site blocks scripts.

- psdevwiki: [DYN-00x](https://www.psdevwiki.com/ps3/DYN-001), [SW2-301](https://www.psdevwiki.com/ps3/SW2-301), [Sherwood](https://www.psdevwiki.com/ps3/Sherwood), [Syscon Hardware](https://www.psdevwiki.com/ps3/Syscon_Hardware) (UART, auth, platforms), [SC EEPROM](https://www.psdevwiki.com/ps3/SC_EEPROM) (UM offsets 0x48C24, 0x48C30, Linux dump), [Syscon Thermal Configs](https://www.psdevwiki.com/ps3/Syscon_Thermal_Configs) and [its talk page](https://www.psdevwiki.com/ps3/Talk:Syscon_Thermal_Configs), [Flash](https://www.psdevwiki.com/ps3/Flash) (NOR layout, per-console regions), [Hardware Flashers: testpoints layout 3](https://www.psdevwiki.com/ps3/Hardware_Flashers:testpoints_layout_3), [OtherOS++](https://www.psdevwiki.com/ps3/OtherOS%2B%2B) (features, 7th SPU, HDD region), [Platform ID](https://www.psdevwiki.com/ps3/Platform_ID), [Syscon Firmware](https://www.psdevwiki.com/ps3/Syscon_Firmware), [Validating flash dumps](https://www.psdevwiki.com/ps3/Validating_flash_dumps) (snippet only).
- xai_plugin (Evilnat), [github.com/xXEvilnatXx/xai_plugin](https://github.com/xXEvilnatXx/xai_plugin): README (option list), `xai_plugin/otheros.cpp` (VFLASH setup, Petitboot, boot flag), `eeprom.cpp` (runtime lv1 patches for the EEPROM dump), `cfw_settings.cpp` and `functions.h` (8th SPE toggle, ROS bank, recovery flag), `overclock.cpp` (RSX clocks by lv1 poke).
- PS3MFW [`tasks/patch_lv1.tcl`](https://github.com/anonymous-ElmerFudd/PS3MFW/blob/master/tasks/patch_lv1.tcl) (lv1 patch list, gpu-4kb-iopage); [Rebug: OtherOS-enabled FW](https://rebug.me/create-otheros-enabled-rebug-fw/); [dblsaiko/PS3-OtherOS](https://github.com/dblsaiko/PS3-OtherOS); [CheezeCake/ps3linux-patches](https://github.com/CheezeCake/ps3linux-patches) (spu-enum-shared).
- Linux [`arch/powerpc/platforms/ps3/mm.c`](https://github.com/torvalds/linux/blob/master/arch/powerpc/platforms/ps3/mm.c), [`spu.c`](https://github.com/torvalds/linux/blob/master/arch/powerpc/platforms/ps3/spu.c).
- Flashers and checking: [NORway](https://github.com/hjudges/NORway) (README, changelog); [PyPS3tools](https://github.com/littlebalup/PyPS3tools); [quade.co Slim Teensy guide](https://quade.co/2017/ps3-slim-custom-firmware-install/); consolemods [E3 Flasher](https://consolemods.org/wiki/PS3:E3_Flasher), [Safe Mode](https://consolemods.org/wiki/PS3:Safe_Mode), [Upgrading your Storage](https://consolemods.org/wiki/PS3:Upgrading_your_Storage), [Recovering Data](https://consolemods.org/wiki/PS3:Recovering_Data), [Syscon Diagnosis](https://consolemods.org/wiki/PS3:Syscon_Diagnosis) (consolemods pages via search snippets).
- SYSCON UART tools: [db260179/ps3syscon](https://github.com/db260179/ps3syscon), [jw0710/PS3_Syscon_Reader_QoL_Update](https://github.com/jw0710/PS3_Syscon_Reader_QoL_Update); psx-place threads [Fantable in SW Syscons not working?](https://psx-place.com/threads/fantable-in-sw-syscons-not-working.45019/), [Modifying SYSCON Fan settings](https://www.psx-place.com/threads/modifying-syscon-fan-settings-better-than-webman.34626/) (snippets).
- 2026 key work: [zecoxao write-up](https://zecoxao.github.io/writeup/); [BiteYourConsole, 2026-09-27](https://www.biteyourconsole.net/2026/09/27/scena-ps3-dopo-20-anni-la-scena-ps3-recupera-la-prima-chiave-body-per-console-del-lv0ldr-su-una-cechl/).
- Other: [BadWDSD](https://github.com/aomsin2526/BadWDSD); [psx-place: Cell overclocking via syscon](https://psx-place.com/threads/cell-overclocking-via-syscon-is-now-possible.49529/) (snippet); [Level1Techs thread on the 8th SPE](https://forum.level1techs.com/t/playstation-2/149481?page=4) (snippet).

Older versions and the dated history: [archive/FLASH-SYSCON.md](../archive/FLASH-SYSCON.md).
