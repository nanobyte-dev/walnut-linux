# Release candidates: what could go from ps3-dev to the stable `ps3` suite

Drafted 2026-10-18 at HEAD `0149148`, for the user to approve in one decision. Nothing was
published, built or run on the PS3 for this list. The rule (memory "Publish only when proven"):
stable only after a package is proven on the PS3, an explicit `DEBS=` list, `build/aptly` backed up
first, never a `~dev` version. Promotion (PACKAGING.md): commit, rebuild the official version
(`0.N`, `...+ps3.N`, no `~dev`), `make publish`.

Sources: the served indices of `http://10.0.0.10:8070/{ps3,ps3-dev,ps3-licensed}` (fetched
2026-10-18), `build/debs`, `git log --since=2026-10-16` (277 commits), GAME-PLAN.md, HOTPATHS.md,
MEMORY.md, STALLS.md, DESKTOP-PLAN.md, and `games/bench/results/*.csv`. "fps.csv row N" is the
Nth data row of the matrices (same rows in every CSV; the `observations` column has the text).
Every row cited as proof has the picture checked on the webcam unless said otherwise.

## 1. What is where

### Stable `ps3` now (everything dates from 2026-10-16 morning, builds up to .163)

| Package | Stable | Latest in ps3-dev | Latest built locally / would build now |
|---|---|---|---|
| kernel (`linux-image-*`, headers, kbuild, metas) | 6.12.111-1+ps3.152 | .433~dev (also .178, .402, .419) | .433~dev; source now builds .435 |
| `ps3rsx-modules-*` | 0.152+k.152 | 0.249+k.178 / .402 / .419 / .433~dev | 0.249+k.433~dev |
| mesa set (13 binaries) | 26.2.4-1+ps3.152 | .408~dev (official .407) | .408~dev; source now .415 |
| `ps3-gpu-tools` | 0.152 | 0.412 | 0.412; source now 0.428 |
| `ps3-spu`, `ps3-spu-programs` | 0.152 | 0.394 (with `libspeq0`, `libspeq-dev`, which stable lacks) | 0.394; source now 0.395 |
| `ps3-boot` | 0.152 | 0.176 | 0.176; source now 0.247 |
| `ps3-system` | 0.160 | 0.419 | 0.419 (matches source) |
| `ps3-desktop`, `ps3-apps` | 0.160 | 0.375 (and 0.376~dev) | 0.376~dev; source now 0.406 |
| `ps3-games` | not in stable | 0.375 (0.376~dev) | 0.376~dev; source now 0.406 |
| `classicube` | 1.3.8~git20261014.aa47929-1+ps3.152 (bundled Mojang data) | .367~dev | .367~dev; source now .374 |
| `quake-shareware`, `quake2-demo-data` | 89 | none | 89 |
| plymouth, `ps3-splash`, `ps3-no-initramfs` | not in stable | 26.134.222-1+ps3.182, 0.182 | same; source now .247 / 0.247 |
| `libsdl3`, `luajit`, `luanti`, `cube2`, `ioquake3`, `yamagi-quake2`, `dsda-doom`, `extremetuxracer`, `xmoto`, `neverball`, `openttd`, `fluidsynth`, `openal-soft`, `supertuxkart`, `teeworlds` | none of them | see section 2 | see section 2 |

Also in stable, legacy and untouched: `ps3-mesa` 26.2.4+ps3.99999, `ps3-kernel-6.12.112-ps3-debug-*`,
`ps3rsx-6.12.112-*` (old pipeline). Leave them.

### `ps3-licensed` now (add-only, already published)

`classicube-data` 1.3.8~git20261014.aa47929-1+ps3.366, `quake-shareware` 89, `quake2-demo-data` 89,
`sauerbraten-nonfree-data` 0.0.20201229-1+ps3.388, `ps3-games-licensed` 0.388 (and 0.374).
Nothing new is waiting there. The catch: `ps3-games-licensed` Depends on `ps3-games`, which is not
in stable, so on the PS3 it installs only with `-t ps3-dev` until `ps3-games` is promoted.

### Rebuild status of `build/debs`

`build/debs` already holds official (non-`~dev`) builds of: libsdl3 254, luajit 247, luanti 405, cube2 354,
ioquake3 396, yquake2 415, dsda-doom 361, etr 324, xmoto 361, neverball 370, openttd 297, fluidsynth 309,
openal-soft 273, supertuxkart 361, teeworlds 422, ps3-system 419, ps3-games-licensed 388,
sauerbraten-nonfree-data 388, quake-data 89. These need no rebuild.

`build/debs` holds only `~dev` builds of: mesa (408~dev), classicube (367~dev), ps3-desktop/ps3-apps/ps3-games
(376~dev), linux and ps3rsx-modules (433~dev). `make publish` refuses those; rebuild first.

Stale (built before a later commit of the package's directory or of the shared packaging tools, so a
rebuild gives a higher version): ps3-gpu-tools 0.412 (now 428), ps3-spu 0.394 (395), ps3-boot 0.176,
plymouth 182 and ps3-splash 0.182 (all three 247, since `a148c56` touched `tools/deb-build.sh`).
N depends on the last commit touching the package or `tools/deb-*.sh`/`docker/Dockerfile.deb`, so
**any commit to those tools before the rebuild shifts every number below**. This file is docs only
and does not.

## 2. Verdicts

READY: proven on the PS3, no known regression. NEEDS-CHECK: one named check first. NOT YET: do not
publish. "Proven" below means a same-boot run on the PS3 with the picture seen on the webcam; where a
stable-worthy number exists it is cited. The 60 fps target (memory "60 fps target") is a performance
goal, not a stability gate; it is listed in the notes so the user can still say no.

### 2.1 Kernel and driver (go together)

| Item | Verdict | Evidence and gaps |
|---|---|---|
| Kernel `6.12.111-1+ps3.178` (release flavour, with headers, `-common`, kbuild, metas `linux-image-ps3`, `linux-headers-ps3`) plus `ps3rsx-modules-6.12.111+ps3.178` and meta `ps3rsx-modules-ps3` at `0.249+k6.12.111+ps3.178` | READY | The PS3 has run this kernel with ps3rsx 0.249 for the whole 2026-10-17/18 work: every fps.csv row from 35 ("ps3rsx 0.249 first boot") to 62 (HOTPATHS.md names kernel .178 for the profiling batch; THERMAL.md "Confirmed on the PS3 (2026-10-18, kernel .178)"). 0.249: GL tests pass, VT switch works with a GL client, no Neverball wedge (`1c1b09a`). Files already exist as official versions in `build/aptly/pool` (dev suite), so no rebuild and no new bits. Open: which flavour (release or debug) the user booted is not recorded in the docs; this list assumes release (the image default). The debug flavour 178 files exist the same way if wanted. Not verified: the splash and DPMS changes of `f30b505` were "tested on hardware" only by the PS3 working on the later boots, the log has no explicit user confirmation (see ps3-splash). |
| ps3rsx 0.249 alone (as `0.249+k` for 402/419/433) | NOT YET | Those module builds pair with unbooted kernels. |

Known gap of .178 that stays in stable if published: the lost-decrementer stalls (fixed only in the
unbooted .402+). Games run with the 10 ms kicker from `ps3-waker` (bench) for now; see 2.6.

### 2.2 Test kernels (separate: NOT YET until the user's reboot session)

| Kernel | What is in it | Verdict |
|---|---|---|
| .402 (dev, official version) | local/0009, idle without `lv1_pause` while an interrupt is pending (lost decrementer fix), `ps3_idle_prep` off switch (`b303e08`, `5de5764`). STALLS.md: "built, not yet booted". | NOT YET. Needs: boot, probe test 1 shows no late wakeups, OpenArena `stall_play_s` about 7 s or less with `-` / `STALLS=1` / kick equal, watch fan and `uptime` (idle threads could spin hotter). |
| .419 (dev, official version) | .402 plus local/0020-0032 survey patches (gelic RX, spufs fixes), local/0040 hwmon `ps3_sm` temperatures, UFS_FS=m read-only (`72b7be6`), local/0060 `/dev/ps3sysmngr` still present. "Nothing is booted yet" (OTHEROS-PATCHES.md). | NOT YET. Superseded by .433+. |
| .433~dev (built, dirty), source now .435 | .419 minus the removed patches (0060, 0090, 0170/0160/0316, `PS3_DISP_MANAGER` off, `3bd717c`), plus local/0041 (final shutdown poll, `bf2a6fe`) and local/0042 (fan policy through the System Manager: SYSCON automatic at boot and on every exit path, `ec834df`). Compile-tested only; the `get` reply layout is a guess confirmed only at boot; the first boot should use `fan_policy=keep` (THERMAL.md). | NOT YET. Needs the user's reboot session: boot with `keep`, then `auto`, confirm temperatures, fan state, power off and reboot paths. Never mix with a publish: rebuild as official only after that session (`make linux-deb`, then `make ps3rsx-modules`). |

### 2.3 Graphics stack and SPU

| Package | Stable -> candidate | Verdict | Evidence, gaps |
|---|---|---|---|
| mesa set (libegl-mesa0, libegl1-mesa-dev, libgbm-dev, libgbm1, libgl1-mesa-dev, libgl1-mesa-dri, libgles2-mesa-dev, libglx-mesa0, mesa-common-dev, mesa-drm-shim, mesa-libgallium, mesa-va-drivers, mesa-vdpau-drivers) | 26.2.4-1+ps3.152 -> **+ps3.415** (rebuild) | READY | Patches 0012-0034 over stable. L8A8/mipmap/offset fixes proven by gl-texfmt, gl-genmip, gl-mipread PASS (fps.csv rows 18, 22; Neverball no longer wedges). Per-draw rounds: ps3.262 OpenArena +23%, Yamagi +24%, X-Moto +21%, ETR +15% (row 38); ps3.299 clean same-boot A/B against .282 (rows 47, 48); `NV30_TRANSLATE_LUT` default: Sauerbraten 25.9 -> 39.6 steady (+53%, row 57), QuakeSpasm neutral (row 58). Mesa 408~dev (which includes the LUT default of `d7e057a`) ran in rows 59-62 with the picture right. The rebuilt .415 is the same source tree as that 408~dev (the only commit after the build is the one that made it clean); the contents should match apart from the version, but this has not been diffed. Known costs, accepted: X-Moto -5% on .299 vs .282 (row 47), ClassiCube -4% with the game-side batching (row 61), Armagetron's ps3.262 loss fixed by 0027. Should go with ps3rsx-modules 0.249 (assumption: 0027-0031 use GART index and fragment-program fetches and were only ever run on 0.249; no old-module run exists). Optional dlopen of libspeq: publish with libspeq0 so DXT5/DXT1 go to the SPEs. 0034 (`NV30_FENCE_BO_POOL`) and 0033's off switch are measured neutral. |
| `libspeq0`, `libspeq-dev`, `ps3-spu`, `ps3-spu-programs` | 0.152 (no libspeq0) -> **0.395** (rebuild; 0.394 is the proven build, 0.395 adds only `ps3-spu/doc/dxt-rgb.md`, `18d2693`) | READY | Ship all four together (the SPU programs must match libspeq0). Proof: DXT1/DXT5 on the SPEs byte for byte against the reference (`speq-dxt --check`, 25 sizes, big-endian run in the chroot); `speq-bench` probes (rows 6, 7); RGB DXT 1024x1024 1.0 ms on 4 SPEs vs about 175 ms on the PPE (row 59); fluidsynth underruns 1231 -> 588 and 1331 -> 709 with the user's ear "night and day" (row 50); shared 2-SPE deadline pool 465 vs 1095 underruns, 0 timeouts (row 52); Sauerbraten skinning on the SPEs measured (row 54: the SPU path adds hitches, PPE path 28.1, cube2 defaults to CPU skinning). Gap: the SPU RGB-DXT1 path is exercised by no game (Sauerbraten's demo uploads none, row 59), covered by the unit test only. |
| `ps3-gpu-tools` | 0.152 -> **0.428** (rebuild) | READY | 0.412 ran on the PS3 (stall recorder `ps3-stallrec`, `ps3-waker`, `ps3-malloctrim.so`, `perf-post.sh` fixes: rows 55-62, docs/STALLS.md "Confirmed on the PS3"). 0.428 = 0.412 plus `ps3-sm-temp.sh` and docs (`b5b0cad`, `3fd288b`); the new script is run-tested only on the kernels that expose the System Manager temperatures (not booted), harmless without them. |
| `libsdl3` | none -> **3.4.18+ds-1+ps3.254** (as built) | READY | Native BGRA upload on big-endian: OpenTTD about 2 -> 8 fps with right colours (row 35), dsda-doom colours right, Chocolate/Crispy/Woof smoke runs right (GAME-PLAN smoke table, 2026-10-18). Pin 990 makes it win over Debian's libsdl3. |
| `libluajit-5.1-*`, `luajit` | none -> **2.1.0+openresty20251030-1+ps3.247** (as built) | READY | Fixes the `luaL_newstate` failure on ppc64 (about half of the starts); Luanti runs on it in every row from 36 on. Goes with luanti. |
| `openal-soft` | none | NOT NEEDED | Debian's source unpatched, built only to get matching debug symbols (`5d43987`). Publishing it would just shadow Debian's libopenal1 with an identical rebuild. Skip. |

### 2.4 Games

All of these are ps3-dev builds without a stable version; publishing them brings the dev package into
the stable suite. "Perf" is the steady fps on the PS3 against the 60 fps target.

| Package | Candidate | Verdict | Evidence, gaps |
|---|---|---|---|
| `ioquake3`, `ioquake3-server` | 1.36+u20260719...+ps3.396 | READY | OpenArena: opengl2 memory patches 14.8 -> 26-32 fps and longest stall 24.7 -> 6 s (row 46); batching 0020-0022 neutral, atlas (0019) off by default after it cost about 10 s of hitches (rows 51, 53); `r_keepImages` + `cl_preloadLate` (0025, 0026): gameplay stalls 9-10 s -> 0, longest 1 s -> 50-60 ms, steady 65 -> 70.6 fps, same boot, webcam right (row 59, `2d3243c`). Perf: 70 fps opengl1; opengl2 53 fps (row 61). |
| `yamagi-quake2`, `yamagi-quake2-core` | 8.70+dfsg-1+ps3.415 (as built) | READY | Sound fix (+ps3.240, user confirmed, row 31), 63.9 fps (row 38); `YQ2_KISS_QSIZE` 4096 default: RSS 125 -> 56 MB, swap 80 -> 9 MB, gameplay stalls 22.6 -> 0.05 s, steady 65.7 -> 68.7 (row 57, run as variant kiss4k on .408; .415 makes the same value the default, so .415 itself was not run, equivalence by the env switch). Perf meets 60. |
| `dsda-doom` | 0.30.0+dfsg-1+ps3.361 | READY | Colour and GL fixes (row 28), GL batching: steady 55.9 vs 54.2 off, low1 40.8 vs 36.5 (row 61); with fluidsynth on the SPEs. Music still chops somewhat: 465 underruns a minute on the SPE pool vs 1095 on the PPE (row 52). Perf under 60 (40-56). |
| `fluidsynth`, `libfluidsynth3`, `libfluidsynth-dev` | 2.6.1+dfsg-1+ps3.309 | READY | Voice DSP on the SPEs through libspeq: CPU 44.5 -> 31.8 s a minute, underruns halved, the user listened (row 50); needs `libspeq0` >= 0.367 for the pool (row 52). Falls back to the PPE path without it. |
| `extremetuxracer`, `extremetuxracer-data` | 0.8.4-2+ps3.324 | READY | Game-side batching 19,400 -> 850 GL calls; race replay 52 -> 59, picture identical on the webcam (row 49). |
| `neverball`, `neverputt`, `-data`, `-common` | 1.6.0+git20180603-3+ps3.370 | READY | Batching: CPU 80 -> 65 s, smooth fps 79.1 vs 78.7, stalls similar (row 54); no RSX wedge on ps3rsx 0.249 (row 35). Perf meets 60. |
| `luanti`, `luanti-data`, `luanti-server` | 5.15.2+dfsg-2+ps3.405 | READY | Builds on luajit .247. Batching stalls 6.25 vs 9.1 s, longest 1.7 vs 3.4 s, no underruns (row 54); Ogg Vorbis byte-order fix (sounds were noise, `7b5b968`). 405 only adds `LU_TRIM_MB` with the old default (trim16 measured and rejected, row 57). Perf 45-47 steady, memory stalls remain. |
| `cube2`, `cube2-server` (Sauerbraten) | 0.0.20201229+dfsg-1+ps3.354 | READY | Without our memory patches it was OOM-killed; steady 25.9 -> 39.6 with Mesa's LUT (row 57), picture right. CPU skinning is the default; the SPU skinning hook is selectable only (row 54). Models and sounds need `sauerbraten-nonfree-data` (ps3-licensed). Perf 39 steady, under 60. |
| `openttd`, `openttd-data` | 15.1-1+ps3.297 | READY | Software video works (right colours with SDL3 .254); the new OpenGL driver: right picture on the webcam, 21 fps vs 8-12 software (rows 58, 61). The software drawing is "far under" 60. |
| `xmoto`, `xmoto-data` | 0.6.3+repack-1+ps3.361 | NEEDS-CHECK | The batching (+ps3.323) is proven: 44.5 -> 166.5 fps, picture identical (row 49). The .361 build adds only the env-gated `XMOTO_BENCH_LOOPS` patch (`f7edfa5`) and was not run on the PS3 (no X-Moto row after 49). Check: one `run.sh smoke xmoto` (or bench base) on the built .361 and the webcam; or publish the proven .323 from `build/aptly/pool` instead. |
| `classicube` (engine) | 1.3.8~git20261014.aa47929-1+ps3.**374** (rebuild) | NEEDS-CHECK | Batching 0001-0002 (`02e71ba`) run in row 61: base 47.3 vs gloff 49.4 fps steady (-4%, single pair), low1 31 vs 27.6, `NV30_FENCE_BO_POOL` neutral; the picture right. Check 1: decide if -4% steady for better lows is acceptable, or repeat the same-boot A/B with 3 rounds. Check 2 (the split): the upgrade from the bundled stable .152 (`classicube` files move to `classicube-data` in ps3-licensed) was checked with dpkg in the chroot only (PACKAGING.md), not on the PS3; run `make deploy PKGS="ps3-system classicube"` on the PS3 with a bundled version installed. `classicube-data` .366 is already in ps3-licensed, so publish only the engine deb (not `classicube-data` .374: a new 15 MB copy of the same files). |
| `supertuxkart`, `supertuxkart-data` | 1.5-2+ps3.361 | NEEDS-CHECK | Loads, karts and track draw (big-endian .spm fixes, webcam, row 61): `lean` 21.4 smooth fps vs 13 default, stalls 2.4 vs 13.5 s; "out of VRAM" lines remain; sound not recorded. Check: decide whether `lean` becomes the package default (it is the only playable setting), then one run with sound audible (`tools/webcam-audio.sh`) and no kernel errors. |
| `teeworlds`, `teeworlds-server`, `teeworlds-data` | 0.7.5-3+ps3.422 | NEEDS-CHECK | The big-endian `FIONBIO` fix (`840a3b5`) is the whole change: in game on dm1, 183 fps, one smoke run (row 62). Debian's package cannot connect on ppc64, so there is no regression risk. Check: sound audible and a second run; DXT5 path through libspeq (no `SPEQ` log line seen). Smoke-level only, not benchmarked. |

### 2.5 Desktop, system, boot

These share one source or one apt contract; promote together or not at all.

| Package | Candidate | Verdict | Evidence, gaps |
|---|---|---|---|
| `ps3-system` | 0.419 (as built) | NEEDS-CHECK | Contents since stable .160: `ps3-licensed` apt source (0.366), `ps3-kick` (10 ms kicker, `a46f930`), `ps3-gameos-mount` (`72b7be6`). The kicker as started by game mode and the GameOS mount (needs UFS_FS=m, in the unbooted kernel) have never run on the PS3. The bench-side `ps3-waker` kick is what rows 56-62 measured. Check: on the PS3, `ps3-game-kick` unit starts and stops with a game (`ps3-desktop/tests/kick-check.sh` is host-side only); `apt update` sees `ps3-licensed`. |
| `ps3-desktop`, `ps3-apps`, `ps3-games` | **0.406** (rebuild; built 0.376~dev) | NEEDS-CHECK | Game mode unloads the wallpaper, freezes services and brings the desktop back; one PS3 report exists for leaving (about 30 s, prompting the file prefetch of `a2e34fe`), the prefetch, the automatic `ps3-game-detect` and `gamemode-check.sh` are "not yet run on the PS3" (DESKTOP-PLAN.md, 2026-10-17). The Games menu entries per mode (`ebf96d4`) and the post-split `ps3-games` (`60a18e4`) are also unrun. `ps3-games` Depends on classicube, yamagi-quake2, neverball, xmoto, etr, dsda-doom, openttd and Debian's openarena/quakespasm/armagetronad/freedoom: promote those first. Check (user at the PS3): start a game from the menu and a fullscreen GL program without the menu, quit, time the desktop's return, `ps3-game-mode` journal has `left game mode`, the wallpaper and applets are back. |
| `ps3-boot` | 0.176 as built (rebuild gives 0.247) | NEEDS-CHECK | Stable 0.152 mishandles the `release`/`debug` flavour names (IMAGE-DIFF.md); 0.176 fixes it. No record of `/etc/kboot.conf` written by 0.176 on the PS3. Check: `ps3-update-kboot` on the PS3 gives the expected default entry; or build image and read it. |
| plymouth set, `ps3-splash`, `ps3-no-initramfs` | 26.134.222-1+ps3.182 / 0.182 (rebuild gives .247) | NEEDS-CHECK | The splash was written and fixed on 2026-10-16 with deploy fixes (`71d9264`); PLAN.md still lists splash timing and the DPMS fix as "not tested on hardware" for the .178 test boot, and no later entry confirms it. Check: the user reports one cold boot on the release kernel (splash shows from Petitboot to the desktop, no black gap, no text). |

### 2.6 Not promoted, and things to know

- `ps3rsx-modules-ps3-debug`, the debug kernel flavours, `*-dbgsym`: dev only (dbgsym goes to `ps3-dbgsym`).
- Old dev builds below the current ones are irrelevant (pruned after 5).
- The stall fix for games exists in three places, none in stable-ready form: the kernel local/0009 (.402+, not booted),
  the `ps3-kick` in ps3-system (not run through game mode), and bench-only `ps3-waker`. Games run today with the kicker
  started by hand, so stable users without it will see the 1+ s hitches (STALLS.md).
- Packages that must go together: kernel .178 + `ps3rsx-modules` 0.249+k.178; Mesa set + `ps3rsx-modules` 0.249
  (+ `libspeq0`/`ps3-spu-programs` for the SPE paths); `libspeq0` + `ps3-spu-programs` + `ps3-spu` + `libspeq-dev`;
  `ps3-desktop` + `ps3-apps` + `ps3-games` (one source) after `ps3-system` (>= 0.367~, which ps3-desktop Depends on);
  `luanti` + `luajit`; `ps3-games` after all its games; `ps3-games-licensed` (already in ps3-licensed) after `ps3-games`.
- Classicube engine/data split: engine in `ps3`, data stays at .366 in `ps3-licensed`.
- Debian packages the games need (openarena, quakespasm, armagetronad, freedoom, openttd data sets, mpv, ...) are
  not in this list; the `deps` component is a separate approval (`make deps`). The index of `ps3/deps` has quakespasm,
  armagetronad, freedoom, openttd-open*, mousepad, zathura, mpv; `openarena` was not found there and comes from
  live Debian ports.

## 3. Summary

- READY (15 items): kernel .178 + ps3rsx-modules 0.249; Mesa .415; libspeq/ps3-spu 0.395;
  ps3-gpu-tools 0.428; libsdl3; luajit; ioquake3; yamagi-quake2; dsda-doom; fluidsynth; extremetuxracer; neverball;
  luanti; cube2; openttd.
- NEEDS-CHECK: xmoto (run .361 once), classicube (the -4% and the split upgrade), supertuxkart (`lean` default, sound),
  teeworlds (sound), ps3-system / ps3-desktop+apps+games (game mode and kicker on the PS3), ps3-boot (kboot.conf),
  plymouth + ps3-splash (a cold boot).
- NOT YET: kernels .402, .419, .433 (and the source's .435) until the user's reboot session; ps3rsx-modules for them.
- SKIP: openal-soft.

## 4. Proposed commands (not run)

Nothing below is run by this file. Check that the PS3 is not in use and no bench session holds the lock
before anything touches it; publishing does not touch the PS3.

### 4.1 Rebuilds for the READY set (official versions, clean tree required)

```
git status --short                      # must print nothing; a dirty tree gives ~dev
make mesa-deb                           # 26.2.4-1+ps3.415, about 12-25 min under qemu
make ps3-spu-deb                        # libspeq0, libspeq-dev, ps3-spu, ps3-spu-programs 0.395
make ps3-gpu-tools-deb                  # 0.428
ls build/debs/*/*.deb | grep '~dev'     # lists the dirs still on dev builds: linux, ps3rsx-*, classicube, ps3-desktop
                                        # until their rebuilds; none of those may be in DEBS
```

No rebuild for libsdl3 254, luajit 247, ioquake3 396, yquake2 415, dsda-doom 361, fluidsynth 309, etr 324,
neverball 370, luanti 405, cube2 354, openttd 297: their `build/debs` files are official versions already.
The kernel .178 pair comes from `build/aptly/pool` (also official versions).

### 4.2 Stable publish of the READY set

Back up first (the user's rule; 5.1 GB, /home has 33 GB free):

```
cp -a build/aptly build/aptly-backup-$(date +%Y%m%d%H%M%S)    # remove after the publish is verified (ephemeral copy)
K=$(find build/aptly/pool \( -name '*_6.12.111-1+ps3.178_*.deb' -o -name '*_0.249+k6.12.111+ps3.178_ppc64.deb' \) \
      ! -name '*debug*' ! -name '*dbgsym*')
make publish DEBS="$K \
  $(ls build/debs/mesa/*.deb build/debs/ps3-spu/*.deb build/debs/ps3-gpu-tools/*.deb \
       build/debs/libsdl3/*.deb build/debs/luajit/*.deb build/debs/ioquake3/*.deb build/debs/yquake2/*.deb \
       build/debs/dsda-doom/*.deb build/debs/fluidsynth/*.deb build/debs/etr/*.deb build/debs/neverball/*.deb \
       build/debs/luanti/*.deb build/debs/cube2/*.deb build/debs/openttd/*.deb)"
```

The `$K` set is eight files: `linux-image-6.12.111+ps3.178`, `linux-headers-6.12.111+ps3.178`, its `-common`,
`linux-kbuild-6.12.111+ps3.178`, metas `linux-image-ps3` and `linux-headers-ps3`, `ps3rsx-modules-6.12.111+ps3.178`
and `ps3rsx-modules-ps3` (0.249). The dbgsym debs in those directories go to `ps3-dbgsym`, as `make publish`
does today; `aptly-publish.sh` refuses any `~dev` version and the check "versions only grow" runs before the add.
Afterwards verify by fetching `http://10.0.0.10:8070/ps3/dists/ps3/main/binary-ppc64/Packages` and counting the
new versions, then `make repo-snapshot` only if the user wants an image snapshot.

### 4.3 After the NEEDS-CHECK items pass (second publish, same pattern)

```
make classicube-deb                        # +ps3.374; publish only the engine deb
make ps3-desktop-deb                       # ps3-desktop, ps3-apps, ps3-games 0.406
make ps3-boot-deb ps3-splash-deb plymouth-deb   # 0.247 / 0.247 / +ps3.247, only if their checks pass
make publish DEBS="build/debs/classicube/classicube_1.3.8~git20261014.aa47929-1+ps3.374_ppc64.deb \
   build/debs/ps3-system/ps3-system_0.419_all.deb build/debs/ps3-desktop/*.deb \
   build/debs/xmoto/*.deb build/debs/supertuxkart/*.deb build/debs/teeworlds/*.deb \
   build/debs/ps3-boot/*.deb build/debs/ps3-splash/*.deb build/debs/plymouth/*.deb"
```

(Adjust the list to what passed; `ls` the directories first so the classicube path is exact.)

### 4.4 ps3-licensed

Nothing to publish: `classicube-data` .366, `sauerbraten-nonfree-data` .388, `ps3-games-licensed` .388,
`quake-shareware` 89, `quake2-demo-data` 89 are all in `ps3-licensed` already, and `build/debs` holds the same
versions. Do not publish `classicube-data` .374 (see classicube). If `ps3-games` changes its Depends again,
`make ps3-games-licensed-deb` and `make publish DEBS=build/debs/ps3-games-licensed/ps3-games-licensed_<N>_all.deb`
(goes to `ps3-licensed` whatever `DIST`). Once `ps3-games` is in stable, a PS3 can install
`ps3-games-licensed` without `-t ps3-dev`.

### 4.5 Do not publish

The `~dev` builds in `build/debs` (mesa 408~dev, classicube 367~dev, ps3-desktop 376~dev, linux and ps3rsx-modules
433~dev), openal-soft, and anything kernel newer than .178 until the reboot session.
