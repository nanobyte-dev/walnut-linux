+++
component = "mesa"
patches = ["patches/0068-nv30-a-fragment-program-that-closes-an-IF-or-a-loop-is-ended-by-the-appended-NOP.patch"]
aliases = ["Mesa 0068"]
category = "correctness"
switch = "none"
default = "always on"
gating = "none: every NV4x fragment program that ends in an IF or a loop"
forwarded = "no"
tests = ["mesa-fragprog:end", "mesa-fragprog:fix-off"]
batches = []
+++

# A program that closes an IF or a loop at its end is ended by the appended NOP

The translator set END on the last instruction it emitted and appended a NOP and END for branches to the end of the
program. When the program's last TGSI instruction closes an IF or a loop, the last emitted instruction is inside it, and
the ELSE and ENDIF targets or the loop's end are the appended slot, past the first END. The ps3rsx fragment-program check
(c2, strict rule S4) refuses a branch target past the first END, and if END means "stop when executed" a loop-last program
ran its body once.

END is now left off the last instruction in that case and only the appended NOP carries it. A program that closes its
structures before its last instruction is unchanged. Its own patch so the PS3 A/B can measure it apart from 0067.

## Validation

Host: `fragprog-test.c end` (IF/ELSE-last, IF-last, loop-last: the first END is the appended slot and every target is at or
before it; two control programs keep END on their last instruction) and its fix-off. The PS3 render probe (an IF(false)
skipping an END, and the colour after it) is still to do; the count-only capture must report `program-end-target` apart.
