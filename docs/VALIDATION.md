# PS3 validation of the stability patches

The stability entries of the patch register (`gamedb/patches/*/patch.md`, category `stability`) each
carry a minimal repro; the host ones run in `make test`. This page is the PS3 side: which repro runs on
the console, how its fix is turned off, what "off" should show, and the sessions they are grouped in.
A session's results go into each entry's Validation section (date, versions, off and on, a few lines of
dmesg) and a row in [TESTS.md](TESTS.md) History.

## Rules for every session

- The user is present and isn't playing. One PS3 agent at a time: take the PC-side lease first,
  `export PS3_LEASE=$(tools/ps3 lock --note "validation A")`; `make deploy` and `tools/play-test.sh`
  refuse while someone else holds it. The PS3-side bench lock stays as it is.
- The debs of the session (the shipped ones and each fix-off build) are copied to `build/testsets/<session>/`
  with a `VERSIONS` file; `make clean-ephemeral` leaves that folder alone.
- netconsole up (`make listen`) before anything that can wedge, or the dump read from the console's disk
  afterwards.
- webMAN fan control in SYSCON (automatic) mode, or a fixed speed of at least 60%, at every flag change.
- The user sets the boot flag and reboots; agents never flip it, reboot or kexec.
- `sync` right before a repro that can wedge. After a hard power cycle, only `fsck -n` on the unmounted
  image; repairs are the user's.
- Kernel-oops repros only after the panic-path fan restore passed (session B, boot 1); until then they are
  left out.
- No validator-off runs: a Mesa fix's "off" is the validator's rejection in dmesg, not a wedge
  (validate.c refuses X16 and UB256x3 whatever Mesa sends).
- The GameOS flag is back at the end of the session.

## Session A: the shipped stack, runtime switches (2 boots)

Boot A1, in this order (the ones that can wedge last):

| Patch | Repro on the PS3 | Fix off | Off should show | Wedge |
|---|---|---|---|---|
| kernel-idle-irq-pending | `ps3-waker -m probe -i 1.37 -t 120` | `echo 0 > /sys/module/kernel/parameters/ps3_idle_prep`, then 1 again | wakeups 100 ms or more late (up to about 1 s); on: worst under 5 ms | no |
| ps3rsx-validator-* (x16, ub256, vertex-type0, vertex-texture, thin-swizzled-rt, tex-offset, allowlist, submit-state) | `rsxdrm-smoke quirks`, `rsx-thin-test.sh`, then `rsx-suite.sh all` | none (no validator-off runs) | on: one `rejected push` line per refused input, then `egl-gears` still draws | no |
| mesa-thin-mip-levels, mesa-blit-source-alignment, mesa-texture-uploads-gart | `gl-genmip`, `gl-mipread`, `gl-texsub`, `egl-thin 1 64` | none (off wedges the RSX) | on: all pass, dmesg clean except the validator's line for `egl-thin 1 64` | no |
| kernel-vdso-chacha-elfv1, boost-context-save-fprs, luajit-low-2gb, sdl2-compat-close-locked-deadlock, webkit-*, plymouth-no-console-viewer | the host tests' programs on the PS3 (`getrandom()`, ctxtest, `luaL_newstate()` 100 times, close while locked on ALSA), Badwolf on a few pages and one calling `startViewTransition()`, the boot splash | none: the off builds break boot or the whole browser | on: no SIGILL, no failure, no hang | no |
| kernel-gelic-netpoll | `echo t > /proc/sysrq-trigger` with netconsole up | none (a kernel without it) | on: the whole dump at the listener, ssh still answers | no |
| kernel-snd-ps3-dma-watchdog | `speaker-test`, then `echo 1 > /sys/module/snd_ps3/parameters/dma_test_stall` | `echo 0 > .../dma_watchdog_ms`; set 200 again after | the hardware pointer stops and sound with it; on: restarted within 200 ms | sound (`sync` first) |
| mesa-no-x16 | mpv `--vo=gpu` on the shipped Mesa | the testset Mesa without 0052 (it sends X16 every frame; no reboot) | a `rejected push` line per frame for the X16 texture; on: none | low (`sync` first) |
| ps3rsx-rejected-submit-cancel, mesa-cancel-recover | the same mpv on the testset Mesa, then the shipped Mesa back | `echo 1 > /sys/module/ps3rsx/parameters/refuse_rejected`, or `NV30_PS3_RECOVER=0` for mpv | `kernel rejected pushbuf`, then -EIO on every submit: mpv draws nothing for good; on: `ps3rsx cancelled a submission`, mpv keeps running | low |

Boot A2, memory and GART pressure:

| Patch | Repro on the PS3 | Fix off | Off should show | Wedge |
|---|---|---|---|---|
| kernel-os-area-no-writes | passive, the whole session: no `FLASH ROM` update attempt in dmesg | never (it would write flash) | - | no |
| kernel-ps3disk-write-guard | a write-back outside the sealed ranges (GAMEOS-HDD.md, Phase B) | none | on: EIO, the image unchanged | no |
| mesa-mm-slab-oom, mesa-context-pushbuf, mesa-upload-null, mesa-alloc-fail, mesa-out-of-gart, mesa-query-report-steal | a Sauerbraten and a SuperTuxKart session to their end (out of GART) | none (host-tested both ways) | on: no segfault, no `out of GART` crash, the RSX still draws after the game exits | yes: the RSX stopped after an STK crash once |
| ps3rsx-vswap-bio-reserve, -bioset-bvecs, -own-stack | a memory hog with the desktop and a game drawing, until the VRAM tier is full and pages reach the disk tier, 5 minutes | none | on: no hung task in dmesg | yes (`sync` first, last of the boot) |

## Session B: kernel paths (3 boots, each a Petitboot boot by the user)

| Boot | Patch | Repro | Off should show | Wedge |
|---|---|---|---|---|
| B1 | the fan policy's panic path ([THERMAL.md](THERMAL.md#ps3-checks-still-to-run) step 9) | full speed (`pwm1_enable=0`), `sync`, `echo c > /proc/sysrq-trigger` | on: `PS3 fan: SYSCON automatic (panic)` on the console, the fan slows within 2 min; the user powers off | yes, on purpose; then `fsck -n` |
| B2 | kernel-sm-shutdown-wait | a test kernel without local/0041 from `build/testsets/B`: `sudo poweroff` | the console stays on until the power button is tapped; on (B3's last step): it goes off by itself | no |
| B3 | kernel-spufs-acquire-saved-race | the shipped kernel: two threads reading a running context's `regs` and `mem` in a loop (repro to write, kept in the repository) | fix-on only: no `BUG_ON`; the off kernel (an oops) only in a later session, after B1 passed | yes if the fix fails (`sync` first) |
| B3 | kernel-sm-shutdown-wait, on | after the GameOS flag is back: `sudo poweroff` | goes off by itself | no |

Session A needs 2 boots, session B 3; 5 in all.

## Left out, and why

- No PS3 repro: kernel-gelic-skb-alloc-fail (needs a fault-injection kernel), kernel-ps3-srr-clobber
  (a debug kernel with `CONFIG_PPC_RFI_SRR_DEBUG`), kernel-ps3-storage-cmd-timeout (a debug storage
  driver that drops completions: a risk to the disk), kernel-ps3disk-unwind (no probe failure on demand),
  kernel-spufs-fault-lock-order (lockdep), kernel-spufs-runqueue-ref (a race, KASAN stress not planned),
  pnmixer-no-card-crash (the PS3 always has a card).
- kernel-spufs-gang-alive: its repro would be a use after free reachable by a user; it stays out of the
  repository, and off the PS3 until the panic path passed.
- kernel-gelic-rx-watchdog: the off run (`ps3_gelic.rx_watchdog=0` with `rx_watchdog_test=1`) takes the
  network away with no way back over ssh; the on run was shown on kernel +ps3.747.
- ps3rsx-no-switch-in-primitive: a latent race, shown by the host model in `validate-test.c`.
- mesa-ctx-state-reset: `NV30_CTX_STATE_KEEP=1` may make the RSX run a freed fragment program, which the
  validator can't see; a mesa-shim run first (a later round).
- mesa-vbo-gpu-fetch: its stability part (no vertex type 0) has no Mesa switch; `rsxdrm-smoke quirks`
  covers the kernel side in A1.
