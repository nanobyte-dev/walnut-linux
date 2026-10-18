# Linux: ps3_sys_manager_fin() can hang at "System Halted" (drivers/ps3/ps3-sys-manager.c)

Status: draft, not sent, RFC (untested on hardware). Mail: `linux-ps3-0002-sys-manager-fin-wait-for-command.patch`
(subject prefix `PATCH RFC`, Signed-off-by added, checkpatch clean, compiled for ps3_defconfig with
powerpc64-linux-gnu-gcc on the 6.12.111 tree). Our source: `linux/patches/local/0041`.

## What changed against our local patch

0041 is large: shutdown request retries (3 requests, 5 s apart), a `ps3_sm_final` flag so that button
presses during the final shutdown are only logged, a busy-wait when interrupts are off, and it
depends on local/0040 (hwmon temperature reply, which returned -EAGAIN to keep the old loop going).
None of that is needed for the core bug and 0040 is not upstream. The upstream form is the minimal
fix: `ps3_sys_manager_handle_cmd()` returns `PS3_SM_MSG_SHUTDOWN_ACKED` (1) after the ack, and
`ps3_sys_manager_fin()` loops until that value. A failed ack is returned as an error and the wait
goes on. The retries, the flag and the spin are held back for follow-ups if the maintainers want them
(each is a separate behaviour change and each is untested too).

## Needs your attention

- Untested on hardware. The commit message says so. It becomes testable in your boot session:
  the local 0041 build is the larger variant of the same idea, so it exercises the same path, but the
  exact minimal patch has only been compiled. To test it: build the kernel with only this patch and
  trigger a poweroff while pressing the power or reset button right after the request (the window is
  small; it may not be reproducible on demand). If it cannot be provoked, say so in the mail: "no
  regression in normal poweroff and reboot; hang not reproduced on demand" is still an honest RFC.
- Our local 0041 says "Upstream (torvalds master, Geoff Levand's ps3-queue-v6.13) has the same loop
  and no fix", checked 2026-10-18. Re-check against the current tree before sending.
- Please keep the claim in the commit message to what the code shows: the hang is deduced from reading
  the loop, not from a captured hang. If you ever saw "System Halted" stuck after a button press, add
  the date and what you did.
- Same rebase note as the idle patch: cut on 6.12.111, rebase onto powerpc `next`.

## Recipients and steps

Same as `linux-ps3-idle.md` (Geoff Levand, linuxppc-dev, powerpc reviewers on Cc; `drivers/ps3/` is in
the PS3 PLATFORM SUPPORT entry). Send it as its own thread, not together with the idle patch: different
files, different risk. `git send-email` with `--dry-run` first.
