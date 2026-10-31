+++
component = "kernel"
patches = ["patches/local/0041-ps3-sys-manager-fin-wait-for-shutdown-command.patch"]
aliases = ["local/0041", "kernel 0041"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "not yet (test on the PS3 first)"
tests = []
batches = []
+++

# sys-manager: the final shutdown waits for the shutdown command, not for any message

`ps3_sys_manager_fin()` sends the shutdown request and then polled the port until a message handler
returned 0. The command handler returns 0 after acking the shutdown command, but so does the event
handler for every event. Now `handle_cmd()` returns `PS3_SM_MSG_SHUTDOWN_ACKED` once the ack is written
(a failed ack is an error and the wait goes on), and `fin()` waits for that value only, handling events,
temperature replies and errors as before. Without a command 5 s after a request it sends the request
again, three in all, then keeps polling, so a late command is still acked. It pauses only when no header
is queued, spins 1 ms instead of pausing with interrupts off, and a power or reset press during the final
shutdown is only logged.

It builds on [kernel-sm-temperatures](../kernel-sm-temperatures/patch.md) (local/0040), whose
temperature reply handler returned -EAGAIN only to keep the old loop going. An upstream draft is in
`docs/upstream/linux-ps3-sys-manager.md`.

## Investigation

- **What crashed:** a hang at power off: the console sat at "System Halted, OK to turn off power" with
  its LED green until it was powered off by hand.
- **Why:** a power button press, a reset press or a thermal alert or cleared event that arrived between
  the shutdown request and the System Manager's command ended the poll, so the command was never read or
  acked and the hypervisor never destroyed the LPAR. Upstream (torvalds master, Geoff Levand's
  ps3-queue-v6.13, linuxppc-dev patchwork, 2026-10-18) has the same loop
  ([THERMAL.md](../../../docs/THERMAL.md#the-shutdown-fix-local0041)).
- **How it was found:** code reading while writing the hwmon driver (local/0040) on 2026-10-18, which
  adds temperature replies to the same port.
- **Minimal repro:** None yet: proposed: THERMAL.md "PS3 checks still to run" step 4 on a kernel
  without the patch: `sudo poweroff`, then short taps on the power button about once a second until the
  console is off. With the patch it must go off on its own.

## Validation

Not yet validated on the PS3.
