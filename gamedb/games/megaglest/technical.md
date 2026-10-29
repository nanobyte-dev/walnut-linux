# MegaGlest: technical notes

## Status

Debian's MegaGlest 3.13.0 (`megaglest`, `megaglest-data` 400 MB, with wxWidgets and fonts: about 430 MB).
Smoke run on 2026-10-29 (kernel .874, Mesa ps3.924, the VRAM swap on; `ps3-bench-20261028T233747Z-smoke-megaglest-base`): the recipe
([benchmark/recipe.sh](benchmark/recipe.sh)) writes ~/.megaglest/glestuser.ini (fullscreen 1920x1080, set aside and
restored) and starts `megaglest --auto-test=150,,exit`, which plays the game's scenarios with the AI on every
side. A scenario on a forest map runs, drawn right (the units, buildings, trees, rocks, rain, the fog of the
edges, the HUD; xwd), fullscreen. 24 fps steady on the Gallium HUD, 1% low 3.3, 78 frames over 100 ms (27 s of
the 90 s window), the longest 5.2 s; 108 MB peak RSS. At the 150 s limit it does not exit: `terminate called
after throwing ... megaglest_runtime_error: In [thread.cpp::~Mutex Line: 534] about to destroy mutex refCount =
1`, and the harness killed it at 200 s. Runs, needs performance work (a profile first) and the exit fixed.


## Our package

`megaglest` 3.13.0-10.1+ps3.N ([packages/megaglest](packages/megaglest/)),
[0001](packages/megaglest/patches/0001-byte_order-don-t-byte-reverse-std-string-objects.patch): Debian's build
crashed at the main menu on the PS3 (SIGSEGV ~50 s after the start, in a SimpleTaskThread; the bench runs
never saw it, `--auto-test` and `--load-scenario` skip the menu). The core (Debian's megaglest-dbgsym, gdb
on the PC) put it in `PlatformByteOrder::EndianReverse<std::string>`: that template reverses the raw bytes
of any object, `toCommonEndian` calls it on big-endian machines, and the CRC cache
(`platform_common.cpp`) passes its version strings and file path through it, so the strings' own pointers
were reversed. 0001 returns a string unchanged. On the PS3 (2026-10-29, `ps3-bench-20261029T044952Z-smoke-megaglest-menu`):
the main menu stays up and animated past 100 s, 38 fps, no crash. The auto-test's exit no longer throws
"about to destroy mutex refCount = 1" but doesn't end within the harness's 220 s
(`ps3-bench-20261029T045225Z-smoke-megaglest-autotest`): the same CRC thread, which now runs to its end
(the CRCs of the whole data set on the PPE, ~80 s past the test, at 114% CPU) and is waited for at exit,
where before it was destroyed running. With a longer cap the game exits by itself, rc 0, no throw
(`ps3-bench-20261029T052419Z-smoke-megaglest-autotest`).

## Our defaults

The menu entry is ours (`desktop/launchers/megaglest-walnut.desktop`, Debian's diverted): a first start writes
`~/.megaglest/glestuser.ini` with `AutoMaxFullScreen=true`, since Debian's `glest.ini` says 800x600 (a mode
change on the TV); with it the game takes the desktop's size (checked on the PC: 1280x720 on a 1280x720
Xvfb, "forced change of resolution to desktop values"). After that the player's settings decide. Measured
on the PS3 on one scenario (`--load-scenario=amazones`, 2026-10-29): Debian's settings 22 fps with 29.3 s of
stalls in 90; `Shadows=Disabled` 23 fps / 25.9 s; that plus `NV30_FORCE_DXT=all` 22 fps / 31.6 s with 16 MB
less VRAM: within one run's noise, so neither is shipped. The stalls are the swap (~150 MB of the game on
disk): a memory job, not a GPU setting. Checked on the PS3 with megaglest 0001 (2026-10-29): the entry's
start writes the glestuser.ini and the main menu comes up fullscreen 1920x1080 in game mode, still up at
110 s.

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-f-engines.md](../../research/gl3-census-f-engines.md), "Scan: other games that need GL 3+ (one line each)": GL 2.x, shaders optional; Debian `megaglest` 3.13.0-10.1+b1; 400 MB+. Verdict: GL2 already; heavy CPU RTS.
