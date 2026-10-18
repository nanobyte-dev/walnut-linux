# Linux: lost decrementer in ps3_power_save() (arch/powerpc/platforms/ps3)

Status: draft, not sent. Mail: `linux-ps3-0001-idle-lost-decrementer.patch` (git format-patch, subject
prefix `PATCH RFC`, Signed-off-by added, checkpatch clean, compiled for ps3_defconfig with
powerpc64-linux-gnu-gcc on the 6.12.111 tree). Our source: `linux/patches/local/0009`. The upstream form
drops the `ps3_idle_prep` core_param A/B switch and the comment that mentions it.

## Needs your attention before sending

- The fix itself is not tested on hardware. STALLS.md says "built, not yet booted". The evidence in the
  commit message shows the problem (probe: thread 1 up to 1439 ms late idle, 19.4 ms with a 10 ms
  kicker) but not the patched kernel. That is why the subject says RFC. Better: boot the test kernel
  (local/0009 is in the series since .402), run probe test 1 with `ps3_idle_prep=1`, and put the
  result into the commit message: "with this patch: worst X ms, none over 100 ms". Then drop RFC.
  Commands are in docs/STALLS.md "The A/B on the PS3" (test 1). Also watch the fan/temperature on that boot.
- The patch is cut on 6.12.111. Rebase onto the powerpc tree before sending:
  `git://git.kernel.org/pub/scm/linux/kernel/git/powerpc/linux.git` branch `next` (or Linus' master).
  I could not check the current ps3/setup.c there. `prep_irq_for_idle()` and `__hard_irq_enable()` exist in 6.12;
  confirm they still do.
- The commit message says "the fence poll of an out-of-tree GPU driver"; that driver is ps3rsx, ours.
  That is fine to say, but expect "out-of-tree" to draw the question of whether other timers are affected.
  The probe (a plain SCHED_FIFO sleeper, no driver) answers it; the message already says so.

## Recipients (from MAINTAINERS in the 6.12.111 tree, `scripts/get_maintainer.pl`)

    To: Geoff Levand <geoff@infradead.org>, linuxppc-dev@lists.ozlabs.org
    Cc: Michael Ellerman <mpe@ellerman.id.au>, Nicholas Piggin <npiggin@gmail.com>,
        Christophe Leroy <christophe.leroy@csgroup.eu>, Naveen N Rao <naveen@kernel.org>,
        Madhavan Srinivasan <maddy@linux.ibm.com>, linux-kernel@vger.kernel.org

Geoff Levand is the maintainer of PS3 PLATFORM SUPPORT; the powerpc people are reviewers and the ones
who merge (he is not always responsive, so keep them on Cc). Re-run `scripts/get_maintainer.pl` on the
tree you send from.

## Submission steps

1. `git clone` the powerpc tree (`next`), `git am --3way docs/upstream/linux-ps3-0001-idle-lost-decrementer.patch`.
2. Run `scripts/checkpatch.pl --strict` on the commit and build `ps3_defconfig` for ppc64 (cross compiler).
3. Edit the commit message if you got the with-fix numbers. Then
   `git send-email --to=geoff@infradead.org --to=linuxppc-dev@lists.ozlabs.org --cc=... -1`
   (set up `git send-email` with your SMTP first; plain text mail, no HTML, no attachments).
   Test it with `--dry-run` and by mailing yourself first.
4. Watch lore.kernel.org/linuxppc-dev for replies; resend as v2 with a changelog under the `---` line.

## What the mail argues (for answering review)

- Why not prep_irq_for_idle_irqsoff() (powernv): relies on lv1_pause() waking on a decrementer pending
  with MSR[EE] off, and the hypervisor keeping the exception pending. Undocumented. Message covers it.
- The remaining race (interrupt between the hard-enable and the hcall) ends at the next timer event with the
  periodic tick (HZ_PERIODIC, 250 Hz here), not seconds.
- Re-arming DEC before the pause would not close the window: a decrementer masked after the re-arm is lost the same way.
