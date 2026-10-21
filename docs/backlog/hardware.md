# Backlog: backups, the fan, lv1 and what stays out of scope

The console-level items: the backups not taken yet, the fan paths not exercised, read-only probes of
the hypervisor, and the things the safety rules keep out. Nothing here writes flash or SYSCON (the
fan policy through the System Manager is the one SYSCON-side setting the user allowed, 2026-10-18).
This console's hardware, the backups we hold and the recovery state are in
[CONSOLE.md](../CONSOLE.md); the research on flash and SYSCON in
[research/FLASH-SYSCON.md](../research/FLASH-SYSCON.md). Items are in rank order; the rank is the
one in [the ranked list](README.md). Terms: [glossary](../README.md#glossary).

### XMB-side backups

Rank 11. The read-only dumps xai_plugin makes from the XMB, stored encrypted off the PC.

**Why.** The one NOR dump (`backups/FLASH-NOR-FW4.92-20261013-232254.bin`, 16,777,216 bytes,
SHA-256 kept with the dump (not published), PyPS3checker 0 dangers and 0 warnings) is the only way back today, it was
taken before the OtherOS install, and it sits only on this PC's nearly full /home. The SYSCON EEPROM
half (bank flag, SPU count, tokens, thermal config, error log) has no backup at all, and neither has
VFLASH or GameOS's own data. After any PUP install the active CoreOS bank flips, so a dump must be
taken fresh after each one, with the bank recorded.

- **Payoff:** recoverability. **Effort:** S.
- **Risk:** low: the dumps are reads ("Dump EEPROM" pokes six lv1 words to lift the access checks
  and puts them back, a RAM-only change).
- **Needs:** the user at the XMB.
- **Status:** waiting on decision D7; never taken.
- **Source:** PLAN 4.1; [archive/FLASH-SYSCON.md, "4. Recovery for this console"](../archive/FLASH-SYSCON.md#4-recovery-for-this-console), ["6. Recommendation, ranked"](../archive/FLASH-SYSCON.md#6-recommendation-ranked) 2.

**To start.** With xai_plugin: a second "Dump Flash" (then `cmp` with the first, expect identical
while no firmware was installed since, and PyPS3checker on it), "Show ROS Bank", "Check 8th SPE",
"Dump EEPROM" (6 x 256 bytes: 0x2F00, 0x3000, 0x48000, 0x48800, 0x48C00, 0x48D00; the flags, not the
thermal config), "Dump SYSCON Error Log", optionally "Dump ERK". Encrypt (`age` or `gpg -c`), keep
three copies on different media, at least one away from the PC; never git, never LFS (per-console
keys and IDs); check each copy with `sha256sum`. Write the bank and the hashes into a private note
next to the dump. The region reformat's checklist (U1) needs the same dumps first.

### Fan: the floor, the panic path, the steps not run

Rank 26. local/0042's paths that weren't exercised on kernel .433.

**Why.** On .433 the temperatures read through hwmon, the fan went to SYSCON automatic at boot, and
full speed and back worked (`pwm1_enable` 0, then 2). Not done: step 1 (a `fan_policy=keep` boot that
only reads the policy; skipping it loses nothing), step 4 (full speed across `reboot` and
`poweroff`: the shutdown must hand the fan back to SYSCON), step 5 (a fixed floor, `echo 0x99 >
pwm1`, `pwm1_enable=1`, 60%, only with the user's explicit OK), step 6 (the panic path from full
speed, `echo c > /proc/sysrq-trigger`, only with the user's OK; then a hard power-off). The get
reply's bytes from the .433 boot are in its dmesg and were not copied: read them before relying on
`pwm1` in automatic mode. Two follow-ups are designed, not built: a game-mode hook
(`GAME_FAN_FLOOR` in `/etc/default/ps3-game-mode`, default empty until a measured game run shows a
floor helps) and lowering the vuart's 24-byte read trigger, so a short reply is not read one
request late (a patch of its own: it changes how every event is read).

- **Payoff:** the exit paths proven, and the floor available if a game needs it.
- **Effort:** S. **Risk:** low in the safe direction (full speed first, then back to auto); the panic
  test needs the power button.
- **Needs:** the user (agreeing, at the console). **Status:** waiting on decision D5; scheduled:
  TESTS.md "Next" (fan floor, with the user).
- **Source:** PLAN 4.4; [archive/THERMAL.md, "PS3 test plan for the fan"](../archive/THERMAL.md#ps3-test-plan-for-the-fan-local0042).

### Read-only probes of lv1 and the firmware state

Rank 41. Questions the running system can answer without writing anything.

**Why.** Is lv1 peek (hypercall 182) present at Linux time on Evilnat 4.92, and does the
`gpu-4kb-iopage` site already carry the patch? If peek is there and the patch isn't, poking it would
be a RAM-only change that lets ps3rsx map 4 KB pages into the GART instead of 1 MiB CMA chunks. The
RAM it could win is small (corrected 2026-10-18: there is no 64 MB GART reservation; the GART takes
1 MiB chunks on demand, 6-14 MB in games, and the area holds page cache between uses, so 4 KB IO pages
would save the rounding of partly used chunks and the CMA migrations, a few MB at most). The larger
part is the yes/no on lv1 patches at run time. Also open and read-only: which ROS bank is active
(xai_plugin, with the backups), what `rtc_diff` Petitboot hands our kernel
(`xxd /proc/device-tree/linux,rtc_diff`) and whether Petitboot's own kernel ever writes the OS area,
which "Rebug Tools" patches Evilnat's lv1 has baked in, and what `gpu-4kb-iopage` changes exactly.

- **Payoff:** low for RAM, useful as knowledge. **Effort:** S to probe.
- **Risk:** medium: a wrong poke hangs the console, so peek only; ask the user before any poke.
- **Needs:** the user's OK, PS3. **Status:** waiting on decision D8; never tried.
- **Source:** PLAN 4.3; [archive/FLASH-SYSCON.md, "7. Open questions"](../archive/FLASH-SYSCON.md#7-open-questions).

## Out of scope under the safety rules

Not ranked. CLAUDE.md's hard constraints rule these out (no permanent alterations beyond installing
Linux, nothing unrecoverable, no SYSCON writes beyond the fan policy, no fuse or flash patches,
hardware mods only if explicitly asked). They are listed so nobody re-proposes them by accident.

| Item | Why it stays out |
|---|---|
| Any NOR or CoreOS write: a PUP install, a custom `emer_init` for a region over 22 GiB (`addis r7,r9,-0x800` for 64 GiB, re-signing `emer_init.self`), a custom lv1 in CoreOS | a flash write; a broken `emer_init` breaks the Recovery Menu, our safety net; we have no hardware flasher (FLASH-SYSCON recovery class 2-3) |
| SYSCON writes other than the fan policy: a gentler fan curve in the thermal config, the 8th SPE (`0x48C30 = 0x07`), SYSCON firmware or patch-area writes (`firmud`, scuartd), product mode, QA or token edits | SYSCON writes are forbidden; the 8th SPE is also likely off because it is defective, and waits for recovery hardware and a measured need |
| lv1 pokes and hypervisor repository writes (the OtherOS++ patch 0170 wrote `ios.net.eurus.lpar` at every boot) | a persistent or console-hanging change; 0170, 0160 and 0316 were dropped by the user on 2026-10-18, as were `/dev/ps3sysmngr` (0060), the BD vendor commands (0090) and `PS3_DISP_MANAGER` |
| Writes to bootldr, eEID, metldr, cISD/cCSD or cvtrm; another console's NOR image or a "rebuilt" one; BadWDSD/qCFW on this model; eFuse or Cell-key experiments | never, whatever the payoff |
| Loading `ps3flash`, `ps3physmem` or `ps3_lv1call` write paths in a normal session | `PS3_FLASH`, `PS3_NOR_FLASH`, `REPOSITORY_WRITE` and `ENCDEC` stay built out; lv1call and the dispatcher manager stay off while ps3rsx runs |
| Recovery hardware: a NOR flasher (Teensy++ 2.0 with NORway, or an E3 clip) and a 3.3 V UART on the SYSCON pads | a hardware mod: only when the user asks (decision D11). It is the prerequisite for every flash or SYSCON write, and FLASH-SYSCON.md says to buy it and prove it by a read before any write is ever wanted |
| The power-control rig | a hardware mod, and a side project CLAUDE.md parks |
| Reading the SYSCON EEPROM from Linux (`ps3dm_scm /dev/ps3dmproxy`) | needs the dispatcher-manager patch and tools we don't build; xai_plugin reads the same from the XMB |

Older versions and the dated history: [archive/FLASH-SYSCON.md](../archive/FLASH-SYSCON.md),
[archive/THERMAL.md](../archive/THERMAL.md), [archive/OTHEROS-PATCHES.md](../archive/OTHEROS-PATCHES.md),
[archive/IDEAS.md](../archive/IDEAS.md).
