# Yamagi Quake II: the KISS PRNG's 16 MiB table, linked twice

Status: draft, not sent. Patch: `yquake2-0001-kiss-table-size.patch`, cut against yquake2 8.70
(`src/common/shared/rand.c`; upstream master's file is identical in the part that changes, read on
GitHub 2026-10-18). Our package patch (`gamedb/games/yquake2/packages/yquake2/patches/0002`) is bigger: it allocates the table at
first use and has a `YQ2_KISS_QSIZE` environment variable, and touches `rogue/` and `xatrix/` copies.
The upstream form is the smallest change that gets the same memory: `QSIZE` 0x1000, still a static
array. The seeding is the same code, so the sequence for a given table size matches what we ran on
the PS3 (we measured the heap-allocated 4096-entry table via the env variable; the static variant was
only compiled, not run on the PS3).

The same file exists in the add-on repositories (xatrix, rogue, ctf, zaero under github.com/yquake2):
each is a separate repo with its own copy of `rand.c`. If the maintainers like the change, the same
one-line edit goes into each, because every game module that links the file carries its own table.

## GitHub issue (yquake2/yquake2)

Title: rand.c: the 16 MiB KISS lag table is resident twice (client and game.so), 32 MB on small machines

Body:

    src/common/shared/rand.c has `static uint64_t QARY[0x200000]` (16 MiB) and randk_seed()
    writes all of it, so every page is resident. The file is linked into the client and into game.so
    (and into the add-on game modules), so a running game holds 32 MiB for random numbers.

    I hit this on a PlayStation 3 (Cell PPE, 217 MB of RAM, Debian ppc64, Yamagi Quake II 8.70,
    q2demo1 timedemo). A profile showed the quake2 binary's .bss at 18 MB resident and game.so's at
    16 MB. With the table reduced to 4096 entries (32 KiB):

    - resident set of the process: 125 -> 56 MB
    - swap in use: 80 -> 9 MB
    - time in frames over 100 ms during gameplay: 22.6 s -> 0.05 s (the stalls were page faults
      and swap-in)
    - steady fps: 65.7 -> 68.7

    Measured on the same boot, interleaved runs, 2 runs each. randk() is only used for gameplay
    decisions (monster choices, damage rolls, challenge numbers), nothing there needs the very long
    period of the large table. Only the lag table length changes; the seeding code is the same.

    Would you take a PR that sets QSIZE to 0x1000? If you would rather keep the long period for
    other targets, I can make it a CMake/Makefile option.

## Pull request

Title: KISS PRNG: shrink the lag table from 16 MiB to 32 KiB

Body: the commit message of the patch, then "Fixes #<issue>". Testing: timedemo q2demo1 and a
deathmatch game with bots on ppc64 (PS3); the 4096-entry table through a heap allocation gave the
numbers above. Compiled with gcc on x86-64 with no warnings. Not tested on other platforms.
A note for review: client-side randomness (particles) and monster rolls draw a different sequence
than before for a given seed. We did not check whether any demo or savegame depends on the sequence;
do that (play a recorded demo with both builds) before saying anything firm about it.

## Submission steps

1. Fork github.com/yquake2/yquake2, `git am gamedb/games/yquake2/upstream/yquake2-0001-kiss-table-size.patch`.
2. Read their CONTRIBUTING / code style (tabs, braces on their own lines; the patch follows the file).
3. Open the issue first, wait for the maintainer (Yamagi) to answer, then the PR. Yamagi tends to reply
   on the issue; an unsolicited PR on a design question may be closed.
4. Add-on repos only after the main repo agrees.
