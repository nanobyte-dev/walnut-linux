# PS3 tests: scheduled and done

The list of every PS3 test that is queued, and every batch that ran, so nothing built is forgotten
(the user, 2026-10-19). When a test is built or proposed it goes under Scheduled; when its batch
runs, it moves to History with the results commit. Rules: same-boot baselines, 60 s warm-up and a
120 s window, webcam photos checked, no commands on the PS3 during a run. New, untested
optimizations go on 2-3 games first; the rest of the suite follows once they hold up.

The console's internal disk is an upgraded SSD; a stock PS3 has a 2.5" HDD. Results on the SSD
root (`root=/dev/mapper/ps3root` in bench.txt) are better on disk-bound phases than a stock
console would see.

## Scheduled

### Tonight (2026-10-19, SSD root, kernel .472, ps3-gpu-tools 0.477~dev)

| Batch | What | Games | Runs | Status |
|---|---|---|---|---|
| ssdroot1 A | baseline on the SSD root, STALLS=1, x3 (against USB: ssd1's "-" arm, batch 17) | Sauerbraten, OpenArena gl2min, Luanti, SuperTuxKart lean, ClassiCube | 15 | running |
| ssdroot1 B, C | force-DXT over 5 games, PLAN 2.6 repeats | | 64 | cut: B narrowed into ssdroot2, C below |
| ssdroot2 P1a | start-up profiles on the SSD root (PERF_LOAD=1 STALLS=1, x1) | the five above | 5 | queued |
| ssdroot2 P1b | gameplay profiles (PERF=1 MEM=1, x1) of the games under 60 fps or with stalls | Luanti, Sauerbraten, SuperTuxKart lean, ClassiCube, OpenArena gl2min, OpenTTD gl, dsda-doom gl | 7 | queued |
| ssdroot2 P2 | Mesa 0038 `NV30_FORCE_DXT=opaque` against off (MEM=1 STALLS=1, x2) | Sauerbraten, SuperTuxKart lean, OpenArena gl2min | 12 | queued |
| ssdroot2 P2 | ps3rsx `shadow_fast` (`PS3RSX_SHADOW_FAST=1` against `-`, STALLS=1, x2) | ClassiCube, Sauerbraten, X-Moto | 12 | queued |
| openttd-play | OpenTTD on a real savegame (gameplay benchmark being built: a developed map, fixed camera, sound and music on), smoke then bench x3, GL vs software | OpenTTD | ~8 | after the asset and recipe land |
| smoke-new | smoke runs (recipes 6d9fcaf; apt install between batches, then `make bench-push`): ScummVM (BASS, queen, drascula, lure), Simutrans pak64, Stone Soup tiles, Freeciv (AI game, observer), Widelands tutorial; menu-only, need the user: LinCity-NG, Flare, Warmux, Cataclysm DDA | 9 games | ~12 | after batch ssdroot2 |

Sound: tools/bench-audio.sh (8 s of the webcam microphone after every photo) is off until the user
says the room is free for it.

### Next (needs a slot, or the user)

| Test | Games | Needs | Source |
|---|---|---|---|
| USB root against SSD root: the same set in alternating boots USB/SSD/USB/SSD (USB root needs ps3-gpu-tools 0.477~dev first, for the disk sampling) | the five baseline games + OpenArena gl2min, Sauerbraten gameplay | the user (reboots) | PLAN 1.14, GAMEOS-HDD.md "Phase B" |
| force-DXT on the rest of the suite (`opaque`, `opaque+NEAREST`, `all`) | Luanti, ClassiCube, OpenArena `gl2min_tc0`, SuperTuxKart `all` | PS3 | MEMORY.md "Texture compression at upload", batch ssd1 script |
| `shadow_fast` on the rest of the suite (`run.sh suite base 1`) | all | PS3 | HOTPATHS.md "ps3rsx's submission path" |
| PLAN 2.6 repeats: Sauerbraten `oqlag` x3, Luanti `view40` x3, ETR steady-state with stalls, Teeworlds, Armagetron live | as listed | PS3 | PLAN 2.6 (ClassiCube `chunks1` and X-Moto `stream` were repeated in batch 16) |
| Mesa 0039 `NV30_SHADER_CACHE=on` cold/warm | OpenArena gl2min, Sauerbraten | PS3 | PROFILING.md "nv30's shader cache" (skipped in ssd1: upper bound 0.4 s) |
| Kernel guard (local/0045, on in the series since 1abb2a1): a kernel deb with it, then GAMEOS-HDD.md "On the PS3" steps 1-5 (write-backs of data already there; Phase A needs the new ps3-imgmap on a guard kernel, and an initramfs rebuilt for that release with guard=required) | none | the user (reboot into the guard kernel), then PS3 | GAMEOS-HDD.md "The kernel guard" |
| Sound by ear: everything bench-audio flags, dsda-doom's music, OpenTTD's music, SuperTuxKart (`sound=none`) | | the user | PLAN 2.1, 2.8 |
| Game mode at the TV, menu-only smoke games (TORCS, Trigger Rally, Chromium B.S.U.), Nexuiz, the interference detector's checks, power paths, 7th SPE, fan floor | | the user | PLAN 1.8-1.13, 4.2, 4.4 |
| Release image session | | the user | RELEASE-CANDIDATES.md |

## History

One line per batch: date (local), boot, what ran, verdict, results commit. Details in the commit and
in games/bench/results/.

| Date | Boot / root | Batch | Verdict | Commit |
|---|---|---|---|---|
| 10-19 02:37 | .472, USB root | ssd1: game data and VRAM-swap spill on the SSD file (Phase A), Sauerbraten x3, OpenArena x3 (stopped early) | game data alone leaves the load's USB disk wait at 5-6 s; Sauerbraten no proven gain, OpenArena load -28% | 9e6bc57 |
| 10-18 23:50 | .472 | batch 17: ioquake3 462 snorm colours, Mesa 0037 sysmem copy + invalidate, cube2 shortnormals, GLSL disk cache, start-up profiles | OpenArena gl2min 57-60 fps with 1-2 s stalls (9-11 before); color16 stalls 12 -> 1.5 s; Sauerbraten +17%; GLSL cache no load gain | 3939a48 |
| 10-18 20:29 | .433 | batch 16: game prefetch, idle fix on/off, ClassiCube chunks1, X-Moto stream, Luanti chunk3 | prefetch no gain (default off); idle fix confirmed; chunks1 +4.7%; stream +1%; chunk3 empties the world | 296f99d |
| 10-18 16:33 | .433 | Luanti clean re-runs | cliunload worse, lean draws a black void; base stays | 6880c07 |
| 10-18 15:35 | .419 | idle fix in game, OpenArena | stalls 0.1-0.5 s with, 5.6-8.2 without (SSH-polled; repeated clean in batch 16) | e59c09a |
| 10-18 14:38 | | Sauerbraten `NV30_QUERY_WAIT` repeat | no fps gain, stays off | 0382e3e |
| 10-18 14:21 | | ClassiCube chunks1, X-Moto stream, query wait (single runs) | +5%, fewer hitches, 25 -> 45.6 (not confirmed by the repeat) | 31e356a |
| 10-18 13:19 | | Luanti cliunload/lean/view40, ClassiCube batching profiles | later found interfered (see 6880c07) | b089cbc |
| 10-18 12:04 | | smoke: Teeworlds, Red Eclipse, SuperTux; watermark_boost_factor=0 | Teeworlds runs (BE fix), Red Eclipse no OOM; watermark no clear gain | 2c5a2c9 |
| 10-18 11:32 | | SuperTuxKart track, OpenTTD GL, dsda/ClassiCube batching, malloc presets, profiles | STK lean 21 fps, OpenTTD GL 21, dsda +3%, ClassiCube -4%, malloc noise | 8bb2acd |
| 10-18 08:34 | | Armagetron live AI | 83 fps, one 127 ms frame | 14480b1 |
| 10-18 08:25 | | OpenArena preload/keep-images | gameplay stalls 9-10 s -> 0, steady 65 -> 70.6 | 2d3243c |
| 10-18 07:43 | | QuakeSpasm LUT, ClassiCube fence pool, dsda/OpenTTD profiles | neutral, neutral | 350683b |
| 10-18 06:47 | | Mesa 0033 LUT, Yamagi kiss4k, Luanti trim16 | Sauerbraten +53%; Yamagi -70 MB, stalls 22.6 -> 0.05 s; trim16 rejected | da8239c |
| 10-18 05:38 | | 10 ms cross-CPU kick | OpenArena gameplay stalls halved | fce0c5b |
| 10-18 03:06 | | first stall-recorder runs | OpenArena hitches CPU, Sauerbraten disk faults | b9a4187 |
| 10-18 02:27 | | Luanti/Neverball batching, Sauerbraten skinning modes | fewer stalls; CPU -19%; SPU skinning adds hitches | be8e87b |
| 10-18 01:32 | | OpenArena lightmap atlas variants | any atlas page +10 s of hitches | 5369041 |
| 10-18 00:20 | | dsda-doom music on the SPE deadline pool | underruns 465 against 1095 | 388b461 |
| 10-17 23:45 | | OpenArena opengl1 batching | no gain | eb7da4a |
| 10-17 20:39 | | Mesa ps3.299, X-Moto and ETR batching | X-Moto 44.5 -> 166.5, ETR 52 -> 59 | edab738 |
| 10-17 16:31 | | ioquake3 ps3.285 (OpenArena opengl2 memory) | 14.8 -> 26-32 fps, longest stall 24.7 -> 6 s | 973b5c9 |

Earlier rounds (2026-10-16/17: stage 0, Mesa ps3.246/262/282) are in GAME-PLAN.md "Measured".
