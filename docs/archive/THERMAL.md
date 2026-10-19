> Superseded on 2026-10-19 by docs/THERMAL.md; kept unchanged for its history.

# Temperatures and the fan

Research and design notes, 2026-10-18. Part A (reading the temperatures) is built: kernel patch
`linux/patches/local/0040-ps3-sys-manager-hwmon-temperatures.patch`, the bench harness, a raw
probe. Not booted on the PS3 yet. Part B (fan control) was research; after the user's decision of
2026-10-18 the fan policy is built too, as `local/0042-ps3-sys-manager-fan-policy.patch` ("The fan
driver" below): SYSCON automatic at boot and on every exit path, full speed or a fixed floor on
request, nothing else sent to SYSCON. Not booted either.

(Status 2026-10-18 evening: both are booted. Kernel .433 ran with them: the temperatures through hwmon,
SYSCON automatic at boot and the full-speed round trip passed; see "On the PS3 with kernel .433" at the
end. Not tried yet: a floor, the panic path, and local/0041's power-off, button and reboot steps.)

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

**Fan policy set** (service 25 = 0x19), from ps3vuart-tools' `set_fan_policy`, used by local/0042:
payload version 1, id (0), policy (0 full speed, 1 SYSCON automatic, 2 manual), duty (0x33-0xFC
accepted by the tool for manual), 1 reserved byte, padded like the temperature request (24 bytes
on the wire, payload size 8). The tool reads no reply. Service 50 (0x32) is the get; its reply
layout is known only from syscall 409's outputs (st, policy, mode, duty); what local/0042 sends
and assumes for it is in "The fan driver".

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
- **Only the get request** is sent for the temperatures; local/0042 adds the fan policy's set and
  get (below) and nothing else.
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

## Part B: fan control (research; built as local/0042)

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

**A safe design** (the research's proposal; what was built, simpler, is in "The fan driver"):
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

The user decided on 2026-10-18 (CLAUDE.md): the fan policy service from Linux is allowed, runtime
only, SYSCON automatic at boot and on every exit path, manual only as a fixed floor at or above a
safe duty with no control loop (so no deadman is needed: nothing depends on Linux reacting in
time), the safe direction tested first; other SYSCON writes stay forbidden.

## The fan driver (local/0042)

Built after the user's decision of 2026-10-18 (CLAUDE.md: fan policy from Linux allowed, SYSCON
automatic at boot and on every exit path, manual only as a fixed floor, no control loop, other
SYSCON writes still forbidden). Same option (`CONFIG_PS3_SYS_MANAGER_HWMON`), same hwmon device
`ps3_sm`, same one-request-at-a-time machinery as the temperatures. Not booted on the PS3 yet.

**What is sent.** Only two new messages, both the System Manager's FAN_POLICY service, which
changes SYSCON's runtime policy and not its EEPROM (the fan tables stay as they are):

| | bytes on the wire | source |
|---|---|---|
| set (service 25) | header (payload size 8, tag 0) + version 1, id 0, policy, duty, 4 zero bytes = 24 | ps3vuart-tools `set_fan_policy` (struct header + 5 bytes, padded to 24); syscall 389 (id, policy, duty); webMAN `(0, 2, duty)` and `(0, 1, 0)` |
| get (service 50) | header (payload size 4, tag 0) + version 1, id 0, 2 zero bytes = 20 | **no public source of the bytes**; laid out like the temperature request (version, selector, padding) |

Policy 0 is full speed (duty 0 as in the tools), 1 SYSCON automatic (duty 0), 2 manual with a duty.
The get reply is taken, tentatively, as version, id, status, policy, speed, duty (syscall 409
returns status, policy, "mode" which webMAN reads as the current speed, duty; an old ps3sm-utils
run printed `0x01 0x01 0x48 0x00` for it). The measured temperature reply, {version, zone, status,
0, integer, fraction, 0, 0}, hints that the data may instead start after a reserved byte (policy at
offset 4): then the boot's confirmation fails, policy reads stay unused, and the raw bytes in the
log say how to fix the offsets in a follow-up. Neither reply's service id nor size is documented:
the temperature reply came back as service 14 to a 13 on the PS3 (below), so the driver takes 25/26
(set) and 51/50 (get) with 0-16 payload bytes and logs them raw, and logs every REQUEST_ERROR's
payload (its contents are undocumented too). A set waits up to 1 s for a REQUEST_ERROR or a reply,
a get up to 1 s for its reply.

**At boot** (a work item 0.5 s after probe, so the probe doesn't wait for replies):
1. get, logged raw (`fan: get reply: ...`) and parsed (`fan: at boot (layout unconfirmed): ...`):
   this shows what GameOS/webMAN left;
2. set SYSCON automatic, whatever was left, unless `fan_policy=keep`;
3. get again: if it reads version 1, id 0, policy 1 with a payload of at least 8 bytes, the reply
   layout counts as **confirmed** and later gets are used (pwm1 in automatic mode, the check
   after each set); otherwise policy reads are never used for anything, only logged;
4. then `fan_policy=full` sets full speed, `fan_floor=` a manual duty.

**Exit paths.** The fan goes back to SYSCON automatic whenever this driver set anything else:
- `.shutdown` (device_shutdown: power off, reboot, halt, kexec): cancels the boot and alert works,
  then sends automatic under the request mutex and stops all requests, before the final
  SET_NEXT_OP and shutdown request;
- the final power off and restart routines (after device_shutdown normally; without it after an
  emergency restart such as SysRq-b): a lockless send;
- a panic notifier (priority INT_MAX), also lockless. On the PS3 `ps3_panic()` is itself a
  panic notifier (INT_MIN) that never returns, so whatever `panic_timeout` says, this notifier is
  the only restore after a crash. A kdump kernel (crash_kexec runs before the notifiers) would
  set automatic itself when it probes; we don't use kdump.
- `fan_dirty` starts up (except with `keep`): an exit before the boot's automatic went out, or
  after it failed to go out, still sends automatic.

The lockless sends use `ps3_vuart_write_atomic()` (new, in ps3-vuart.c): a trylock on the tx lock,
nothing queued before it, no allocation; a partial write is finished in a bounded spin (half a
message would desync the System Manager's stream and with it the final shutdown). It retries for 10
ms when the lock is held or a write is queued (tx interrupts masked by then) and then gives up with
a "not sent" line. `.shutdown` waits at most 3 s for the request mutex (a task that died holding
it) and then uses the lockless send. `fan_dirty` goes up before a full/manual set is written and
down only after automatic was written, `fan_final` is set before an exit path looks at it, and a
full/manual set refuses once it is up, so an exit path never misses a setting.

**Errors fall back to SYSCON.** A full/manual set that fails to go out, or that the confirmed
get reads back wrong, is followed by automatic. A REQUEST_ERROR to a set (the SM refusing fan
rights to this LPAR) leaves only automatic allowed for the rest of the boot (full/manual writes
return `-EIO`), and automatic is still sent on the exit paths, since an earlier setting may have
taken; if automatic itself is refused, the exit paths stop sending it. The temperatures keep
working. A REQUEST_ERROR or three unanswered gets turn off only the reads (the get's bytes are
the guess; the set's are not). A thermal alert event hands a manual duty back to SYSCON, and
manual is refused (`-EBUSY`) until every zone has cleared (an event, not a loop: SYSCON sends it
at its own trip points). Every set checks the duty range again just before it is written.

**The safe minimum duty is 0x99 (60%).** A manual duty is fixed: SYSCON's curve is off while it
is in force, and nothing follows the load. So it has to be safe for a game at full load, not
merely above the idle point of the curve. 60% is the fixed speed the project's rules already
accept for a Linux session (CLAUDE.md, cooling), and twice what SYSCON's own curve on a CECH-20xx
reaches up to about 70 C (0x33-0x4a, 20-29%; psdevwiki Thermal, measured with IRISMAN). The upper
clamp, 0xf4 (95%), is webMAN's; full speed is policy 0. Every duty written (module parameter or
sysfs) is clamped into 0x99-0xf4 and the clamp is logged. SYSCON's "minimum" fan mode (fanconmode
3, which might be a true floor under the curve) is not reachable through this service and is not
used.

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

A set is logged (`fan: SYSCON automatic`, `fan: full speed`, `fan: manual, duty 0x99 (60%)`) only
when it changes something, so a script writing the same value doesn't fill the log.

**Game mode hook (not implemented in ps3-desktop yet).** `ps3-game-mode on` runs as root through
sudo and its watcher's `thaw` runs whenever game mode ends, however it ends. A setting such as
`GAME_FAN_FLOOR=0xb3` in /etc/default/ps3-game-mode would do, in `on`:
```
h=$(grep -l '^ps3_sm$' /sys/class/hwmon/hwmon*/name 2>/dev/null | head -1); h=${h%/name}
[ -n "$h" ] && [ -w "$h/pwm1" ] && echo "$GAME_FAN_FLOOR" > "$h/pwm1" && echo 1 > "$h/pwm1_enable"
```
and in `thaw`: `echo 2 > "$h/pwm1_enable"` (back to SYSCON). Nothing else is needed: the driver
clamps the duty, falls back to SYSCON on any error and on a thermal alert, and restores SYSCON
on shutdown and panic even if the thaw never runs. The default should stay empty (SYSCON
automatic during games too) until a measured game run shows the floor helps.

**Review.** An independent Opus review (races with the message loop, shutdown ordering, panic path,
unbind, wire bytes, hwmon ABI) confirmed the set's bytes against the tool, that nothing else is
sent, the clamp on every path, the `.shutdown` ordering and the `fan_dirty`/`fan_final` handshake.
It found one blocker, fixed: a REQUEST_ERROR used to turn off every send, the exit paths' automatic
included, so a floor accepted earlier would have stayed after Linux exited. Also fixed from it:
`fan_dirty` starting up; the set's wait 1 s like the others (a late REQUEST_ERROR charged to the
next request); finishing partial atomic writes; dropping service 26 (REQUEST_ERROR_LOG) as a "set
reply" (put back after the PS3 showed a 13 answered by a 14: an SM never sends a guest its request
id 26 otherwise); 0-byte replies accepted instead of draining the queue; REQUEST_ERROR payloads
logged; the alert scheduling regardless of a set in flight and manual refused during an alert; a
bounded mutex wait at shutdown; the duty range checked at the wire; quieter logs. Left as is: the
get stays before the boot's automatic, as the task asked (the first test boot uses `keep`, so the
guessed get is tried alone first); the 24-byte trigger (below). Compile-checked with the cross
compiler, W=1, option on and off.

**Known limits.**
- The vuart's async read wakes the driver at 24 bytes (upstream `PS3_SM_RX_MSG_LEN_MIN`). A reply
  shorter than that (a 4-byte get reply would be 20) sits in the receive list until the next
  message arrives and is then taken for the request pending at that time, so it reads one
  request late. The layout is confirmed only with 8 payload bytes or more for that reason; with a
  short reply the reads stay unused and the log shows the "late" replies raw. A fix would lower
  the trigger to the header size, which changes how every event is read: a patch of its own.
- The fan rights: the stock Linux LPAR's ability mask (0x40012) lacks bit 9, which gates both 25
  and 50. Unless our OtherOS++ grants it, the first set gets a REQUEST_ERROR and the driver leaves
  the fan alone for the boot.
- A sysfs write sending manual on one CPU at the very moment another panics can still land after
  the panic's automatic.
- Untested on the PS3: the rights, the get's bytes, the reply ids and sizes.

## PS3 test plan for the fan (local/0042)

Supervised, the user at the console and agreeing, webMAN fan in SYSCON mode before setting the
OtherOS flag (CLAUDE.md), nothing else running, stop on any "PS3 Thermal Alert". The kernel goes
in next to the known-good one as in the temperature steps (2. above); the old one stays the
default.

1. **Boot, read the policy.** Boot the test kernel from Petitboot with
   `ps3_sys_manager.fan_policy=keep` for the very first boot: only the get is sent.
   `dmesg | grep -e 'fan:' -e REQUEST_ERROR`: expect `fan: policy control on, at boot keep`, one
   `fan: get reply: service id N, payload M, tag T: ...` line (or "no reply" / "refused"), and
   `fan: at boot (layout unconfirmed): ... policy 1 (SYSCON automatic)` if the guess is right and
   webMAN was in SYSCON mode. A late reply shows as `fan: reply no request waits for`. Note the
   bytes. A "refused" get means no fan rights or a wrong guess: the set still decides.
2. **Reboot with the default** (no parameter): the boot sets SYSCON automatic (a no-op when webMAN
   left it there). Expect `fan: SYSCON automatic`, either `fan: set reply ...` or `fan: no reply to
   the set within 500 ms`, and `fan: get layout confirmed` or `not confirmed`. A
   `setting SYSCON automatic refused (REQUEST_ERROR)` line means our OtherOS++ gives the Linux LPAR
   no fan rights: stop here, nothing more to test (the temperatures are unaffected).
3. **Full speed and back (the safe direction):**
   `h=$(dirname $(grep -l ps3_sm /sys/class/hwmon/hwmon*/name))`;
   `echo 0 | sudo tee $h/pwm1_enable`: the fan must audibly go to full speed within a few seconds
   (also `tools/webcam-audio.sh` from the host, if the user is not at the TV); `cat $h/pwm1_enable`
   reads 0. Then `echo 2 | sudo tee $h/pwm1_enable`: it must spin down again within SYSCON's ~2 min
   step-down. dmesg: `fan: full speed`, `fan: SYSCON automatic`, no "back to SYSCON" warnings.
4. **Full speed across a reboot:** `echo 0 | sudo tee $h/pwm1_enable`, then `sudo reboot`: the
   console shows the fan going back before the LPAR ends (`fan: SYSCON automatic` in the
   shutdown messages, netconsole if set up), and in GameOS/webMAN or after the next Linux boot's
   get it is SYSCON automatic. Same with `sudo poweroff`.
5. **Floor, only with the user's explicit OK:** `echo 0x99 > $h/pwm1` (stored), `echo 1 >
   $h/pwm1_enable`: `fan: manual, duty 0x99 (60%)`, the fan steadier and louder than at idle;
   `cat $h/pwm1` 153. Then `echo 2 > $h/pwm1_enable`. A value below 0x99 must log the clamp and
   still read 153. Watch the temperatures meanwhile (`ps3-sm-temp.sh`), stop above 75 C.
6. **Panic path (optional, only with the user's OK):** with full speed set (`pwm1_enable=0`),
   `echo c | sudo tee /proc/sysrq-trigger` crashes the kernel on purpose: the console must show
   `PS3 fan: SYSCON automatic (panic)` and the fan must spin down within ~2 min; then a hard
   power-off (the panic halts). Do this from full speed, never from a floor.
7. Rollback without a rebuild: `ps3_sys_manager.fan_policy=off` (no fan requests at all).


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
  defaults, SYSCON's measured fan steps per model: CECH-20xx 0x33-0x4a up to ~70 C),
  [OtherOS++](https://www.psdevwiki.com/ps3/OtherOS%2B%2B) (features: SM access),
  [User talk:Masterzorag](https://www.psdevwiki.com/ps3/User_talk:Masterzorag) (an old ps3sm-utils
  `get_fan_policy 0` printing `0x01 0x01 0x48 0x00`; that tool's source is not online any more).
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

### Confirmed on the PS3 (2026-10-18, kernel 6.12.111+ps3.178, OtherOS++ on Evilnat 4.92)

`ps3-sm-temp.sh --raw` through /dev/ps3sysmngr, after a long benchmark session: the SM answers the
Linux LPAR. Zone 0 (Cell) 65.76 C, zone 1 (RSX) 60.00 C, status 0; reply service id 14, tag 0, payload
8 bytes (`0100000041c30000`, `010100003c000000`: {ver, zone, status, 0, integer C, fraction/256, 0, 0}).
The hwmon driver's accepted formats cover this.

### On the PS3 with kernel .433 (2026-10-18 evening, dated note)

Kernel 6.12.111+ps3.433 (local/0040, 0041, 0042 and the rest of that series) booted from Petitboot.
From the session notes: the hwmon `ps3_sm` temperatures read, the fan went to SYSCON automatic at
boot, and test step 3 (full speed through `pwm1_enable=0`, then back to 2) worked. The exact bytes of
the get reply (whether the layout guess was confirmed) are in that boot's dmesg and are not copied
here; read them before relying on `pwm1` in automatic mode. Not done yet: step 1's `fan_policy=keep`
boot (if it was skipped, nothing is lost: the default sends only the automatic policy), step 4
(full speed across a reboot), step 5 (a floor, only with the user's OK), step 6 (panic), and the
power-off, button-tap and reboot checks of local/0041 in "PS3 test steps" 5. Game mode has no fan
hook yet ("Game mode hook" above), and nothing measured whether a floor would help a game.
