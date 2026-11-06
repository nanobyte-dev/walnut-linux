# Merge candidates in the patch series

A proposal for the user, written from the register (2026-10-31); the user approved every merge on
2026-10-31. Each chain says below what has been done; the rest is still in its series as before. The plan's rule holds: a merge waits until the series it touches
has been validated on the PS3 (a merge removes the old-path switches that validation uses), each chain is
approved on its own, and it waits as a local `~dev` build until its repros and host suites pass. The
patch that survives keeps its file name; the names it absorbs become aliases in its register entry.

Three kinds of candidate:

- **rework chains:** a later patch reworks or extends an earlier one (A then B, A then C);
- **settled switches:** a switch that brings back the old path, measured and decided, whose old path
  could go;
- **history-only splits:** one logical change in several files only because it was written in steps.

## Rework chains

### Kernel GART: local/0004 + local/0007

local/0004 (register entry `kernel-gart-reserve`) reserves a fixed GART at boot (`ps3rsx_gart=`), and
[kernel-gart-cma](kernel-gart-cma/patch.md) adds the CMA area beside it (`ps3rsx_cma=`) that ps3rsx backs
on demand. Both kernels' boot arguments say `ps3rsx_gart=0 ps3rsx_cma=64M`, so the fixed reservation is
only a fallback nobody boots.

- **Fold:** 0004 into 0007, as one "ps3rsx's GART from CMA" patch. Either drop `ps3rsx_gart=` (and the
  fixed path in ps3rsx's `gart.c`) or keep it with a default of 0.
- **Switches that go:** `ps3rsx_gart=` (or only its 64 MiB default).
- **Risk:** low for the kernel, medium in practice: 0004's code default is 64 MiB, so today a command line
  without `ps3rsx_gart=0` reserves 64 MiB for good. Dropping the fallback means ps3rsx needs a clear error when
  there is no CMA area. `local/0043` (ps3fb's shadow release) also takes chunks from ps3rsx's
  CMA area. One boot with the folded kernel, `rsxdrm-smoke gart` and a game.
- **Status (2026-11-01): folded, checked on the PS3** (session K, kernel .1210~dev; the game run under
  use still to do). 0004 is in 0007; `ps3rsx_gart=` stays (ps3rsx's
  `gart.c` reads the exported struct, so dropping it would need a ps3rsx change) with a default of 0, and
  the `.args` say `ps3rsx_cma=64M` alone. Checked as a local `~dev` kernel (`build/testsets/kernel-merged/`) in the boot of
  [VALIDATION.md](../../docs/VALIDATION.md) ("Kernel merges of 2026-10-31").

### System Manager: local/0040 -> 0041 -> 0042

[kernel-sm-shutdown-wait](kernel-sm-shutdown-wait/patch.md) (0041) changes the return value of the
temperature reply handler that [kernel-sm-temperatures](kernel-sm-temperatures/patch.md) (0040) added, and
[kernel-sm-fan-policy](kernel-sm-fan-policy/patch.md) (0042) builds on both.

- **Fold:** only that one return value, from 0041 into 0040. 0041 itself fixes an upstream bug and its
  upstream draft wants it as a patch of its own; 0042 is a separate feature.
- **Switches that go:** none.
- **Risk:** low, but the fan code is safety code: the fold needs the thermal checks of
  [THERMAL.md](../../docs/THERMAL.md) again.
- **Status (2026-11-01): folded, checked on the PS3** (both temperature zones read; `sudo poweroff`
  still to do), in the same local kernel as the GART fold: the
  final source is unchanged except one comment; 0040 alone now returns 0 for a temperature reply. Its
  checks are in the same VALIDATION.md section.

## Settled switches whose old path could go

| Switch | Patch | State | Proposal |
|---|---|---|---|
| `NV30_TRANSLATE_LUT` | [mesa-translate-lut](mesa-translate-lut/patch.md) (0033) | on by default (Sauerbraten 25.9 to 39.6 fps) | drop the off path |
| `NV30_TXB_HW` | [mesa-txb-as-tex](mesa-txb-as-tex/patch.md) (0045) | kept only for tests; the hardware TXB samples wrong | keep while an exact bias is still to be tried (TXD), then drop |
| `NV30_FENCE_BO_POOL`, `NV30_QUERY_WAIT` | [mesa-fence-bo-pool](mesa-fence-bo-pool/patch.md) (0034), [mesa-query-wait](mesa-query-wait/patch.md) (0035) | measured neutral, off | drop candidates: the keep-or-drop re-test in [TESTS.md](../../docs/TESTS.md) decides |
| `ps3_idle_prep` | [kernel-idle-irq-pending](kernel-idle-irq-pending/patch.md) (local/0009) | the fix has been on in every kernel since .472 | keep the switch until the patch goes upstream (it is the A/B for the upstream case), then drop |
| `refuse_rejected` | [ps3rsx-rejected-submit-cancel](ps3rsx-rejected-submit-cancel/patch.md) | the old refuse rule, for clients that don't recover | keep: it is the fallback for any client without `PS3_RECOVER` |

## History-only splits

| Files | Register entry | Proposal | Risk |
|---|---|---|---|
| Mesa 0030, 0031 | [mesa-index-upload](mesa-index-upload/patch.md), [mesa-fp-const-stream](mesa-fp-const-stream/patch.md) | keep apart: both use the stream upload buffer but in different paths, and `NV30_FP_CONST_VRAM` is the fallback for GART fragment programs | no merge |
| Mesa 0050, 0051 | [mesa-point-smooth](mesa-point-smooth/patch.md), [mesa-point-size](mesa-point-size/patch.md) | keep apart: different mechanisms (a fragment program variant, a rasterizer bit), and 0051 depends on ps3rsx's `PS3_ABI` 2; each is upstreamable alone | no merge |
| gdp 0004, 0014, 0018 | [gdp-no-launchers](gdp-no-launchers/patch.md) | done 2026-10-31: one `--no-launchers` patch, 0004 (the Doom family, GOG icons, ScummVM games), which is also the shape Debian would want; the tree it makes is unchanged | low; the gdp host tests cover 0004 and 0018 |
| kernel-ps3 0110+0314, 0440+0450 (0070+0420 dropped 2026-11-05, round 3 review U1) | [kernel-ps3-repository-lparid](kernel-ps3-repository-lparid/patch.md), [kernel-ps3-srr-clobber](kernel-ps3-srr-clobber/patch.md) | leave the files as they are: they are the psx-place set's, and keeping them 1:1 with it makes later refreshes and the credit simple; the register already groups them | no merge |
| SPU gcc 0001+0002, newlib 0001-0007 | [spu-gcc-backend](spu-gcc-backend/patch.md), [spu-newlib-4-6](spu-newlib-4-6/patch.md) | keep: the transplant is reviewable against GCC 9.5 only as its own file | no merge |
