# Merge candidates in the patch series

A proposal for the user, written from the register (2026-10-31). Nothing here has been changed: every
patch is still in its series as before. The plan's rule holds: a merge waits until the series it touches
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

[kernel-gart-reserve](kernel-gart-reserve/patch.md) reserves a fixed GART at boot (`ps3rsx_gart=`), and
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

### Mesa staging pools: 0034, 0036, 0040

[mesa-staging-pool](mesa-staging-pool/patch.md) (0036, `NV30_STAGING_POOL`) keeps idle mapped staging
buffers up to 256 KiB, and [mesa-staging-pool-big](mesa-staging-pool-big/patch.md) (0040,
`NV30_STAGING_POOL_MB`) extends the same pool to bigger uploads, with its policy in
`nv30_staging_policy.h`. [mesa-fence-bo-pool](mesa-fence-bo-pool/patch.md) (0034, `NV30_FENCE_BO_POOL`) is the
same idea for the fences' 4 KiB wait buffers. [mesa-out-of-gart](mesa-out-of-gart/patch.md) (0049) drains the
staging pools when GART runs out.

- **Fold:** 0036 into 0040, one staging pool with one size switch; 0034 stays a patch of its own (other
  objects, other code) unless it is dropped.
- **Switches that go:** `NV30_STAGING_POOL` merges into `NV30_STAGING_POOL_MB` (or the reverse). Measured:
  0040 took OpenTTD from 34 to 52 fps (8bpp) and 11 to 20 fps (32bpp-anim), with no decision recorded to
  turn it on; 0036 and 0034 measured neutral and stay off. Deciding 0040's default comes before the fold.
- **Risk:** medium: the pools hand out buffers the GPU may still read if the fence logic is wrong. The
  host test `mesa-staging-pool` covers 0040's policy only; it should cover the small classes too before
  the fold.

### Mesa submission cadence: 0019 -> 0026 -> 0027

Without relocations ([mesa-no-relocations](mesa-no-relocations/patch.md), 0019) nothing submitted mid-frame.
[mesa-kick-draws](mesa-kick-draws/patch.md) (0026) added an opt-in submission every N draws, and
[mesa-kick-auto](mesa-kick-auto/patch.md) (0027) replaced it as the default with a submission when the GPU
has caught up; `NV30_PS3_KICK_DRAWS` now only overrides 0027.

- **Fold:** 0026 into 0027.
- **Switches that go:** `NV30_PS3_KICK_DRAWS` could go (a fixed `kick100` was rejected: it cost X-Moto 9%).
  `NV30_PS3_RELOCS` stays until a PS3 check, since it is the only way back from 0019.
- **Risk:** low; the command stream is the same, only cut differently.

### Mesa out-of-GART robustness: 0002, 0003, 0047, 0048, 0049

[mesa-mm-slab-oom](mesa-mm-slab-oom/patch.md) (0002) returns NULL when no slab can be had, and
[mesa-alloc-fail](mesa-alloc-fail/patch.md) (0048) builds on exactly that spot (a buffer of the request's own
size when no slab fits). [mesa-context-pushbuf](mesa-context-pushbuf/patch.md) (0003) counts a context's
staging in flight, which [mesa-out-of-gart](mesa-out-of-gart/patch.md) (0049) reworks.
[mesa-upload-null](mesa-upload-null/patch.md) (0047) is in other files (u_upload, the state tracker).

- **Fold:** 0002 into 0048. 0003's push buffer fixes stay; its staging counter could move into 0049.
  0047 stays separate: it is generic Gallium code, the part most worth offering upstream on its own.
- **Switches that go:** none (none of these has one).
- **Risk:** low; the host tests `alloc-fail-test.c` and `transfer-cpu-test.c` (`mesa-query-steal`)
  cover the merged paths.

### Mesa vertex formats: 0022 and 0029

[mesa-vtxfmt-cache](mesa-vtxfmt-cache/patch.md) (0022) forgets the cached vertex formats when the context
that drew last is destroyed; [mesa-ctx-state-reset](mesa-ctx-state-reset/patch.md) (0029) generalised that
to the whole cached state.

- **Fold:** move 0022's "forget on destroy" part into 0029; 0022 keeps the cache itself.
- **Switches that go:** none (`NV30_VTXFMT_ALWAYS` is below, `NV30_CTX_STATE_KEEP` stays a debugging aid).
- **Risk:** low.

### System Manager: local/0040 -> 0041 -> 0042

[kernel-sm-shutdown-wait](kernel-sm-shutdown-wait/patch.md) (0041) changes the return value of the
temperature reply handler that [kernel-sm-temperatures](kernel-sm-temperatures/patch.md) (0040) added, and
[kernel-sm-fan-policy](kernel-sm-fan-policy/patch.md) (0042) builds on both.

- **Fold:** only that one return value, from 0041 into 0040. 0041 itself fixes an upstream bug and its
  upstream draft wants it as a patch of its own; 0042 is a separate feature.
- **Switches that go:** none.
- **Risk:** low, but the fan code is safety code: the fold needs the thermal checks of
  [THERMAL.md](../../docs/THERMAL.md) again.

## Settled switches whose old path could go

| Switch | Patch | State | Proposal |
|---|---|---|---|
| `NV30_TEX_INVALIDATE_ALWAYS`, `NV30_VTX_INVALIDATE_DRAW`, `NV30_VTXFMT_ALWAYS`, `NV30_CONSTBUF_ALLOC`, `NV30_TEXUNIT_ALWAYS`, `NV30_VP_CONST_SINGLE` | 0020 to 0025 | the new paths have been the default since ps3.262 (2026-10-17) in every game | drop all but `NV30_TEX_INVALIDATE_ALWAYS`, which 0020 names as the stale-texture diagnostic; then fold the six patches into one "less per draw" patch (below) |
| `NV30_TRANSLATE_LUT` | [mesa-translate-lut](mesa-translate-lut/patch.md) (0033) | on by default (Sauerbraten 25.9 to 39.6 fps) | drop the off path |
| `NV30_TXB_HW` | [mesa-txb-as-tex](mesa-txb-as-tex/patch.md) (0045) | kept only for tests; the hardware TXB samples wrong | keep while an exact bias is still to be tried (TXD), then drop |
| `NV30_PS3_KICK_DRAWS` | 0026 | overridden by 0027 | see the cadence chain above |
| `NV30_FENCE_WORK_KICK`, `NV30_QUERY_KICK` | [mesa-fence-work-kick](mesa-fence-work-kick/patch.md) (0041), [mesa-query-kick](mesa-query-kick/patch.md) (0042) | both measured neutral and off; HOTPATHS.md says to drop 0042 unless another game gains | drop candidates, not merges: the keep-or-drop re-test in [TESTS.md](../../docs/TESTS.md) decides |
| `NV30_FENCE_BO_POOL`, `NV30_STAGING_POOL`, `NV30_QUERY_WAIT` | 0034, 0036, 0035 | measured neutral, off | the same keep-or-drop re-test |
| `ps3_idle_prep` | [kernel-idle-irq-pending](kernel-idle-irq-pending/patch.md) (local/0009) | the fix has been on in every kernel since .472 | keep the switch until the patch goes upstream (it is the A/B for the upstream case), then drop |
| `refuse_rejected` | [ps3rsx-rejected-submit-cancel](ps3rsx-rejected-submit-cancel/patch.md) | the old refuse rule, for clients that don't recover | keep: it is the fallback for any client without `PS3_RECOVER` |

## History-only splits

| Files | Register entry | Proposal | Risk |
|---|---|---|---|
| Mesa 0015, 0016, 0017 | [mesa-fence-ref-unchanged](mesa-fence-ref-unchanged/patch.md), [mesa-validate-fence-unchanged](mesa-validate-fence-unchanged/patch.md), [mesa-winsys-kref-once](mesa-winsys-kref-once/patch.md) | one "fewer fence and kref operations" patch: one round (ps3.246), no switches, the same reference counts and command stream | low |
| Mesa 0020 to 0025 | mesa-tex-invalidate to mesa-vp-const-burst | one "less per draw" patch after the switches above go; 0020 could stay apart if its diagnostic switch stays | medium: six changes to the command stream in one file. They were measured together (Mesa ps3.262: every suite game 11-24% faster except Armagetron, whose loss was 0019's), 0020 alone once more in the repeats |
| Mesa 0011, 0032 | [mesa-s3tc-libspeq](mesa-s3tc-libspeq/patch.md) | one patch: DXT1 through libspeq, then DXT5 through the same hook; the switches are libspeq's | low |
| Mesa 0030, 0031 | [mesa-index-upload](mesa-index-upload/patch.md), [mesa-fp-const-stream](mesa-fp-const-stream/patch.md) | keep apart: both use the stream upload buffer but in different paths, and `NV30_FP_CONST_VRAM` is the fallback for GART fragment programs | no merge |
| Mesa 0050, 0051 | [mesa-point-smooth](mesa-point-smooth/patch.md), [mesa-point-size](mesa-point-size/patch.md) | keep apart: different mechanisms (a fragment program variant, a rasterizer bit), and 0051 depends on ps3rsx's `PS3_ABI` 2; each is upstreamable alone | no merge |
| Mesa 0041, 0042 | see above | drop candidates, not a merge | |
| gdp 0004, 0014, 0018 | [gdp-no-launchers](gdp-no-launchers/patch.md) | one `--no-launchers` patch (the Doom family, GOG icons, ScummVM games), which is also the shape Debian would want | low; the gdp host tests cover 0004 and 0018 |
| kernel-ps3 0070+0420, 0110+0314, 0440+0450 | [kernel-ps3-avmgr-device](kernel-ps3-avmgr-device/patch.md), [kernel-ps3-repository-lparid](kernel-ps3-repository-lparid/patch.md), [kernel-ps3-srr-clobber](kernel-ps3-srr-clobber/patch.md) | leave the files as they are: they are the psx-place set's, and keeping them 1:1 with it makes later refreshes and the credit simple; the register already groups them | no merge |
| SPU gcc 0001+0002, newlib 0001-0007 | [spu-gcc-backend](spu-gcc-backend/patch.md), [spu-newlib-4-6](spu-newlib-4-6/patch.md) | keep: the transplant is reviewable against GCC 9.5 only as its own file | no merge |
