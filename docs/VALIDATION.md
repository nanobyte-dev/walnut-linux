# Validating the stability patches: the fix ladder

The user's plan (2026-10-31): "we test without any of the patches. We run the minimal testing suite.
With each fix, we expect more tests to pass, until all fixes mean all tests have passed. We can use the
tests as regression tests in the future."

So each component's stability fixes (register category `stability`, `gamedb/patches/*/patch.md`) go
in one rung at a time, in series order, and the same tests run at every rung. The expectations come
from the register: an entry's `tests` names the tests that guard its fix (a run-tests suite, one part
of it as `suite:part`, or `ps3:<program>`). At rung k a test should pass when every fix it guards is
at rung k or below; a test that guards no stability fix should pass at every rung. Anything else is a
surprise, and the ladder names it:

- **too early**: the test passes below its fix's rung, so the repro doesn't reproduce the bug;
- **after fix**: it fails at its fix's own rung, so the fix is broken (or the test needs more than it);
- **regression**: it passed and fails at a later rung, and that rung is named as the cause.

Rung 0 is the series without its stability fixes. The performance and correctness patches stay in
every rung (the later fixes are written on top of them, and their own tests must keep passing), and so
does a fix that can't be taken out (below). The order of work: the host ladder, then one PS3 session
for the Mesa ladder and its wedging pairs, then a short kernel session. A session's results go into
each entry's Validation section (date, versions, a few lines of dmesg) and a row in
[TESTS.md](TESTS.md) History.

## 1. The host ladder (PC)

`make ladder FULL=1` runs [tools/ladder.py](../tools/ladder.py) for every component; `make ladder`
runs only Mesa's top rung (every fix in, every test must pass), which is also the run-tests suite
`fix-ladder` in `make test`. Each prints a matrix of test by rung (`P` pass, `x` failing below its
fix as expected, `!P` and `!F` surprises) and the surprises, and exits 1 on any. The last run's
outcome per component is in `build/tmp/ladder/result.json`; the commit check `patch-register` warns
while a top rung there is failing, and lists the stability entries that name no test (they can't be
on the ladder).

| Component | How the rungs are made | Tests |
|---|---|---|
| Mesa | A clone of `build/work/mesa` in `LADDER_DIR/mesa` (config.local.mk; never `build/work` itself). Branch `ladder/all` is the Debian base plus every patch file in series order; the rungs below come from reverting the stability fixes from the top down (`ladder/rungNN`). | `packages/patched/graphics/mesa/tests/host-tests.sh` with `MESA_WORK` at the rung's tree, part by part |
| ps3rsx | Its own ladder: `fix-off.sh` turns each validator rule off in a copy of `validate.c` (rung "off", each case must fail), `validate-test` runs them all on | the validator's cases |
| sdl2-compat, luajit | two rungs, the stock source and our series, through their suites | `close-locked-test.sh` (SDL2 and SDL 1.2 API), `low-2gb.sh` |

A fix whose revert doesn't apply, because later patches are written on top of it, can't be taken out
of the series: the ladder calls it **fused**, carries it in rung 0 and expects its tests to pass at
every rung. Its off check stays the per-file one (`host-tests.sh alloc-fail fix-off` compiles a test
against the file as it was before its patch) or the PS3. Not laddered on the host, for want of a host
test of the fix in a tree: the kernel's stability patches (section 3), boost-context, plymouth,
pnmixer and webkit2gtk.

After a merge in `build/work/mesa`, `tools/ladder.py --refresh` clones it again (the old clone stays
as `mesa.prev`). While a merge is under way in the working tree, `--rev HEAD` (`make ladder REV=HEAD`)
takes Mesa's series, the register and Mesa's host tests from the last commit instead.

**2026-10-31.** On the series before the Mesa merges (`--rev HEAD`, 2 min 25 s): 9 Mesa rungs, 0 the
base, then 0002, 0014, 0046, 0047, 0048, 0049, 0052, 0053; fused mesa-context-pushbuf (0003),
mesa-vbo-gpu-fetch (0005), mesa-texture-uploads-gart (0007), mesa-thin-mip-levels (0013) and
mesa-ctx-state-reset (0029). On the merged series (41 patches, not committed yet; 2 min 39 s): 9 rungs,
0 the base, then 0003 (removable now), 0014, 0046, 0047, 0048 (0002 folded in), 0049, 0052, 0053;
fused 0005, 0007, 0013, 0029. On both, with the register's tests split into parts, every test fails
below its fix and passes from it on (merged: pushbuf-flush and -destroy from rung 1, query-steal from
3, upload from 4, mm-fallback and mm-nomem from 5, fence and transfer-cpu from 6, fence-cancel at 8),
and the tests that guard no stability fix pass at every rung. Two surprises came up while building it,
both in the tests, not the fixes: alloc-fail-test.c didn't build without 0053's
`nouveau_device::cancelled` (it now sets it only when the tree has it), and the ppc64 part of
minmax-vmx lost its source when the tests came from elsewhere (mounted now). ps3rsx: 16 rules, each
caught off and passing on; sdl2-compat and luajit: stock fails, ours passes. 0 surprises.

## Rules for every PS3 session

- The user is present and nobody is playing. One PS3 agent at a time: take the PC-side lease first,
  `export PS3_LEASE=$(tools/ps3 lock --note "validation M")`; `make deploy` and `tools/play-test.sh`
  refuse while someone else holds it. The PS3-side bench lock stays as it is.
- The debs of the session (the shipped ones and any test build) are copied to `build/testsets/<session>/`
  with a `VERSIONS` file; `make clean-ephemeral` leaves that folder alone. The ladder's libraries are in
  `build/testsets/ladder/`, each rung with its `BUILD` file (branch and commit).
- netconsole up (`make listen`) before anything that can wedge, or the dump read from the console's disk
  afterwards.
- webMAN fan control in SYSCON (automatic) mode, or a fixed speed of at least 60%, at every flag change.
- The user sets the boot flag and reboots; agents never flip it, reboot or kexec.
- `sync` right before a repro that can wedge. After a hard power cycle, only `fsck -n` on the unmounted
  image; repairs are the user's.
- Kernel-oops repros only after the panic-path fan restore passed ([THERMAL.md](THERMAL.md#ps3-checks-still-to-run)
  step 9: full speed, `sync`, `echo c > /proc/sysrq-trigger`, `PS3 fan: SYSCON automatic (panic)` on the
  console and the fan slowing within 2 min; the user powers off, then `fsck -n`); until then they are left out.
- No validator-off runs: a Mesa fix's "off" is the validator's rejection in dmesg, not a wedge
  (validate.c refuses X16 and UB256x3 whatever Mesa sends).
- The GameOS flag is back at the end of the session.

## 2. PS3 session M: the Mesa ladder (1 boot, the shipped stack)

On the PC, `make ladder-ps3-libs` builds each Mesa rung's libraries (the nouveau and softpipe drivers,
EGL, GLX, GBM; [tools/ladder-mesa-build.sh](../tools/ladder-mesa-build.sh), cross-built the way BUILD=xcc
packages are, with debian/rules' options) into `build/testsets/ladder/rungNN/{lib,dri}`, with
`manifest.txt` (the rungs, and the register's `ps3:` tests with their fix rungs) and the runner.
`RUNGS="0 8"` builds only some. Proof on 2026-10-31 (rungs 0 and 8 of the series before the merges): rung 0 in 5 min 25 s from a
fresh build directory (ccache warm from the Mesa deb build), the top rung after it in 4 min; 26 MB
each, stripped. Every rung of the merged series takes about 4 min more each.

On the PS3, as the desktop user, with ps3rsx loaded and the shipped Mesa installed (nothing is
reinstalled): copy the folder to `/var/tmp/ladder/` and run `rsx-ladder.sh /var/tmp/ladder`
(ps3-gpu-tools' scripts; the copy in the folder until the package has it). For each rung it runs the
GL tests (egl-clear, -tri, -tex, -gears, -short; gl-texfmt, -texsub, -fog, -sky, -pointsmooth, -dxt,
-genmip, -mipread; egl-thin 1 64; rsxdrm-smoke quirks once) with `LD_LIBRARY_PATH` and
`LIBGL_DRIVERS_PATH` on that rung, after checking that the GL stack really loads the rung's libgallium.
It records pass or fail (exit 0 and the RSX as renderer, not a softpipe fallback) and the kernel lines
each test caused (`ps3rsx: rejected`, graph exceptions), and checks that the GPU still answers
(`rsxdrm-smoke nop`) after every rung, stopping if not. Output in `results-<time>/`: `results.tsv`
(synced as it goes), `matrix.txt`, `summary.txt` (the surprises, the kernel lines per rung) and the logs.

**The wedging pairs.** A test whose old failure hung the RSX (gl-genmip, gl-mipread, egl-thin) runs at
two rungs only: its fix's own rung in the normal order, and the rung below it among the last runs of
the session, `sync` first. Today only gl-mipread has such a pair (rungs 2 and 1,
mesa-blit-source-alignment); the other two guard fused fixes and run at rung 0 only. With the
validator's tex-offset rule its off run should end in a `rejected` line, not a wedge; if the RSX does
hang, the session ends there (netconsole up, the user powers off).

**After the runner,** by hand on the same rung folders, still no reinstall: mpv `--vo=gpu` on a video
with `LD_LIBRARY_PATH=/var/tmp/ladder/rung06/lib` (below mesa-no-x16: a `rejected push` line per frame
for its X16 texture; low wedge risk, `sync` first), then with rung07 (no such line).

## 3. PS3 session K: the kernel's stability patches (1 boot, the shipped kernel)

The kernel stability patches with a repro on the PS3, through their switches, least risky first:

| Patch | Repro | Fix off | Off should show | Wedge |
|---|---|---|---|---|
| kernel-os-area-no-writes (local/0044) | passive, the whole session: no `FLASH ROM` update attempt in dmesg | never (`ps3_os_area.write` would write flash) | - | no |
| kernel-idle-irq-pending (0009) | `ps3-waker -m probe -i 1.37 -t 120` | `echo 0 > /sys/module/kernel/parameters/ps3_idle_prep`, then 1 again | wakeups 100 ms or more late (up to about 1 s); on: worst under 5 ms | no |
| kernel-ps3disk-write-guard (0045) | a write-back outside the sealed ranges (GAMEOS-HDD.md, Phase B) | none | on: EIO, the image unchanged | no |
| kernel-snd-ps3-dma-watchdog (0047) | `speaker-test`, then `echo 1 > /sys/module/snd_ps3/parameters/dma_test_stall` | `echo 0 > .../dma_watchdog_ms`; set 200 again after | the hardware pointer stops and sound with it; on: restarted within 200 ms | sound (`sync` first) |
| kernel-gelic-rx-watchdog (0049) | `rx_watchdog_test=1` with the watchdog on: receive stalls, and comes back | `ps3_gelic.rx_watchdog=0` takes the network away with no way back over ssh: only with the user at the console's keyboard, else the on run only (shown on +ps3.747) | receive stays dead; on: the watchdog restarts it, ssh answers | the network (last) |

The merged GART check below (the kernel merge agent's part) goes in the same boot, with
kernel-sm-shutdown-wait's `sudo poweroff` at its end.

## Left out, and why

- No PS3 repro: kernel-gelic-skb-alloc-fail (needs a fault-injection kernel), kernel-ps3-srr-clobber
  (a debug kernel with `CONFIG_PPC_RFI_SRR_DEBUG`), kernel-ps3-storage-cmd-timeout (a debug storage
  driver that drops completions: a risk to the disk), kernel-ps3disk-unwind (no probe failure on demand),
  kernel-spufs-fault-lock-order (lockdep), kernel-spufs-runqueue-ref (a race, KASAN stress not planned),
  pnmixer-no-card-crash (the PS3 always has a card).
- kernel-spufs-gang-alive: its repro would be a use after free reachable by a user; it stays out of the
  repository, and off the PS3 until the panic path passed.
- kernel-spufs-acquire-saved-race: its fix-on repro (two threads reading a running context's `regs` and
  `mem` in a loop) is still to be written; then on the shipped kernel, `sync` first, after the panic path.
- kernel-vdso-chacha-elfv1, boost-context-save-fprs, luajit-low-2gb, sdl2-compat-close-locked-deadlock,
  plymouth-no-console-viewer, the webkit ones and kernel-gelic-netpoll: their off builds break boot or
  the whole browser (or are host-laddered), and the shipped stack runs them every day; their on checks
  are the daily use (`echo t > /proc/sysrq-trigger` reaching netconsole for netpoll).
- The fused Mesa fixes (0005, 0007, 0013, 0029; 0003 too before the merges): no off rung exists; the PS3 runs their tests at
  rung 0, on. mesa-ctx-state-reset has no test: `NV30_CTX_STATE_KEEP=1` may make the RSX run a freed
  fragment program, which the validator can't see; a mesa-shim run first (a later round).
- mesa-cancel-recover (0053) on the PS3: its recovery needs a rejected push from a Mesa that has 0053,
  and every rung with 0053 also has 0052 (no X16 left to reject); its host part
  (`mesa-alloc-fail:fence-cancel`) is on the ladder.
- ps3rsx-no-switch-in-primitive: a latent race, shown by the host model in `validate-test.c`.
- ps3rsx-vswap-* and the out-of-GART Mesa fixes under real load: the game sessions and benchmarks run
  them on; their off state is a hung task or a crash, not a check.

<!-- BEGIN kernel-merges-2026-10-31 (written by the kernel merge agent; another agent is rewriting the rest of this file: keep this block whole) -->
## Kernel merges of 2026-10-31 (testset `build/testsets/kernel-merged/`, not committed yet)

The kernel folds of [MERGES.md](../gamedb/patches/MERGES.md) are a local `~dev` build (its `MANIFEST`
names the debs and their sha256). The patches and their register entries are committed only after this
check passes. One boot, the user present and not playing, the testset's kernel and its ps3rsx module
installed; the shipped kernel stays in the menu as the way back.

| Merge | What to check | Pass |
|---|---|---|
| kernel-gart-cma (local/0004 folded into 0007; `ps3rsx_gart` default 0, `ps3rsx_gart=0` gone from the `.args`) | `cat /proc/cmdline`: `ps3rsx_cma=64M`, no `ps3rsx_gart`. `dmesg \| grep -i 'ps3rsx'`: `ps3rsx: 64 MiB CMA area for the GART`, ps3rsx's probe line `GART 63 MiB (on demand)` (not `(reserved)`; one MiB is the scratch chunk), and no `ps3rsx gart: ... bytes at` line (that is the fixed reservation). `grep -E 'MemTotal\|MemAvailable\|CmaTotal\|CmaFree' /proc/meminfo` right after login, against the same lines on the shipped kernel in the same session (MemTotal equal, CmaTotal 65536 kB on both; a 64 MiB drop in MemTotal means the fixed reservation came back). | all as stated |
| the same, under use | `rsxdrm-smoke gart`, then one game (OpenTTD or Sauerbraten) for a few minutes: CmaFree falls while it runs and comes back within seconds after it exits; `sudo cat /sys/kernel/debug/dri/*/ps3rsx_mm` shows `GART backed by memory` rising with the game and falling after; no `no GART` and no `no memory to back the GART` | as stated, the game draws (webcam) |
| kernel-sm-temperatures + kernel-sm-shutdown-wait (0041's return value folded into 0040; the final source unchanged except one comment) | `sensors` or `cat /sys/class/hwmon/hwmon*/temp*_input` for `ps3_sm` (both zones read), then at the end `sudo poweroff` | temperatures read; the console goes off by itself (no "System Halted" hang) |
| fan policy (0042 on top, unchanged) | at boot `dmesg \| grep 'PS3 fan'`: SYSCON automatic | as on the shipped kernel |

The thermal checks of [THERMAL.md](THERMAL.md#ps3-checks-still-to-run) that 0040 and 0041 need are the
two rows above; the fold changes no code path that runs on the shipped kernel.
<!-- END kernel-merges-2026-10-31 -->
