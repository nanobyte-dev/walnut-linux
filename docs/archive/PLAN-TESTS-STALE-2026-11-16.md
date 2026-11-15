> Removed from docs/PLAN.md and docs/TESTS.md on 2026-11-16 (the user: "Clean them up") because the entries were done or superseded; kept unchanged for their history. The current text is in those two files.

# Stale entries of PLAN.md and TESTS.md, 2026-11-16

## docs/PLAN.md

Rewritten to the current state (the sections below are the old text, verbatim): "Next up (the user, 2026-10-23)" (items 1 and 2 done: the cleanups, and the Atomic Bomberman clone as the `bomberclone` package, 09e2cb92), "Where we are (2026-10-19)" (replaced by the state after the 0.1.0 release), decision D1 (stable publish: done with Walnut 0.1.0 on 2026-10-23), D13 (rewritten to the kicker alone) and D14 (the release image exists), and the status cells of items 1.2, 1.15, 5.1 and 5.4.

### Next up

## Next up (the user, 2026-10-23)

Walnut 0.1.0, the first public alpha, is out ([release/ALPHA.md](release/ALPHA.md)). In this order:

1. A round of cleanups (disk space on the PC).
2. An Atomic Bomberman clone on the PS3, packaged.
3. Then what the release interrupted, now one stage: **Walnut Games** ([GAMEDB.md](GAMEDB.md)), a
   games database at walnut.nanobyte.dev/gamedb/ and a launcher app on Walnut, every game played,
   measured and compared with the PC, rated with medals. Steps:
   - P0 the rating, calibrated on the 13 benchmarked games (done, the user's OK);
   - P1 the catalog (one folder per game), the scoring and import tools, the Hugo site (3 games);
   - P2 the server: screenshots as their own upload kind, /gamedb/ published;
   - P3 the session recorder (the ~10-minute played session, its end-of-session form);
   - P4 the desktop app (Tk) and its install helper;
   - P5 the games one by one to about 50, new games first. Rules 2.0 (2026-10-26, [GAMEDB.md](GAMEDB.md#rules-versions))
     rate the sessions from here on: each game runs on its shipped defaults (the player's settings set
     aside and back), the recorder measures the input, the pointer, the level load and the quit, the
     importing agent answers a picture questionnaire (review.py), the form asks eight one-tap questions.
   - Scripted runs ([games/SUITE.md](games/SUITE.md#scripted-runs)) are the benchmark harness for measuring game and
     GPU-driver optimizations (A/B runs, regressions, profiling). They never rate a game: ratings come from played sessions only.
   - P6 (the user, 2026-10-26: "keep planned"; started as 5.12): tests submitted by other Walnut users,
     recorded by the same harness and form (`walnut-rate`), sent to us as GitHub pull requests and reviewed
     by the maintainer before they count. Decided by the user, 2026-10-30: pull requests rather than a receiver
     on our server, an optional nickname for the credit, an accepted session weighing like ours; one player's
     odd setup moving a medal is what the review is there to catch.

The rest of this file predates the 0.1.0 release and gets a refresh alongside P2.

### Where we are

## Where we are (2026-10-19)

The whole stack is built and packaged: the kernel (6.12 LTS with the OtherOS++ patches and ours), the ps3rsx
driver, Mesa nv30, the SPU toolchain and libspeq, the desktop with game mode, the splash, our apt
repository. The release kernel `6.12.111+ps3.472` with ps3rsx 0.443 and Mesa `.476` is the
default of the 2026-10-18 release build ([archive/RELEASE-CANDIDATES-2026-10-19.md](archive/RELEASE-CANDIDATES-2026-10-19.md)). Releases
now go through CI to walnut.nanobyte.dev's stable suite (docs/release/ALPHA.md); the LAN repository
that build waited in was retired on 2026-10-26 (docs/PACKAGING.md).

### D1

| D1 | Stable publish of the release (old 1). Decided 2026-10-18 as "test first, then publish": everything rebuilt official, an image built, the PS3 image session, then the stable publish with an explicit `DEBS=` list and `build/aptly` backed up. Nothing is published yet. | [archive/RELEASE-CANDIDATES-2026-10-19.md](archive/RELEASE-CANDIDATES-2026-10-19.md) | 5.1 |

### D13D14

| D13 | The default kernel: the release build makes `.472` (with the idle fix) the default; the stable suite still has the 2026-10-16 builds. Left: when the stopgap kicker goes for good | [archive/RELEASE-CANDIDATES-2026-10-19.md](archive/RELEASE-CANDIDATES-2026-10-19.md) | 1.2 |
| D14 | Reimage timing: the user said on 2026-10-17 the image rebuild waits until this plan is through; it is in Stage 5 | [IMAGE-DIFF.md](IMAGE-DIFF.md) | 5.4 |


## docs/TESTS.md, rows removed from Next

Why: all done: History 11-01 00:38 (sessions K and M), the gelic off run, the merged GART game run and the poweroff check; the row itself said "Nothing left".

| Stability patches on the PS3 (sessions K and M, 11-01 00:38, then the U5d session 2 boot and the 05:20 power-off the same day): all done. The gelic off run (watchdog off: no fault, no re-probe, packets kept coming, watchdog back on), the merged GART's game run (OpenTTD drew, backing 13 -> 20 -> 12 MiB, CmaFree back, no GART errors) and `sudo poweroff` for kernel-sm-shutdown-wait (down within about 15 s) are in VALIDATION.md and the patch entries. Nothing left | none | none | Phase 3 plan, 2026-10-31 |

Why: done: History 11-01 gpu-recovery sessions 1-4 (Mesa 0053 and the in-driver recovery; session 4 passed, mpv and Neverball kept drawing); its debs (ps3rsx 0.1137 for kernels .874/.1076) are superseded by the testing builds.

| Recovery from a rejected push (ps3rsx 0.1137 + Mesa 0053, [DRIVER.md](DRIVER.md#a-rejected-push-the-submit-is-cancelled-the-client-recovers)): a rejected push cancels its submit (-ECANCELED) and Mesa re-sends its state, instead of the channel being refused. Only after the mpv 0x105 row above passed, with the user present (a wrong guess wedges the RSX: the user reboots), not while the user plays or a batch runs, nothing else queued after it in the boot. Debs (built 2026-10-29, not published): `ps3rsx-modules-6.12.111+ps3.874` and `ps3rsx-modules-ps3` 0.1137 from `build/old/recover/ps3rsx-0.1137-k874` (for kernel .1076: `build/old/recover/ps3rsx-0.1137-k1076`), Mesa 26.2.4-1+ps3.1038~dev with 0052 and 0053 from `build/old/recover/mesa-ps3.1038dev-0053` (libgl1-mesa-dri, mesa-libgallium, libegl-mesa0, libglx-mesa0, libgbm1), and the same Mesa without 0052 in `build/old/recover/mesa-ps3.1038dev-0053-no0052`, not installed: `dpkg -x` into a directory and run only mpv on it (`LD_LIBRARY_PATH` and `LIBGL_DRIVERS_PATH` there). 1) X and the desktop come up, glxgears and a light game smoke (`run.sh smoke xmoto`) draw, dmesg free of `rejected push`. 2) mpv `--vo=gpu --hwdec=no` on the 0x105 clip with the installed Mesa (0052): plays, no rejection. 3) The same mpv on the no-0052 Mesa: each frame `rejected push: texture format X16` and `submit cancelled` (rate-limited), no graph exception, mpv keeps running with a black or empty picture, X and the desktop keep drawing; quit mpv. 4) Once more with `echo 1 \| sudo tee /sys/module/ps3rsx/parameters/refuse_rejected` (the old rule: `refused from now on`), then back to 0. Stop at the first graph exception (webcam, dmesg `PS3 graph exception`) and keep dmesg | mpv, Mesa without 0052 | the user | host: validate-test's recovery cases, the mesa-shim runs (commits 1aeaf88, 44c91ab) |

Why: done: the recovery work above (gamedb patch ps3rsx-gpu-recovery, Mesa 0053).

| RSX wedged after a game crash (2026-10-26, [SuperTuxKart](../gamedb/games/supertuxkart/)): after the segfault in Mesa (GART exhausted), X kept running but nothing drew; a fresh X couldn't draw either (the 3D state still the old X's target, 0x900000; the TV showed the game's last frame); the ssh reboot then hung too (the user power-cycled). Find why the RSX's FIFO stops (a client's pushbuffer left mid-command at its death? ps3rsx's cleanup of a dead client's channel), make ps3rsx recover by itself, then reproduce with a GL client killed mid-frame (SIGKILL in a draw loop) | a GL test client, SuperTuxKart | PS3 | this row |

Why: done: History (the DMA watchdog row) measured it; the row said "Nothing more to measure".

| snd_ps3 DMA watchdog (kernel local/0047, proven in ssdroot12 with the test module), in the booted `.874` and every kernel since `.751`: `cat /sys/module/snd_ps3/parameters/dma_stats` answers with the default 200 ms, and one forced stall (`dma_test_stall` under `speaker-test`, as in the ssdroot12 row) restarts the DMA within 0.5 s. Nothing more to measure; `snd_restarts` in later batches keeps counting | none | PS3 | LOG-FINDINGS.md item 1 |

Why: done: Walnut 0.1.0 released 2026-10-23 (docs/release/ALPHA.md, TESTING-0.1.md); the archived release-candidates plan it cited is superseded.

| Release image session | | the user | [archive/RELEASE-CANDIDATES-2026-10-19.md](archive/RELEASE-CANDIDATES-2026-10-19.md) |

