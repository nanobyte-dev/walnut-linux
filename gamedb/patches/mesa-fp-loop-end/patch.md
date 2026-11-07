+++
component = "mesa"
patches = ["patches/0067-nv30-a-fragment-program-loop-ends-on-an-instruction-slot.patch"]
aliases = ["Mesa 0067"]
category = "correctness"
switch = "none"
default = "always on"
gating = "none: the REP end of every NV4x fragment program with a loop"
forwarded = "no"
tests = ["mesa-fragprog:rep", "mesa-fragprog:golden", "mesa-fragprog:fix-off"]
batches = []
+++

# A fragment program loop ends on an instruction slot

`nv40_fp_rep_end` set the REP instruction's end offset to the last instruction's offset + 4. That is the slot after the
loop only while the last instruction has no constant of its own; with one, its 16-byte constant follows it in the program and
the offset named that data slot. The end is now the program's length so far, the next instruction slot (the appended NOP and
END when the loop is the last thing in the program). The kernel's fragment-program check (ps3rsx c2) refuses the old encoding,
so this ships with it, and the ps3rsx-modules of that kernel `Breaks:` the Mesa before this one.

## Validation

Host: `fragprog-test.c rep` walks the translated program the way the GPU does and checks the loop's end is an instruction slot
for a loop ending in a constant, in a temporary, and at the end of the program; `golden` shows only that one word differs in
eight programs. The corpus has no program with a loop. Nothing to measure on the PS3.
