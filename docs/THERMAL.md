# Temperatures and the fan

This is how Linux on our PS3 reads its temperatures and how it treats the fan. Both are built into the kernel as patches `local/0040` (hwmon temperatures), `local/0041` (a shutdown fix) and `local/0042` (fan policy). Kernel 6.12.111+ps3.433 booted with all three and the temperatures, SYSCON automatic at boot, and full speed and back all worked. A fixed floor, the panic path and the power-off paths of 0041 are not tested yet: the steps are in [PS3 checks still to run](#ps3-checks-still-to-run).

Terms such as SYSCON, System Manager and LPAR are in the [glossary](README.md#glossary). Safety rules for the fan are in [CLAUDE.md](../CLAUDE.md); this page does not change them.

## Where the temperatures come from

Every sensor on the board hangs off SYSCON, the console's service processor. The Cell and the RSX each have a diode read by a TMP411/ADT7461-class monitor on SYSCON's SMBus. SYSCON compares the readings with the thermal config in its EEPROM and sets the fan duty on its own.

SYSCON numbers its sensors as thermal zones ("tzone"). Zone 0 is the Cell BE and zone 1 the RSX on every model. Some boards add 0x02 XDR, 0x03 BE VR, 0x0a air intake, 0x0f GbE, 0x14 southbridge and 0x15 EE+GS. The later boards list only 0, 1 and maybe 0x14. Ours is a CECH-20xx (DYN-001), so we expect 0 and 1, and that is what we get. Temperatures are 8.8 fixed point: SYSCON's console prints `0x5980` as 89.5 C.

A guest OS never talks to SYSCON. It talks to the System Manager (SM), which runs in process 9 of the hypervisor's LPAR 1 and reaches Linux through vuart port 2. Linux's `drivers/ps3/ps3-sys-manager.c` already owns that port for the power button, thermal alert events and shutdown.

GameOS reads the same data with lv2 syscalls that wrap SM services:

| lv2 syscall | SM service id | What |
|---|---|---|
| 383 `sys_game_get_temperature(tzone, *u32)` | 13 TEMPERATURE | one zone's temperature |
| 384 / 408 `sys_sm_get_tzpb` | 15 | thermal zone table (0x20 bytes) |
| 389 `sys_sm_set_fan_policy(id, policy, duty)` | 25 FAN_POLICY | set |
| 409 `sys_sm_get_fan_policy(id, *st, *policy, *mode, *duty)` | 50 FAN_POLICY | get |

webMAN-MOD calls 383 and keeps `t >> 24` as whole degrees (device 0 the CPU, 1 the RSX).

The SM gives each guest type an ability bitmask. On a stock 3.15 hypervisor the Linux LPAR's mask is 0x40012 (shutdown, reboot, boot Linux), GameOS has 0x3BF7EF, which includes fan policy. TEMPERATURE is not in the gated list. OtherOS++ advertises "Linux with GameOS rights", and sguerrini97 read temperatures and set the fan policy from Linux under OtherOS++ in 2018. On our CFW (Evilnat 4.92 Cobra, Petitboot from xai_plugin) this is now measured: the SM answers the Linux LPAR, both for temperatures and for the fan policy (see [Confirmed on the PS3](#confirmed-on-the-ps3)).

## Message formats

All fields are big-endian. Every SM message is a 16-byte header plus a payload:

| offset | size | field |
|---|---|---|
| 0 | 1 | version = 1 |
| 1 | 1 | header size = 16 |
| 2 | 2 | reserved |
| 4 | 4 | payload size |
| 8 | 2 | service id |
| 10 | 2 | reserved |
| 12 | 4 | request tag (0 in every message Linux and ps3vuart-tools send) |

**Temperature request** (service 13): 20 bytes on the wire, payload size 4. Payload byte 0 is the version (1), byte 1 the tzone, bytes 2-3 zero. The reference is `ps3sm get_temperature` from ps3vuart-tools. It declares the request as a struct of header plus 3 bytes, which pads to 20, and it sends `sizeof - 16` = 4 as the payload size. Our first draft sent 19 bytes with payload size 3 and the review caught it. We send the 20 bytes the tool sends, with zeros where the tool left stack garbage.

**Temperature reply**: a payload of 8 bytes. Byte 0 version (1), byte 1 tzone, byte 2 status (0 = ok), byte 3 reserved, byte 4 integer degrees C, byte 5 the fraction in 1/256 degree, bytes 6-7 reserved. The reply comes back as service id 14 to a request with id 13, with tag 0 and 8 payload bytes. The driver accepts service 13 or 14 with 1-16 payload bytes, matches on tzone and logs the first reply's service id, size and tag (`dmesg | grep "temperature reply"`).

**Fan policy set** (service 25 = 0x19), from ps3vuart-tools' `set_fan_policy` and used by `local/0042`: payload version 1, id 0, policy (0 full speed, 1 SYSCON automatic, 2 manual), duty, one reserved byte, padded like the temperature request. That is 24 bytes on the wire, payload size 8. The tool reads no reply.

**Fan policy get** (service 50 = 0x32): 20 bytes, payload size 4 (version 1, id 0, two zero bytes). No public source documents these bytes. We laid the request out like the temperature request. The reply's layout is known only from syscall 409's outputs (status, policy, "mode", duty); [The fan driver](#the-fan-driver-local0042) says what the driver assumes.

## The hwmon driver (local/0040)

`CONFIG_PS3_SYS_MANAGER_HWMON` (bool; needs `PS3_SYS_MANAGER` and `HWMON`, both on in `linux/config/ps3.config`) adds to `ps3-sys-manager.c` a hwmon device `ps3_sm` with `temp1_*` (Cell BE) and `temp2_*` (RSX). Each has `_input` (millidegrees), `_label` and `_alarm`. The module parameter `ps3_sys_manager.temp_zones=0,1,0x14` adds zones (up to 8) and `ps3_sys_manager.hwmon=0` turns the temperatures off.

How it behaves:

- **Polled on read.** Nothing is sent at boot. Reading `temp*_input` sends one request and waits up to 1 s for the reply. The value is cached for 1 s and one request runs at a time (a mutex). The existing async handler dispatches the reply, so the driver shares the port instead of opening a second reader.
- **Gives up rather than insists.** A REQUEST_ERROR while a request is pending, or three failed requests in a row, turns the sensors off for that boot (`-ENODATA`, one warning in dmesg). A reply with a non-zero status turns off that zone only.
- **Shutdown.** The driver's `.shutdown` (run before power off, reboot, halt and kexec) takes the mutex and stops requests, so none is in flight when the final SET_NEXT_OP and shutdown request go out. The final power-off and restart paths set the stop flag too, without locks, because the panic path skips device_shutdown.
- **Alarms** follow the THERMAL_ALERT and THERMAL_CLEARED events the driver always received (zone id = tzone). They need no request. The "PS3 Thermal Alert Zone N" log line stays.

The patch also hardened the existing message paths, because an unexpected reply or a second sender could otherwise break the stream and with it the power button and shutdown:

- `ps3_sys_manager_write()` sends header and payload in one `ps3_vuart_write()`, so a request from another CPU cannot land between them.
- `send_attr`'s payload is 16 bytes, as its header always said. The write used to send 8 bytes of stack after the attribute (upstream does the same), now zeros.
- A header with a payload size other than 8 or 16 no longer calls `BUG()`: it is logged and what is queued is dropped.
- Unhandled payloads and everything after a bad header are read out of the rx list. `ps3_vuart_clear_rx_bytes()` drains only the hypervisor's buffer, and the already queued payload used to be parsed as the next header.
- Unbinding the driver through sysfs is off (`suppress_bind_attrs`; remove was never supported).

An Opus review of the first draft found the 19/20-byte request, a request racing the final shutdown messages, the undocumented reply size reaching the resync-less error path, sleeping notify calls in the panic path, an unbind use-after-free and one bad zone switching off all sensors. All are fixed. The patch compiles with the cross compiler at W=1, option on and off.

### The shutdown fix (local/0041)

Found on the way, in upstream behaviour: `handle_event()` returns 0 for every event, and `ps3_sys_manager_fin()` polled until any handler returned 0. A power button or thermal event between the final shutdown request and the SM's COMMAND ended the poll early, the command was never acked, and the console stayed at "System Halted, OK to turn off power" until a hard power off. Upstream (torvalds master, Geoff Levand's `ps3-queue-v6.13`, linuxppc-dev patchwork) has no fix.

`local/0041`: `handle_cmd()` returns `PS3_SM_MSG_SHUTDOWN_ACKED` once the ack is written, and `fin()` waits for that only, handling every other message meanwhile. Without a command 5 s (timebase) after a request it sends the request again, three in all, then keeps polling without new requests, so a late command is still acked. It pauses only when nothing is queued (a 1 ms spin with interrupts off), and button presses during it are only logged (no `kill_cad_pid()`). A temperature reply that arrives in this poll is handled and the poll goes on. An Opus review found no bug. On its advice the first draft's halt after the third request and its `preempt_disable()` were dropped (no help with PREEMPT_NONE, and a sleep-in-atomic risk in `ps3_vuart_write()` on preemptible kernels).

Still upstream behaviour: `final_power_off` does not send `set_attr(0)` first (restart does), so events stay enabled there; the event and command handlers call `BUG()` when a payload is not queued behind its header.

## The bench's temperature limit

`ps3-bench.sh` (in ps3-gpu-tools) watches temperatures whenever the `ps3_sm` hwmon exists:

- The sampler reads every `temp*_input` and `temp*_alarm` once a second into `$D/temps` (`# Cell BE,RSX`, then `time mC mC`, with `-` when unreadable). The memory samples file is unchanged, since `report.py` and `step0-report.py` parse its fixed columns.
- It stops the command (TERM, then KILL after 5 s, like the thermal alert watchdog) when a zone reaches `BENCH_TEMP_MAX` or raises its alarm. `BENCH_TEMP_MAX` is in degrees C and defaults to **80**: below SYSCON's own shutdown at about 85-90 C and webMAN's 83 C warning, above what a supervised run with the fan on SYSCON or at 60% or more should reach. `$D/TEMPLIMIT` says why it stopped.
- `bench.txt` gets `temp_max=cell:71.5 rsx:66.0` and `temp_stop=1`, and `result` gets a "temps max" line.
- `games/bench/run.sh` stops the whole batch on a TEMPLIMIT, as it does on a thermal alert.
- `report.py` has columns `tmax` (`71.5/66.0`, `!` when stopped), `tcell` and `trsx` (numbers, with medians in the per-variant line). `selftest.py` checks them, and that runs without sensors get none.

Each temperature read is one SM request. At one read per zone per second that is two small messages a second on a port that otherwise carries a few events per boot.

`ps3-sm-temp.sh` prints the temperatures from hwmon. Its `--raw` mode used `/dev/ps3sysmngr`, which our kernels no longer have (see [History](#history)).

## The fan driver (local/0042)

CLAUDE.md allows the fan policy from Linux with these limits: only through the System Manager's fan-policy service, runtime only; SYSCON automatic at boot and on every exit path; manual only as a fixed floor at or above a safe duty, with no control loop that needs the PPE to react in time; other SYSCON writes stay forbidden. `local/0042` builds exactly that, under the same option (`CONFIG_PS3_SYS_MANAGER_HWMON`), on the same hwmon device and the same one-request-at-a-time machinery as the temperatures.

**What is sent.** Two new messages, both of the FAN_POLICY service. They change SYSCON's runtime policy and not its EEPROM, so the fan tables stay as they are. The set is 24 bytes, byte for byte what ps3vuart-tools sends (it matches syscall 389 and webMAN's `(0, 2, duty)` and `(0, 1, 0)`). The get is 20 bytes with no public source (see above). Policy 0 is full speed (duty 0), 1 SYSCON automatic (duty 0), 2 manual with a duty.

The driver takes the get reply, tentatively, as version, id, status, policy, speed, duty. An old ps3sm-utils run printed `0x01 0x01 0x48 0x00` for it. The measured temperature reply has a reserved byte after the status, which hints that the policy might sit at offset 4 instead. The driver therefore accepts service ids 25/26 (set) and 51/50 (get) with 0-16 payload bytes, logs them raw, and logs every REQUEST_ERROR's payload. A set waits up to 1 s for a REQUEST_ERROR or a reply, a get up to 1 s for its reply.

**At boot** a work item runs 0.5 s after probe, so the probe itself does not wait:

1. get, logged raw (`fan: get reply: ...`) and parsed (`fan: at boot (layout unconfirmed): ...`). This shows what GameOS or webMAN left.
2. set SYSCON automatic, whatever was left, unless `fan_policy=keep`.
3. get again. If it reads version 1, id 0, policy 1 with at least 8 payload bytes, the reply layout counts as confirmed and later gets are used (pwm1 in automatic mode, the check after each set). Otherwise policy reads are only logged.
4. then `fan_policy=full` sets full speed, and `fan_floor=` a manual duty.

**Exit paths.** The fan goes back to SYSCON automatic whenever this driver set anything else:

- `.shutdown` (power off, reboot, halt, kexec) cancels the boot and alert works, sends automatic under the request mutex and stops all requests, before the final SET_NEXT_OP and shutdown request.
- The final power-off and restart routines (after device_shutdown normally; without it after an emergency restart such as SysRq-b) send it without locks.
- A panic notifier (priority INT_MAX), also lockless. On the PS3 `ps3_panic()` is itself a panic notifier (INT_MIN) that never returns, so whatever `panic_timeout` says, this notifier is the only restore after a crash. A kdump kernel would set automatic itself when it probes; we do not use kdump.
- `fan_dirty` starts up (except with `keep`): an exit before the boot's automatic went out, or after it failed, still sends automatic.

The lockless sends use `ps3_vuart_write_atomic()` (new, in `ps3-vuart.c`): a trylock on the tx lock, nothing queued before it, no allocation. A partial write is finished in a bounded spin, since half a message would desync the System Manager's stream and with it the final shutdown. It retries for 10 ms when the lock is held or a write is queued, then gives up with a "not sent" line. `.shutdown` waits at most 3 s for the request mutex (a task that died holding it) and then uses the lockless send. `fan_dirty` goes up before a full or manual set is written and down only after automatic was written. `fan_final` is set before an exit path looks at it, and a full or manual set refuses once it is up, so an exit path never misses a setting.

**Errors fall back to SYSCON.** A full or manual set that fails to go out, or that the confirmed get reads back wrong, is followed by automatic. A REQUEST_ERROR to a set (the SM refusing fan rights to this LPAR) leaves only automatic allowed for the rest of the boot (full and manual writes return `-EIO`). Automatic is still sent on the exit paths, since an earlier setting may have taken. If automatic itself is refused, the exit paths stop sending it. The temperatures keep working. A REQUEST_ERROR or three unanswered gets turn off only the reads (the get's bytes are a guess; the set's are not). A thermal alert event hands a manual duty back to SYSCON, and manual is refused (`-EBUSY`) until every zone has cleared. That is an event, not a loop: SYSCON sends it at its own trip points. Every set checks the duty range again just before it is written.

**The safe minimum duty is 0x99 (60%).** A manual duty is fixed: SYSCON's curve is off while it is in force and nothing follows the load. So it has to be safe for a game at full load, not just above the idle point of the curve. 60% is the fixed speed CLAUDE.md already accepts for a Linux session, and about twice what SYSCON's own curve on a CECH-20xx reaches up to about 70 C (0x33-0x4a, 20-29%; psdevwiki Thermal, measured with IRISMAN). The upper clamp, 0xf4 (95%), is webMAN's; full speed is policy 0. Every duty written (module parameter or sysfs) is clamped into 0x99-0xf4 and the clamp is logged. SYSCON's "minimum" fan mode (fanconmode 3, possibly a true floor under the curve) is not reachable through this service and is not used.

**Module parameters** (`ps3_sys_manager.` on the kernel command line):

| parameter | values | default |
|---|---|---|
| `fan_policy` | `auto` (SYSCON automatic at boot), `keep` (log the get, set nothing at boot), `full`, `off` (no fan requests at all, no pwm1) | `auto` |
| `fan_floor` | a duty, raised to 0x99, lowered to 0xf4; sets manual at boot (after automatic); ignored with `full` and `off` | 0 (none) |
| `hwmon` | 0 turns the temperature channels off; pwm1 stays unless `fan_policy=off` | 1 |

**sysfs** (`/sys/class/hwmon/hwmonN/`, the one whose `name` is `ps3_sm`; root writes):

| file | read | write |
|---|---|---|
| `pwm1_enable` | 0 full speed, 1 manual, 2 SYSCON automatic (what this driver set; after `keep`, the confirmed get, else `-ENODATA`) | 0, 1 (manual at the stored duty), 2 |
| `pwm1` | manual: the duty; full: 255; automatic: SYSCON's current speed from the get, only once the layout is confirmed (cached 1 s), else `-ENODATA` | a duty 0-255, clamped to 0x99-0xf4 (logged); sent at once in manual mode, otherwise stored for `pwm1_enable=1` |

A set is logged (`fan: SYSCON automatic`, `fan: full speed`, `fan: manual, duty 0x99 (60%)`) only when it changes something, so a script writing the same value does not fill the log.

**Review.** An independent Opus review (races with the message loop, shutdown ordering, panic path, unbind, wire bytes, hwmon ABI) confirmed the set's bytes against the tool, that nothing else is sent, the clamp on every path, the `.shutdown` ordering and the `fan_dirty`/`fan_final` handshake. It found one blocker, now fixed: a REQUEST_ERROR used to turn off every send, the exit paths' automatic included, so a floor accepted earlier would have stayed after Linux exited. Fixed from the same review: `fan_dirty` starting up; the set waiting 1 s like the others (a late REQUEST_ERROR was charged to the next request); finishing partial atomic writes; accepting 0-byte replies instead of draining the queue; logging REQUEST_ERROR payloads; scheduling the alert regardless of a set in flight and refusing manual during an alert; a bounded mutex wait at shutdown; the duty range checked at the wire; quieter logs. Service 26 (REQUEST_ERROR_LOG) was dropped as a "set reply" and put back after the PS3 showed a 13 answered by a 14. Left as is: the get stays before the boot's automatic (the first test boot uses `keep`, so the guessed get is tried alone first). The patch compiles with the cross compiler at W=1, option on and off.

**Known limits.**

- The vuart's async read wakes the driver at 24 bytes (upstream `PS3_SM_RX_MSG_LEN_MIN`). A shorter reply (a 4-byte get reply would be 20 bytes) sits in the receive list until the next message arrives, and is then taken for the request pending at that time, so it reads one request late. The layout is confirmed only with 8 payload bytes or more for that reason. With a short reply the reads stay unused and the log shows the late replies raw. The fix would lower the trigger to the header size, which changes how every event is read: a patch of its own.
- The stock Linux LPAR's ability mask (0x40012) lacks bit 9, which gates both service 25 and 50. Our OtherOS++ grants fan policy (the full-speed test passed), but a different firmware might not.
- A sysfs write that sends manual on one CPU at the very moment another panics can still land after the panic's automatic.

## Fan control research

This section is the research that led to the design above.

**What sets the fan.** SYSCON runs the fan from the thermal config's tables (temperature steps, duties, hysteresis). The SM's FAN_POLICY service (25 set, 50 get), wrapped by lv2 syscalls 389 and 409, switches a fan controller between policy 0 (full speed), 1 (SYSCON automatic) and 2 (manual, with a duty). It corresponds to SYSCON's console commands `fanconpolicy get/set` and the external `FAN SETPOLICY/GETPOLICY` (psdevwiki Syscon Firmware). webMAN-MOD calls `sys_sm_set_fan_policy(0, 2, duty)` for manual and `(0, 1, 0)` to hand back to SYSCON.

**Runtime or persisted.** SYSCON keeps two copies: `fanconpolicy set` and `fantbl set` change the copy "currently in RAM / in use", `getini/setini` the one stored in the EEPROM. Which one the SM service writes is not documented. The evidence that it is the RAM copy is indirect:

- webMAN re-sets the duty about every 3 s in its loop. An EEPROM write each time would have worn the EEPROM out on many consoles, and nobody reports that.
- webMAN restores SYSCON mode only when fan control is turned off, on uninstall or on one quit URL, not on shutdown or reboot, and the common experience is that a console without webMAN loaded runs on SYSCON again after a power cycle.
- One unverified user report says a manual speed survived a standby (SYSCON stays powered in standby), which fits a RAM setting that only a full power loss resets.

So the setting is most likely runtime-only, lost when SYSCON loses power. We infer, without documentation, that SYSCON stays powered while the mains is on, so a standby, a reboot or a GameOS-to-Linux switch would not reset it either. That is why CLAUDE.md requires webMAN's fan to be in SYSCON mode or at 60% or more before Linux starts: a manual duty webMAN set in GameOS stays in force under Linux.

**Failure modes.**

- The controller dies (crash, kernel panic, LPAR destroyed) with policy 2 set: the last duty stays until something sets policy 1 or SYSCON restarts. No SYSCON watchdog for a dead guest is documented. A low manual duty plus a hot game is the overheat case.
- A duty set too low: the tools refuse below 0x33 (20%) and webMAN clamps to 0x33-0xF4.
- Garbage in the message (wrong padding or tag, as our first temperature draft had): unknown SM behaviour, and for a set request that is the dangerous kind of unknown.
- SYSCON's own thermal protection stays independent of all this: the per-zone `trp` (trip) and `tshutdown` values with `tshutdown_time` in the thermal config (shutdown around 85-90 C). That it still applies in manual mode is very likely, being SYSCON firmware below any OS, but not documented.

**How SYSCON automatic comes back:** policy 1 through the same service, or SYSCON losing power.

**The research's proposal** was more elaborate than what we built: a floor applied only above a threshold (say 70 C) with hysteresis, a watchdog fallback with a deadman, and no manual duty below 60% while sensors are unreadable. The user's decision of 2026-10-18 chose the simpler form. A fixed floor needs no control loop, so no deadman is needed: nothing depends on Linux reacting in time.

**Not built:** a hook in game mode (`ps3-game-mode on` setting a floor through pwm1, `thaw` handing back to SYSCON, a setting such as `GAME_FAN_FLOOR=0xb3`). The default would stay empty until a measured game run shows a floor helps. See [backlog/platform.md](backlog/platform.md).

## Confirmed on the PS3

- **Kernel 6.12.111+ps3.178, 2026-10-18** (OtherOS++ on Evilnat 4.92), after a long benchmark session, through the raw device that kernel still had: the SM answers the Linux LPAR. Zone 0 (Cell) 65.76 C and zone 1 (RSX) 60.00 C, status 0, reply service id 14, tag 0, payload 8 bytes (`0100000041c30000` and `010100003c000000`, i.e. version, zone, status, 0, integer C, fraction/256, 0, 0). The hwmon driver's accepted formats cover this.
- **Kernel 6.12.111+ps3.433, 2026-10-18 evening** (`local/0040`, `0041`, `0042` and the rest of the series): the `ps3_sm` hwmon temperatures read, the fan went to SYSCON automatic at boot, and full speed through `pwm1_enable=0` and back to 2 worked (the full-speed test at the end of "PS3 checks still to run").

Not confirmed:

- The get reply's exact bytes in that boot, so whether the layout guess was confirmed. They are in that boot's dmesg and were not copied; read them before relying on `pwm1` in automatic mode.
- A fixed floor (`pwm1_enable=1`), the panic path, and full speed across a reboot or poweroff.
- `local/0041`'s final-shutdown paths: `poweroff`, `reboot`, the power button during a shutdown.
- Whether `fan_policy=keep` was ever used for a first boot. If it was skipped nothing is lost, since the default sends only the automatic policy.
- Whether a floor would help a game (nothing measured it).

## PS3 checks still to run

All of these need a supervised session: the user at the console and agreeing, webMAN's fan in SYSCON mode (or fixed at 60% or more) before the OtherOS flag is set, the user not playing, nothing else running. Stop on any "PS3 Thermal Alert". A test kernel goes in next to the known-good one (`PS3_DEFAULT=<old>` in `/etc/default/ps3-boot`, then `make deploy KERNELS=1 PKGS="linux-image-ps3 ps3rsx-modules-ps3"`) and is booted by hand from Petitboot. See [CONSOLE.md](CONSOLE.md) for how a session works.

**Temperatures and the power paths**

1. Check the boot: `dmesg | grep -i -e ps3_sys_manager -e "temperature" -e "bad header"`. Expect "hwmon: 2 temperature zones, read on demand" and no "bad header".
2. `ps3-sm-temp.sh` (or `sensors`, or `cat /sys/class/hwmon/hwmon*/temp*_input`) gives two values, and dmesg shows one "temperature reply: service id N, payload M, tag T" line. Compare with webMAN's figures from GameOS (same order of magnitude; idle about 45-55 C).
3. Power button: a short press must still shut down cleanly (systemd poweroff, then the PS3 off). Then boot again and `sudo reboot` once (LPAR reboot path): it must come back to Petitboot/GameOS as before.
4. `local/0041`, each from a booted test kernel, watching the console (and netconsole, if set up, for the last lines):
   - `sudo poweroff`: the PS3 must be off (standby, red LED) a few seconds after "System Halted, OK to turn off power", with no "no shutdown command after 5 s" line.
   - `sudo reboot`: back to Petitboot/GameOS as before, no "no shutdown command" line.
   - Power button during the shutdown: start `sudo poweroff`, then tap the power button (short presses, about one a second, never hold it) until the PS3 is off. It must still go off on its own; "PS3 power button: shutdown already under way" may show in the last lines. The old kernel (419) can hang at "System Halted" here (the bug), which needs a hard power off, so run this one only on a new kernel.
   - If it ever sits at "System Halted" with the LED green: wait 15 s for the "no shutdown command after 5 s" lines and "after 3 requests, still waiting" (the SM did not answer), note them, then power off by hand.
5. A bench run (`ps3-bench.sh run t -- sleep 30`) leaves `temps` in the run directory and `temps max:` in the result.

**Fan**

6. **Read the policy first (optional).** Boot with `ps3_sys_manager.fan_policy=keep`, so only the get is sent. `dmesg | grep -e 'fan:' -e REQUEST_ERROR` should show `fan: policy control on, at boot keep`, one `fan: get reply: service id N, payload M, tag T: ...` line (or "no reply" or "refused"), and `fan: at boot (layout unconfirmed): ... policy 1 (SYSCON automatic)` if the guess is right and webMAN was in SYSCON mode. A late reply shows as `fan: reply no request waits for`. Note the bytes: they settle the get's layout. A "refused" get means no fan rights or a wrong guess; the set still decides.
7. **Full speed across a reboot.** `h=$(dirname $(grep -l ps3_sm /sys/class/hwmon/hwmon*/name))`, `echo 0 | sudo tee $h/pwm1_enable`, then `sudo reboot`. The fan must go back before the LPAR ends (`fan: SYSCON automatic` in the shutdown messages, netconsole if set up), and be SYSCON automatic in GameOS/webMAN or after the next Linux boot's get. Repeat with `sudo poweroff`.
8. **A floor, only with the user's explicit OK.** `echo 0x99 > $h/pwm1` (stored), then `echo 1 > $h/pwm1_enable`. Expect `fan: manual, duty 0x99 (60%)`, the fan steadier and louder than at idle, and `cat $h/pwm1` reading 153. Then `echo 2 > $h/pwm1_enable`. A value below 0x99 must log the clamp and still read 153. Watch the temperatures (`ps3-sm-temp.sh`) and stop above 75 C.
9. **Panic path, optional, only with the user's OK.** With full speed set (`pwm1_enable=0`), `echo c | sudo tee /proc/sysrq-trigger` crashes the kernel on purpose. The console must show `PS3 fan: SYSCON automatic (panic)` and the fan must spin down within about 2 min; then power off by hand (the panic halts). Do this from full speed, never from a floor.

For reference, step 3's full-speed test (done on .433): `echo 0 | sudo tee $h/pwm1_enable` makes the fan audibly go to full speed within a few seconds (`tools/webcam-audio.sh` from the host helps if nobody is at the TV), `pwm1_enable` reads 0, and `echo 2 | sudo tee $h/pwm1_enable` lets it spin down within SYSCON's roughly 2 min step-down, with `fan: full speed`, `fan: SYSCON automatic` and no "back to SYSCON" warnings.

**Rollback** without a rebuild: `ps3_sys_manager.fan_policy=off` sends no fan requests at all, and `ps3_sys_manager.hwmon=0` turns the temperature channels off. The old kernel stays the default.

## History

- **Earlier implementations (all outside our tree).** glevand's ps3vuart-tools (2011-2012; mirrored at github.com/sguerrini97/ps3linux-ps3vuart-tools) has `ps3sm`, which sends raw SM messages through `/dev/ps3sysmngr`, the char device of glevand's `ps3sysmgr-char-device` patch. sguerrini97 added `get_temperature` (2018-05) and `set_fan_policy` (2018-12) there, and those are the only public Linux-side record of the message layouts. Mainline's `ps3-sys-manager.c` handles only events, next-op, attributes and the shutdown request, and has no temperature or fan driver. In GameOS, webMAN-MOD and IRISMAN read syscall 383 and drive 389 from their own control loops.
- **`/dev/ps3sysmngr` dropped, 2026-10-18.** The char device was `linux/patches/ps3/0060-ps3sysmgr-char-device.patch`, in our kernels up to 6.12.111+ps3.419. While it is open the kernel's async read of the port is cancelled, so the power button and thermal alerts are not handled, and it can send any SM service. The user dropped it ([OTHEROS-PATCHES.md](OTHEROS-PATCHES.md), decision 2). `local/0040` had used its `struct ps3sm` only to return `-EBUSY` while the device was open; that and the misc device are gone. `ps3-sm-temp.sh --raw`, which sent the temperature request through the device, works only on a kernel that still has it (419 and older).
- **First draft of the temperature request, 2026-10-18:** 19 bytes with payload size 3, caught by review; now the 20 bytes the reference tool sends.
- **Research proposal versus built:** see [Fan control research](#fan-control-research).

## Sources

- psdevwiki, read through the Wayback Machine (the live site is behind a bot check): [LV2 Functions and Syscalls](https://www.psdevwiki.com/ps3/LV2_Functions_and_Syscalls) (syscalls 383, 384, 389, 409 and their SM service ids; the 383 conversion and tzone list), [Hypervisor Reverse Engineering](https://www.psdevwiki.com/ps3/Hypervisor_Reverse_Engineering) ("Guest OS VUART 2 (System Manager)", the 62 service ids, the ability bitmasks), [Syscon Firmware](https://www.psdevwiki.com/ps3/Syscon_Firmware) (tmp, tzone lists per board, fanconpolicy get/set/getini/setini, fantbl RAM versus EEPROM, trp, tshutdown, FAN SETPOLICY), [Syscon Thermal Configs](https://www.psdevwiki.com/ps3/Syscon_Thermal_Configs), [Thermal](https://www.psdevwiki.com/ps3/Thermal) (sensors, IRISMAN defaults, SYSCON's measured fan steps per model: CECH-20xx 0x33-0x4a up to about 70 C), [OtherOS++](https://www.psdevwiki.com/ps3/OtherOS%2B%2B) (SM access), [User talk:Masterzorag](https://www.psdevwiki.com/ps3/User_talk:Masterzorag) (an old ps3sm-utils `get_fan_policy 0` printing `0x01 0x01 0x48 0x00`; that tool's source is not online any more).
- ps3vuart-tools: [sguerrini97/ps3linux-ps3vuart-tools](https://github.com/sguerrini97/ps3linux-ps3vuart-tools) (`ps3sm/ps3sm_msg.h`, `ps3sm/cmd/cmd_get_temperature.c`, `cmd_set_fan_policy.c`, `ps3sm/dev.c`; commits 74f3fec, 26927cc, 85526f2, 4ffabb3, af8af4c); glevand's original command list on [ps3devwiki Ps3vuart-tools (2012)](http://ps3devwiki.com/wiki/Ps3vuart-tools).
- [webMAN-MOD](https://github.com/aldostools/webMAN-MOD): `include/poll/fancontrol.h` (syscalls, modes, 0x33-0xF4 limits, restore_fan), `include/poll/fancontrol2.h` (the control loop), `include/cmd/quit.h`, `include/setup.h`.
- [IRISMAN](https://github.com/aldostools/IRISMAN): `source/controlfan.c`, `sm_monitor/main.c` (speed and temperature tables).
- Linux: `drivers/ps3/ps3-sys-manager.c`, `drivers/ps3/ps3-vuart.c` (6.12.111 with our series).

Older versions and the dated history: [archive/THERMAL.md](archive/THERMAL.md).
