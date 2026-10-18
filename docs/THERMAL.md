# Temperatures and the fan

Research and design notes, 2026-10-18. Part A (reading the temperatures) is built: kernel patch
`linux/patches/local/0040-ps3-sys-manager-hwmon-temperatures.patch`, the bench harness, a raw
probe. Not booted on the PS3 yet. Part B (fan control) is research only: nothing here sends a
fan or SYSCON setting, and building it is the user's decision.

## Where the temperatures come from

- Every thermal sensor on the board hangs off SYSCON. The Cell BE and the RSX each have a
  diode read by a TMP411/ADT7461-class monitor on SYSCON's SMBus; SYSCON compares the readings
  with the "thermal config" in its EEPROM and sets the fan duty itself (psdevwiki
  [Thermal](#sources)).
- SYSCON numbers its sensors as thermal zones ("tzone"): 0x00 Cell BE, 0x01 RSX on every model;
  some boards add 0x02 XDR, 0x03 BE VR, 0x0a air intake, 0x0f GbE, 0x14 southbridge, 0x15 EE+GS.
  The later boards ("cookie new", "sherwood") list only 0, 1 and maybe 0x14. Ours is a CECH-20xx
  (DYN-001): expect 0 and 1.
- Temperatures are 8.8 fixed point everywhere: SYSCON's own console prints `0x5980` as 89.5 C.
- Guests never talk to SYSCON. They talk to the **System Manager (SM)**, which runs in process 9
  of the hypervisor's LPAR 1. It reaches the guest through **vuart port 2**, the port Linux's
  `drivers/ps3/ps3-sys-manager.c` already owns for the power button, thermal alert events and
  shutdown.
- GameOS reads them with lv2 syscalls that wrap SM services:

| lv2 syscall | SM service id | What |
|---|---|---|
| 383 `sys_game_get_temperature(tzone, *u32)` | 13 TEMPERATURE | one zone's temperature |
| 384 / 408 `sys_sm_get_tzpb` | 15 | thermal zone table (0x20 bytes) |
| 389 `sys_sm_set_fan_policy(id, policy, duty)` | 25 FAN_POLICY | set (part B) |
| 409 `sys_sm_get_fan_policy(id, *st, *policy, *mode, *duty)` | 50 FAN_POLICY | get |

  webMAN-MOD calls 383 and keeps `t >> 24` as whole degrees (the integer byte comes first), dev
  0 the CPU, 1 the RSX (`include/poll/fancontrol.h`).
- **Who may use which service:** the SM gives each guest type an ability bitmask. On a stock
  3.15 hypervisor the Linux LPAR's mask is 0x40012 (shutdown/reboot and boot-Linux only), GameOS
  has 0x3BF7EF, which includes fan policy (psdevwiki Hypervisor Reverse Engineering). TEMPERATURE
  is not in the gated list. OtherOS++ advertises "Linux with GameOS rights" and "full access to
  all System Manager services" (psdevwiki OtherOS++); sguerrini97 read the temperatures and set
  the fan policy from Linux under OtherOS++ in 2018. Whether **our** CFW's OtherOS++ (Evilnat
  4.92 Cobra, Petitboot from xai_plugin) grants the same is unverified: a refusal comes back as a
  REQUEST_ERROR (service 6) and the driver handles it (below).

## Message formats

All fields big-endian. Every SM message is a 16-byte header plus a payload:

| offset | size | field |
|---|---|---|
| 0 | 1 | version = 1 |
| 1 | 1 | header size = 16 |
| 2 | 2 | reserved |
| 4 | 4 | payload size |
| 8 | 2 | service id |
| 10 | 2 | reserved |
| 12 | 4 | request tag (0 in every message Linux and ps3vuart-tools send) |

**Temperature request** (service 13), 20 bytes on the wire, payload size **4**:

| payload offset | field |
|---|---|
| 0 | version = 1 |
| 1 | tzone |
| 2-3 | reserved, 0 |

The reference, `ps3sm get_temperature` (ps3vuart-tools, below), declares the request as a struct
of header + 3 bytes; the header's u32 fields pad it to 20, and the tool sends
`sizeof - 16` = 4 as the payload size and writes 20 bytes. The first draft of our patch sent 19
bytes with payload size 3; the review caught it. We send the 20 bytes the tool sends, with zeros
where the tool left stack garbage.

**Temperature reply**, payload of 8 bytes in the tool's struct:

| payload offset | field |
|---|---|
| 0 | version = 1 |
| 1 | tzone (the one asked for) |
| 2 | status, 0 = ok |
| 3 | reserved |
| 4 | integer degrees C |
| 5 | fraction, 1/256 degree (the tool prints `(b * 100) >> 8` as hundredths) |
| 6-7 | reserved |

Not documented anywhere: the reply's **service id** (13 or 14, by the SM's request/reply
pairing) and whether its payload is always 8 bytes (the tool reads whatever size the header
says). The driver takes service 13 or 14 with 1-16 payload bytes, matches on tzone, and logs the
first reply's service id, size and tag (`dmesg | grep "temperature reply"`), so the first boot
answers both.

**Fan policy set** (service 25 = 0x19), from ps3vuart-tools' `set_fan_policy`; documented here
for part B, **not implemented**: payload version 1, id (0), policy (0 full speed, 1 SYSCON
automatic, 2 manual), duty (0x33-0xFC accepted by the tool for manual), 1 reserved byte, padded
like the temperature request (24 bytes on the wire, payload size 8). The tool reads no reply.
Service 50 (0x32) is the get; its reply layout is known only from syscall 409's outputs (st,
policy, mode, duty).

## Earlier implementations

- **glevand's ps3vuart-tools** (2011-2012, gitorious, mirrored at
  github.com/sguerrini97/ps3linux-ps3vuart-tools): `ps3sm` sends raw SM messages through
  `/dev/ps3sysmngr`, the char device of glevand's `ps3sysmgr-char-device` kernel patch. Its own
  commands: get_inter_lpar_param, ring_buzzer, set_attr, ctl_led. **sguerrini97 added
  get_temperature (2018-05) and set_fan_policy (2018-12)** in that mirror; those are the only
  public Linux-side record of the message layout.
- **That char device** was `linux/patches/ps3/0060-ps3sysmgr-char-device.patch` (psx-place set),
  in our kernels up to 6.12.111+ps3.419. Opening `/dev/ps3sysmngr` cancels the kernel's async read
  of the port, so the power button and thermal alerts are not handled while it is open. The user
  dropped it on 2026-10-18 (docs/OTHEROS-PATCHES.md, decision 2: it can send any SM service), and
  local/0040 no longer refers to it. `ps3-sm-temp.sh --raw` still works on a kernel that has it
  (419), as an optional check before the new kernel (test steps below).
- No in-kernel temperature driver: mainline's `ps3-sys-manager.c` handles only events, next-op,
  attributes and the shutdown request, and the psx-place/Gentoo set we carry adds only the char
  device.
- In GameOS: webMAN-MOD and IRISMAN read 383 and drive 389 from their own control loops.

## The driver (local/0040)

`CONFIG_PS3_SYS_MANAGER_HWMON` (bool, needs `PS3_SYS_MANAGER` and `HWMON`; both on in
`linux/config/ps3.config`) adds to `ps3-sys-manager.c`:

- hwmon device `ps3_sm` with `temp1_*` (Cell BE) and `temp2_*` (RSX): `_input` (millidegrees),
  `_label`, `_alarm`. `ps3_sys_manager.temp_zones=0,1,0x14` adds zones (up to 8);
  `ps3_sys_manager.hwmon=0` turns it all off.
- **Polled on read:** nothing is sent at boot. Reading `temp*_input` sends one request and waits
  up to 1 s for the reply; the value is cached 1 s; one request at a time (a mutex). The existing
  async handler dispatches the reply, so the driver shares the port instead of opening a second
  reader.
- **Only the get request** is ever sent; the driver has no code that builds any other new
  message.
- **Gives up rather than insists:** a REQUEST_ERROR while a request is pending, or three failed
  requests in a row (timeouts, write errors), turns the sensors off for the boot (`-ENODATA`, one
  warning in dmesg). A reply with a non-zero status turns off that zone only.
- **Shutdown:** the driver's `.shutdown` (run by device_shutdown before power off, reboot, halt and
  kexec) takes the mutex and stops requests, so none is in flight when the final SET_NEXT_OP and
  REQUEST shutdown go out. The final power-off and restart paths set the stop flag too, without
  locks (the panic path skips device_shutdown). A temperature reply that arrives during
  `ps3_sys_manager_fin()`'s poll is handled and the poll goes on: since local/0041 it waits for
  the acked shutdown command only (below).
- **Alarms** follow the THERMAL_ALERT / THERMAL_CLEARED events the driver has always received
  (zone id = tzone), with no request; the "PS3 Thermal Alert Zone N" log line stays.

Changes to the existing paths (the review found that an unexpected reply, or a second sender,
could otherwise break the message stream and with it the power button and shutdown):

- `ps3_sys_manager_write()` sends header and payload in one `ps3_vuart_write()`, so a request
  from another CPU can't land between them.
- `send_attr`'s payload is 16 bytes, as its header always said; the write used to send 8 bytes of
  stack after the attribute (upstream does the same), now zeros.
- A header with a payload size other than 8 or 16 no longer `BUG()`s: it is logged and what is
  queued is dropped.
- Unhandled payloads and everything after a bad header are read out of the rx list.
  `ps3_vuart_clear_rx_bytes()` drains only the hypervisor's buffer; the payload, already queued by
  the interrupt handler, stayed to be parsed as the next header.
- Unbinding the driver through sysfs is off (`suppress_bind_attrs`; remove was never supported).

Review: an Opus review of the first draft found the 19/20-byte request, a request racing the
final shutdown messages, the undocumented reply size reaching the resync-less error path,
sleeping notify calls in the panic path, an unbind use-after-free and one bad zone switching off
all sensors; all fixed as above. Compile-checked with the cross compiler, W=1, option on and off.

Found on the way (upstream behaviour, fixed in local/0041, 2026-10-18): `handle_event()`
returns 0 for every event, and `ps3_sys_manager_fin()` polled until any handler returned 0, so a
power button or thermal event arriving between the final REQUEST shutdown and the SM's COMMAND
ended the poll early; the command was never acked and the console stayed at "System Halted, OK
to turn off power" until a hard power off. Upstream (torvalds master, Geoff Levand's
ps3-queue-v6.13, linuxppc-dev patchwork) has no fix. local/0041: `handle_cmd()` returns
`PS3_SM_MSG_SHUTDOWN_ACKED` once the ack is written and `fin()` waits for that only, handling
every other message meanwhile; without a command 5 s (timebase) after a request it sends the
request again, 3 in all, then keeps polling without new requests, so a late command is still
acked; it pauses only when nothing is queued (and spins 1 ms with interrupts off); button presses
during it are only logged (no `kill_cad_pid()`). An Opus review (2026-10-18) found no bug; on its
advice the first draft's halt after the third request and its `preempt_disable()` (no help with
PREEMPT_NONE, a sleep-in-atomic risk in `ps3_vuart_write()` on preemptible kernels) were dropped. The temperature reply handler returns 0 again (its -EAGAIN only kept the old
loop going). Still upstream behaviour: `final_power_off` doesn't send `set_attr(0)` first
(restart does), so events stay enabled there; the event and command handlers `BUG()` when a
payload isn't queued behind its header.

## The bench harness

`ps3-bench.sh` (ps3-gpu-tools), only when the `ps3_sm` hwmon exists:

- the sampler reads every `temp*_input` and `temp*_alarm` once a second into `$D/temps`
  (`# Cell BE,RSX`, then `time mC mC`, `-` when unreadable); the memory samples file is
  unchanged (report.py and step0-report.py parse its fixed columns);
- it stops the command (TERM, then KILL after 5 s, like the thermal alert watchdog) when a zone
  reaches `BENCH_TEMP_MAX` (degrees C, default **80**: below SYSCON's own shutdown at about
  85-90 and webMAN's 83 C warning, above what a supervised run with the fan on SYSCON or >= 60%
  should reach) or raises its alarm; `$D/TEMPLIMIT` says why;
- `bench.txt` gets `temp_max=cell:71.5 rsx:66.0` and `temp_stop=1`; `result` a "temps max" line;
- `games/bench/run.sh` stops the batch on a TEMPLIMIT like on a THERMAL alert;
- `report.py`: columns `tmax` (`71.5/66.0`, `!` when stopped), `tcell`, `trsx` (numbers, medians
  in the per-variant line). selftest.py checks both, and that runs without sensors get none.

Each temperature read is one SM request; at one read per zone per second that is two small
messages a second on a port that otherwise carries a few events per boot.

`ps3-sm-temp.sh` prints the temperatures from hwmon; `sudo ps3-sm-temp.sh --raw [ZONE...]` sends
the same request through `/dev/ps3sysmngr` and prints each reply in full (kernels that still have
the device only: 419 and older).

## PS3 test steps

Supervised session, fan on SYSCON auto (or >= 60%) in webMAN before setting the OtherOS flag, the
user not playing. Stop on any "PS3 Thermal Alert".

1. **Optional, before the new kernel, on 419 (the last kernel with `/dev/ps3sysmngr`):**
   `sudo ps3-sm-temp.sh --raw` (ps3-gpu-tools with the script deployed, or the script copied
   over). It sends only the temperature request, the bytes the driver sends; skip it if the raw
   device is not to be used at all. Expected: `zone 0x0: 4x.xx C, status 0
   [service 1x, tag 0x0, payload 8 bytes: ...]` for both zones. This shows the SM answers the Linux
   LPAR, the reply's service id and size, and that the values are sane, with no new kernel code.
   A REQUEST_ERROR line means our OtherOS++ doesn't grant the service: the driver would then
   switch itself off, and there is nothing more to test. Press nothing while it runs (1-2 s; the
   power button is not seen meanwhile).
2. Install the kernel next to the known-good one (as in the earlier test kernels:
   `PS3_DEFAULT=<old>` in /etc/default/ps3-boot, `make deploy KERNELS=1 PKGS="linux-image-ps3
   ps3rsx-modules-ps3"`), boot it by hand from Petitboot.
3. `dmesg | grep -i -e ps3_sys_manager -e "temperature" -e "bad header"`: expect "hwmon: 2
   temperature zones, read on demand"; no "bad header".
4. `ps3-sm-temp.sh` (or `sensors`, or `cat /sys/class/hwmon/hwmon*/temp*_input`): two values;
   then dmesg shows one "temperature reply: service id N, payload M, tag T" line. Compare with
   webMAN's figures from GameOS (same order of magnitude; idle ~45-55 C).
5. Power button: a short press must still shut down cleanly (systemd poweroff, then the PS3 off);
   that path got the single-write and stream changes. Then boot again and `sudo reboot` once
   (LPAR reboot path): it must come back to Petitboot/GameOS as before.
   **local/0041 (final shutdown poll)**, each from a booted test kernel, watching the console
   (and netconsole, if set up, for the last lines):
   - `sudo poweroff`: the PS3 must be off (standby, red LED) a few seconds after "System Halted,
     OK to turn off power"; no "no shutdown command after 5 s" line.
   - `sudo reboot`: back to Petitboot/GameOS as before, no "no shutdown command" line.
   - Power button during the shutdown: start `sudo poweroff`, then tap the power button
     (short presses, about one a second; never hold it) until the PS3 is off. It must still go
     off on its own; "PS3 power button: shutdown already under way" may show in the last lines.
     The old kernel (419) can hang at "System Halted" here (the bug), which needs a hard power off;
     run this one only on the new kernel.
   - If it ever sits at "System Halted" with the LED green: wait 15 s for the "no shutdown
     command after 5 s" lines and "after 3 requests, still waiting" (the SM didn't answer), note
     them, then power off by hand.
6. A bench run (`ps3-bench.sh run t -- sleep 30`): `temps` in the run directory, `temps max:` in
   the result.
7. Rollback as for every test kernel: the old kernel stays the default; `ps3_sys_manager.hwmon=0`
   on the command line turns the sensors off without another build.

## Part B: fan control (research only)

**What sets the fan.** SYSCON runs the fan from the thermal config's tables (temperature steps,
duties, hysteresis). The SM's FAN_POLICY service (25 set, 50 get), wrapped by lv2 syscalls
389/409, switches a fan controller between policy 0 (full speed), 1 (SYSCON automatic) and 2
(manual, with a duty). It corresponds to SYSCON's console commands `fanconpolicy get/set` and
the external `FAN SETPOLICY/GETPOLICY` (psdevwiki Syscon Firmware). webMAN-MOD calls
`sys_sm_set_fan_policy(0, 2, duty)` for manual and `(0, 1, 0)` to hand back to SYSCON; from
Linux the same is one SM message (format above).

**Runtime or persisted.** SYSCON keeps two copies: `fanconpolicy set` / `fantbl set` change the
copy "currently in RAM / in use", `getini/setini` the one stored in EEPROM (psdevwiki Syscon
Firmware); the EEPROM thermal config holds a policy byte and a ROM/RAM select per table
(Syscon Thermal Configs). Which one the SM service writes is **not documented**. The evidence
that it is the RAM copy is indirect:
- webMAN re-sets the duty every ~3 s in its loop; an EEPROM write each time would have worn the
  EEPROM out on many consoles, and nobody reports that;
- webMAN only restores SYSCON mode when fan control is turned off, on uninstall or on one quit
  URL, not on shutdown or reboot (its source), and the common experience is that a console
  without webMAN loaded runs on SYSCON again after a power cycle;
- one unverified user report says a manual speed survived a **standby** (SYSCON stays powered in
  standby), which fits a RAM setting that only a full power loss resets.

So: most likely runtime-only, lost when SYSCON loses power. Inferred, not documented: SYSCON
stays powered while the mains is on, so a standby, a reboot or a GameOS->Linux switch would not
reset it either.
That is why the CLAUDE.md rule matters: a manual duty webMAN set in GameOS stays in force under
Linux.

**Failure modes.**
- The controller dies (crash, kernel panic, LPAR destroyed) with policy 2 set: the last duty stays
  until something sets policy 1 or SYSCON restarts. No SYSCON watchdog for a dead guest is
  documented. A low manual duty then plus a hot game is the overheat case.
- A duty set too low: the tools refuse below 0x33 (20%); webMAN clamps to 0x33-0xF4.
- Garbage in the message (wrong padding or tag, as our first temperature draft had): unknown SM
  behaviour; for a set request that is the dangerous kind of unknown.
- Racing GameOS's settings across a reboot: whatever was set last stays.
- What stays independent of all this: SYSCON's thermal protection, the per-zone `trp` (trip)
  and `tshutdown` values with `tshutdown_time` in the thermal config (shutdown around 85-90 C).
  That it still applies in manual mode is very likely (SYSCON firmware, below any OS) but not
  documented.

**How SYSCON automatic mode comes back.** Policy 1 through the same service
(`sys_sm_set_fan_policy(0, 1, 0)`), or SYSCON losing power (mains off). Nothing in Linux needs to
do anything for it while Linux never sets policy 2.

**A safe design, if the user wants it** (not built):
1. Default: do nothing; the fan stays on SYSCON automatic, which is what the safety rules assume.
2. Only a floor: a userspace or kernel controller may raise the duty above what SYSCON would
   pick, never below SYSCON's own curve: set manual only when the measured temperature is above a
   threshold (e.g. 70 C), with a duty from a table that never goes under 0x4D (30%), and hand
   back to SYSCON (policy 1) as soon as it falls below the threshold minus hysteresis.
3. Watchdog fallback: policy 1 on every exit path (service stop, `.shutdown` of the driver,
   reboot notifier, panic notifier), and a deadman: the kernel side re-checks every few seconds
   that its controller is alive and otherwise sends policy 1 itself. Since SYSCON keeps a manual
   duty when the guest dies, policy 1 must be sent before anything that can end the guest
   (including kexec from Petitboot).
4. Never manual below 60% duty while the temperature sensors are unreadable; if the sensors fail,
   policy 1.
5. Every set request byte-for-byte equal to ps3vuart-tools' (24 bytes, payload size 8), tested
   first with policy 1 (a no-op on a console already in SYSCON mode), with webMAN in SYSCON mode
   before booting, supervised, the reading of `sys_sm_get_fan_policy` (service 50) checked first.

This needs a SYSCON-facing set request, which the safety rules ("no syscon writes") rule out until
the user decides. A RAM-only policy change is arguably not a "write" in the flash/EEPROM sense,
but that is exactly the kind of judgement the rules leave to the user.

## Sources

- psdevwiki, read through the Wayback Machine (the live site is behind a bot check):
  [LV2 Functions and Syscalls](https://www.psdevwiki.com/ps3/LV2_Functions_and_Syscalls) (syscalls
  383, 384, 389, 409 and their SM service ids; the 383 conversion and tzone list),
  [Hypervisor Reverse Engineering](https://www.psdevwiki.com/ps3/Hypervisor_Reverse_Engineering)
  ("Guest OS VUART 2 (System Manager)", the 62 service ids, the ability bitmasks),
  [Syscon Firmware](https://www.psdevwiki.com/ps3/Syscon_Firmware) (tmp, tzone lists per board,
  fanconpolicy get/set/getini/setini, fantbl RAM vs EEPROM, trp, tshutdown, FAN SETPOLICY),
  [Syscon Thermal Configs](https://www.psdevwiki.com/ps3/Syscon_Thermal_Configs) (the EEPROM thermal
  config, policy and ROM/RAM select bytes),
  [Thermal](https://www.psdevwiki.com/ps3/Thermal) (sensors, temperature monitors, IRISMAN
  defaults), [OtherOS++](https://www.psdevwiki.com/ps3/OtherOS%2B%2B) (features: SM access).
- ps3vuart-tools: [sguerrini97/ps3linux-ps3vuart-tools](https://github.com/sguerrini97/ps3linux-ps3vuart-tools)
  (`ps3sm/ps3sm_msg.h`, `ps3sm/cmd/cmd_get_temperature.c`, `cmd_set_fan_policy.c`, `ps3sm/dev.c`;
  commits 74f3fec, 26927cc, 85526f2, 4ffabb3, af8af4c); glevand's original command list on
  [ps3devwiki Ps3vuart-tools (2012)](http://ps3devwiki.com/wiki/Ps3vuart-tools).
- [webMAN-MOD](https://github.com/aldostools/webMAN-MOD): `include/poll/fancontrol.h` (syscalls,
  modes, 0x33-0xF4 limits, restore_fan), `include/poll/fancontrol2.h` (the control loop),
  `include/cmd/quit.h`, `include/setup.h`.
- [IRISMAN](https://github.com/aldostools/IRISMAN): `source/controlfan.c`, `sm_monitor/main.c`
  (speed and temperature tables).
- Linux: `drivers/ps3/ps3-sys-manager.c`, `drivers/ps3/ps3-vuart.c` (6.12.111 with our series).
