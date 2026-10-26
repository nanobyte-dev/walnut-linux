+++
id = "big-endian-registers"
games = ["mrboom"]
driver_patches = []
game_patches = ["mrboom/0001"]
sessions = ["2026-10-26-session", "2026-10-26-session-0314"]
batches = []
+++

# Mr.Boom: the emulated 8086 registers in little-endian layout (2026-10-26)

**Symptom.** Debian's ppc64 `mrboom` 5.5+ds-1 segfaulted on the PS3 within 15 s of every start: two played
sessions, both on the welcome screen, after 13-14 s of play. dmesg: `mrboom: segfault (11) at 1835c88a1 ...
in mrboom`, a load (`lbzx`) from an address far outside the program.

**Evidence.** The fault is in `program()`, at an `LODSB` in `aff_lettre` (the font code), with ESI 0x481dd440.
Mr.Boom is the original DOS game translated from 8086 assembly to C: `program()` runs the translated code on
emulated registers. ESI held a value no font offset could have.

**Cause.** `mrboom.h` gives the registers and the string instructions (`LODSB`, `STOSW`, ...) a big-endian
layout under `MSB_FIRST`, but only the libretro core gets that define, from `retro_endianness.h`. The SDL2
binary built on a big-endian machine used the little-endian layout: a byte written to AL landed in the top
byte of EAX, so the font code's offsets went out of range on its first frames of text.

**Fix.** Patch 0001 defines `MSB_FIRST` from the compiler's byte order when the libretro header isn't used
(commit ec29378). In ppc64 qemu-user with the dummy SDL drivers, `mrboom -e` checks all 16 recorded demos
against upstream's frame numbers: SUCCESS, where Debian's binary segfaults at once. Not yet run on the PS3:
the next played session and `mrboom -e -z` there (docs/TESTS.md, batch mrboom-be).
