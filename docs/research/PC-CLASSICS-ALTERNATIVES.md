# PC classics: the alternatives and their popularity

Every open project found for each game in [PC-CLASSICS.md](PC-CLASSICS.md), ranked by popularity, with
the pick for our port. Fetched 2026-10-20.

How to read it:
- **Stars** come from the GitHub API for the first few dozen repositories, and from the repository
  pages for the rest. Counts of 1k and above from the pages are rounded. Codeberg and GitLab stars
  are not comparable with GitHub's (EDuke32 has 14 on its own GitLab and is still the most-used Build port).
- **Last push** is the last commit on the default branch (or the API's `pushed_at` where it came from the API).
- **Pick** is the project we'd port: the most popular one that fits the PS3 (C/C++ and SDL rather than
  .NET, Java, Unity, Go, Rust or Pascal; GL level; big-endian prior art). Where the most popular project
  doesn't fit, both are named.
- **?** means the page couldn't be fetched (removed, or not on a host we could read).

Most popular projects that are not the pick: OpenRA (17.6k stars, .NET, GL 3.2), openage (14.5k,
gameplay broken), OpenDiablo2 (11.1k, Go, archived), doukutsu-rs (1.3k, Rust; NXEngine-evo is the C++
pick for Cave Story), Widelands (3.1k, its own game rather than Settlers 2), Beyond All Reason (4.3k, too heavy).

Stars and dates fetched 2026-10-20 from the public repo pages (stargazerCount, last commit on the default branch, archived flag); the GitHub REST quota was spent on discovery. "?" = not fetchable (page removed or not on GitHub). "Pick" = recommended for our PS3 port (most popular AND fits: GL 2.1, C/C++, big-endian prior art).

## The user's favourites (and Ultima, Sims, AoE II)

| Game | Project (URL) | Stars | Last push | State | Pick |
|---|---|---|---|---|---|
| Jazz 2 | Jazz² Resurrection https://github.com/deathkiller/jazz2-native | 716 | 2026-10-17 | Full game, active, C++ (own engine, GL 3.3 / GLES 3) | **PICK** (only complete JJ2 option; renderer back-port to GL 2.1 needed) |
| Jazz 2 | deathkiller/jazz2 https://github.com/deathkiller/jazz2 | 581 | 2026-10-17 | Older C#/.NET version by the same author | no (.NET) |
| Jazz 2 | Project Carrot https://github.com/soulweaver91/project-carrot | 39 | 2017-08-28 | Abandoned C++/Qt | no |
| Jazz 2 | jazzjackrabbit2-decompiled https://github.com/Mustafa1177/jazzjackrabbit2-decompiled | 3 | 2025-05-09 | Decompiler dump, not playable | no |
| Jazz 1 | OpenJazz https://github.com/AlisterT/openjazz | 411 | 2026-09-01 | Playable C++/SDL, many console ports | **PICK** (only option, tiny) |
| Elasto Mania | elma-classic https://github.com/elastomania/elma-classic | 17 | 2025-12-05 | Official "Elasto Mania Classic Source Code" (new find, not in the doc); mirror https://github.com/elmadev/elma-classic (5 stars, 2026-10-20) | **PICK to evaluate** (the real game's source) |
| Elasto Mania | Action SuperCross (predecessor) https://github.com/elastomania/across | 27 | 2021-11-21 | Source of the predecessor, not Elma | check |
| Elasto Mania | X-Moto https://github.com/xmoto/xmoto | 162 | 2025-08-27 | Open clone, we ship it | fallback |
| Atomic Bomberman | fpc_atomic https://github.com/PascalCorpsman/fpc_atomic | 58 | 2026-10-03 | Playable clone incl. network, Free Pascal/Lazarus | no (Pascal) |
| Atomic Bomberman | Bombman https://github.com/drummyfish/Bombman | 41 | 2017-03-18 | Python clone | no |
| Atomic Bomberman | elraro/atomic-bomberman https://github.com/elraro/atomic-bomberman | 17 | 2026-10-12 | C++20, SDL3, GL 3.3, uses original assets | **PICK** (least popular, but C++ and the only one with original data) |
| NFS II SE | NFSIISE https://github.com/zaps166/NFSIISE | 773 | 2026-05-01 | Reimplementation, C++ (new find, not in the doc) | **PICK to evaluate** |
| NFS 1-6 | OpenNFS https://github.com/OpenNFS/OpenNFS | 855 | 2026-06-21 | Track viewer, early, GL 4+ | watch |
| NFS III | nfs3-re https://github.com/melroy89/nfs3-re | 8 | 2026-10-15 | Decompilation | watch |
| NFS III/SE | nfs-recompiled https://github.com/motor-dev/nfs-recompiled | 80 | 2026-03-04 | Static recompilation, "toy project" | watch |
| NFS II | re-nfs2 https://github.com/thiekus/re-nfs2 | 8 | 2026-03-05 | Hooks the Windows exe | watch |
| NFS High Stakes | NFSHS-PSX-decomp https://github.com/Caesar0007/NFSHS-PSX-decomp | 53 | 2026-10-19 | PS1 matching decomp (MIPS) | no |
| NFS Underground | nfsug https://github.com/dbalatoni13/nfsug | 39 | 2026-02-04 | Decomp | watch |
| NFS Underground 2 | speed-re https://github.com/troikoss/speed-re (6, 2026-10-19); nfsu2-decomp https://github.com/cobanov/nfsu2-decomp (7, 2026-10-09); nfsu2-sw https://github.com/antoxa2584x/nfsu2-sw (147, 2026-10-18) | | | Incomplete decomps / software renderer WIP | watch |
| NFS Most Wanted | nfsmw https://github.com/dbalatoni13/nfsmw | 231 | 2026-10-10 | GameCube matching decomp (big-endian PPC); Most-Wanted-Vehicles-Decomp https://github.com/Brawltendo/Most-Wanted-Vehicles-Decomp (33, 2022) stale | watch (PPC code) |
| Unreal / UT99 | SurrealEngine https://github.com/dpjudas/SurrealEngine | 1121 | 2026-10-20 | Only open engine; UT 436 / Unreal 226 "relatively playable" | **PICK** (no alternative) |
| Unreal / UT99 | UnrealTournamentPatches https://github.com/OldUnreal/UnrealTournamentPatches | 1496 | 2026-10-16 | Official revival of the real binary; patches/issues, binaries x86/ARM | not a source option |
| UT2004 | none | | | OldUnreal binaries only | not possible |
| Ultima VII | Exult https://github.com/exult/exult | 694 | 2026-10-12 | Complete, C++ | **PICK** |
| Ultima IV | xu4 https://github.com/xu4-engine/u4 | 73 | 2025-06-05 (archived) | Reimplementation | alt |
| Ultima IV/VI/VIII | ScummVM https://github.com/scummvm/scummvm | 2861 | 2026-10-20 | Ultima engines inside | **PICK** (most popular) |
| Ultima Underworld | Underworld Adventures https://github.com/vividos/UnderworldAdventures | 75 | 2026-07-30 | Active again in 2026 (the doc said dormant), C++ | **PICK** |
| The Sims | Simitone https://github.com/riperiperi/Simitone | 500 | 2025-10-11 | .NET | no |
| The Sims Online | FreeSO https://github.com/riperiperi/FreeSO | 986 | 2025-08-22 | .NET | no |
| The Sims 2 | OpenTS2 https://github.com/LazyDuchess/OpenTS2 | 437 | 2026-07-25 | Unity | no |
| SimCity 4, Spore, Hercules, Tarzan, UT2004 | none found | | | | not possible |
| AoE II | openage https://github.com/SFTtech/openage | 14525 | 2026-10-15 | Gameplay non-functional while the simulation is rebuilt | watch (only live one) |
| AoE II | freeaoe https://github.com/sandsmark/freeaoe | 241 | 2021-08-03 | Inactive | no |

## Action and shooters

| Game | Project (URL) | Stars | Last push | State | Pick |
|---|---|---|---|---|---|
| GTA III / VC / LCS | hez-gta-re3 https://github.com/Hezkore/hez-gta-re3 | 117 | 2026-08-31 | re3 + reVC + reLCS in one tree, fixes; the most maintained fork I found | **PICK** (covers all three, librw GL 2.1) |
| GTA Vice City | reVC fork/mirror https://github.com/mrxenginner/reVC | 437 | 2026-10-02 | Vice City source, active | alt (most stars) |
| GTA III | Cai1Hsu/re3 https://github.com/Cai1Hsu/re3 | 318 | 2022-07-29 | Stale | no |
| GTA III | re3-19feb21 https://github.com/leanndroguedes/re3-19feb21 | 202 | 2021-02-19 (archived) | Snapshot | no |
| GTA III | GTAmodding/re3, halpz/re3 | ? | ? | Removed (HTTP 451, Take-Two DMCA/suit 2021): the upstream is gone, a legal risk for us | |
| GTA III | OpenRW https://github.com/rwengine/openrw | 2244 | 2025-06-18 | Clean-room, GPLv3, C++; incomplete | no |
| GTA III/VC | reVCDOS https://github.com/Lolendor/reVCDOS | 413 | 2026-05-29 | Browser port | no |
| Jedi Knight | OpenJKDF2 https://github.com/shinyquagsire23/OpenJKDF2 | 770 | 2026-10-13 | 95.9% reimplemented, GL 3.3/1.1/GLES3 | **PICK** |
| Dark Forces / Outlaws | TheForceEngine https://github.com/TheForceEngine/TheForceEngine | 1169 | 2026-10-06 | v1.0, GL + software renderer | **PICK** |
| Indiana Jones Infernal Machine | OpenJones3D https://github.com/smlu/OpenJones3D | 68 | 2025-07-08 | Early | watch |
| Indiana Jones Infernal Machine | infernal-machine-decomp-public https://github.com/andrewspode/infernal-machine-decomp-public | 15 | 2026-10-19 | Byte-matched decomp, active | watch |
| Jedi Outcast / Academy | OpenJK https://github.com/JACoders/OpenJK | 2309 | 2026-09-29 | Playable, ppc/ppc64 BSD packages | **PICK** |
| Jedi Outcast | jk2mv https://github.com/mvdevs/jk2mv (139, 2025-06); Speed-Outcast https://github.com/kugelrund/Speed-Outcast (18, 2026-06) | | | MP client / speedrun fork | no |
| Half-Life / CS 1.6 | Xash3D FWGS https://github.com/FWGS/xash3d-fwgs | 2865 | 2026-10-19 | The engine; GL 1.x/2.x, GLES, software | **PICK** |
| Half-Life | hlsdk-portable https://github.com/FWGS/hlsdk-portable | 413 | 2026-10-20 | Game DLLs for non-x86; needed with Xash | needed |
| Half-Life | Valve halflife https://github.com/ValveSoftware/halflife | 4389 | 2024-10-02 | Official game code + SDK, no engine | no engine |
| Half-Life | fgsfdsfgs/xash3d-fwgs https://github.com/fgsfdsfgs/xash3d-fwgs | 13 | 2023-06-25 | Old console fork | no |
| HL2 / Portal | nillerusr/source-engine https://github.com/nillerusr/source-engine | 2299 | 2025-11-25 | From the leak, ToGL, builds on Linux/ARM | **PICK** (only engine path; L) |
| HL2 | source-sdk-2013 https://github.com/ValveSoftware/source-sdk-2013 | 10070 | 2026-10-10 | Official SDK, game code only | no engine |
| HL2 | Mapbase https://github.com/mapbase-source/source-sdk-2013 | 316 | 2026-05-17 | Mod framework on the SDK | no |
| Doom 3 | dhewm3 https://github.com/dhewm/dhewm3 | 2185 | 2026-06-08 | GL ARB2 path, SDL2 | **PICK** (GL 2.x fit) |
| Doom 3 | RBDOOM-3-BFG https://github.com/RobertBeckebans/RBDOOM-3-BFG | 1861 | 2026-08-07 | Modern renderer, GL 4.x | no (too heavy) |
| Doom 3 | id releases https://github.com/id-Software/DOOM-3 (3609, 2012); https://github.com/id-Software/DOOM-3-BFG (5387, 2015) | | | Frozen dumps | reference |
| RTCW | iortcw https://github.com/iortcw/iortcw | 763 | 2024-05-26 | ioq3 line, GL 1.x/2.x, stable | **PICK** |
| RTCW | RealRTCW https://github.com/wolfetplayer/RealRTCW | 180 | 2026-10-12 | Active gameplay overhaul on iortcw | alt (heavier) |
| RTCW MP | rtcwPro https://github.com/rtcwmp-com/rtcwPro | 30 | 2025-09-13 | Competitive mod | no |
| RTCW | id RTCW-SP https://github.com/id-Software/RTCW-SP | 629 | 2012-01-31 | Frozen dump | reference |
| Wolfenstein: ET | ET: Legacy https://github.com/etlegacy/etlegacy | 731 | 2026-10-19 | Active; free game | **PICK** |
| Wolfenstein: ET | id Enemy-Territory https://github.com/id-Software/Enemy-Territory | 617 | 2012-01-31 | Frozen dump | reference |
| Quake 3 | ioquake3 https://github.com/ioquake/ioq3 | 2850 | 2026-10-12 | The baseline; PPC JIT history | **PICK** |
| Quake 3 | Quake3e https://github.com/ec-/Quake3e | 1547 | 2026-10-14 | Performance fork; x86/ARM JIT, Vulkan | alt (no PPC JIT) |
| Quake 3 | id Quake-III-Arena https://github.com/id-Software/Quake-III-Arena | 8275 | 2012-01-31 | Frozen dump | reference |
| Quake 3 | spearmint https://github.com/zturtleman/spearmint | ? | ? | Not fetchable | ? |
| Elite Force | none verified | | | | ? |
| Heretic II | Heretic2R https://github.com/m-x-d/Heretic2R | 271 | 2026-08-21 | Reverse-engineered source port (Mar 2026), GPL, v1.06 data | **PICK** |
| Heretic II | 0lvin/heretic2 https://github.com/0lvin/heretic2 | 32 | 2026-10-14 | Yamagi Quake II based port | alt |
| Hexen II | uHexen2 https://github.com/sezero/uhexen2 | 119 | 2026-09-04 | Active | **PICK** |
| Hexen II | hexen2-dreamcast https://github.com/Shockblast/hexen2-dreamcast | 1 | 2022-03-12 | Console fork | no |
| Heretic/Hexen (1994/95) | Chocolate Doom https://github.com/chocolate-doom/chocolate-doom | 2437 | 2026-10-12 | Vanilla-exact | **PICK** |
| Heretic/Hexen | Crispy Doom https://github.com/fabiangreffrath/crispy-doom (962, 2026-10-12); DSDA-Doom https://github.com/dsda-org/dsda-doom (513, 2026-10-15); Doomsday https://github.com/skyjake/Doomsday-Engine (306, 2024-01); Russian Doom https://github.com/Russian-Doom/russian-doom (88, 2026-05) | | | Alternatives | |
| Quake (note) | QuakeSpasm https://github.com/sezero/quakespasm | 404 | 2026-10-03 | Ironwail is the same family but Quake only, not Hexen II | n/a |
| Serious Sam TFE/TSE | SeriousSamClassic https://github.com/tx00100xt/SeriousSamClassic | 191 | 2026-01-13 | Linux/BSD/ARM fork of the official source | **PICK** (the only maintained non-Windows line) |
| Serious Sam | Croteam-official https://github.com/Croteam-official/Serious-Engine | 3187 | 2017-01-21 | Official 2016 dump, Windows | reference |
| Serious Sam | ptitSeb/Serious-Engine https://github.com/ptitSeb/Serious-Engine | 94 | 2025-09-28 | First Linux/ARM port | alt |
| Serious Sam | SeriousSamClassic-VK https://github.com/tx00100xt/SeriousSamClassic-VK | 161 | 2026-01-14 | Vulkan | no |
| Duke 3D / Build | EDuke32 https://voidpoint.io/terminx/eduke32 (GitLab) | 14 | 2026-10-18 | Most-used Build port (stars not comparable on this host); Polymost GL plus software | **PICK** (software mode suffices on the PPE) |
| Duke 3D | Raze https://github.com/ZDoom/Raze | 890 | 2025-11-19 | GZDoom-style renderer, GL 3.3+ | no |
| Duke / Blood / Redneck / SW | NBlood https://github.com/NBlood/NBlood | 815 | 2026-09-14 | Blood, Rednukem, EDuke32-based | alt (**PICK for Blood**) |
| Duke 3D | jfduke3d https://github.com/jonof/jfduke3d | 142 | 2026-01-02 | Classic port, PPC Mac history | alt |
| Duke 3D | icculus/duke3d https://github.com/icculus/duke3d | 19 | 2021-07-12 | Original Linux port | alt |
| Duke 3D | chocolate_duke3D https://github.com/fabiensanglard/chocolate_duke3D | 618 | 2019-03-09 (archived) | Readable port | no |
| Rise of the Triad | rottexpr https://github.com/LTCHIPS/rottexpr | 109 | 2022-12-21 | SDL2 port, the common one | **PICK** |
| Rise of the Triad | rott94 https://github.com/h4mu/rott94 (40, 2026-03); Barrett https://github.com/lunarmeadow/barrett (34, 2026-06) | | | Newer SDL2 ports | alt |
| Descent 1/2 | DXX-Rebirth https://github.com/dxx-rebirth/dxx-rebirth | 641 | 2026-10-11 | Main line | **PICK** |
| Descent 1/2 | DXX-Redux https://github.com/dxx-redux/dxx-redux | 77 | 2026-09-15 | Rebirth fork | alt |
| Descent 1/2 | DXX-Retro https://github.com/CDarrow/DXX-Retro | 52 | 2022-12-16 | Competitive fork, stale | no |
| Descent 3 | Descent3 https://github.com/DescentDevelopers/Descent3 | 3019 | 2026-09-16 | Official GPL, active | **PICK** |
| Forsaken | ForsakenX https://github.com/ForsakenX/forsaken | 181 | 2024-07-16 | Only option | **PICK** |
| Postal | POSTAL-SourceCode https://github.com/RWS-Studios/POSTAL-SourceCode | 127 | 2025-05-26 | Official GPL-2 release; Debian packages it | **PICK** |
| Postal | classiccoding fork https://github.com/classiccoding/postal-1-open-source | 6 | 2017-11-27 | 64-bit fixes, stale | no |
| Hitman | ReHitman https://github.com/ReGlacier/ReHitman | 72 | 2026-05-23 | Hitman 2 tooling | watch |
| Hitman Codename 47 | re47 https://github.com/0danny/re47 | 74 | 2026-07-29 (archived) | Archived | no |
| Halo CE | punpckhdq/halo https://github.com/punpckhdq/halo | 196 | 2026-10-17 | Xbox decomp, active | watch |
| Halo CE | halo-re/halo https://github.com/halo-re/halo | 456 | 2023-07-07 | Older | watch |

## Racing

| Game | Project (URL) | Stars | Last push | State | Pick |
|---|---|---|---|---|---|
| Carmageddon | dethrace https://github.com/dethrace-labs/dethrace | 1227 | 2026-10-20 | Playable, BE fixes upstream, GL option | **PICK** |
| Carmageddon | OpenC1 https://github.com/jeff-1amstudios/OpenC1 | 168 | 2014-12-02 | C#/XNA, dead | no |
| Carmageddon II | carma2-clone https://github.com/kiney/carma2-clone | 1 | 2026-10-17 | New, tiny | watch |
| Re-Volt | RVGL | | | Closed source; FreeVolts (SourceForge, GPLv2) is multiplayer-only | not portable |
| Midtown Madness | Open1560 https://github.com/0x1F9F1/Open1560 | 196 | 2026-04-20 | Partial rewrite, x86 asm | watch |
| SW Episode I Racer | SW_RACER_RE https://github.com/tim-tim707/SW_RACER_RE | 78 | 2026-10-05 | dinput hook, D3D6 | watch |
| Death Rally | dRally https://github.com/urxp/dRally | 74 | 2023-07-26 | WIP, Amiga port | **PICK** |
| Death Rally | brally https://github.com/jeff-strutb/brally | 11 | 2026-10-19 | Another reimplementation, active | watch |
| LEGO Racers | racers https://github.com/isledecomp/racers (100, 2026-10-17); lego-racers https://github.com/marijnvdwerf/lego-racers (8, 2025-01) | | | Decomps | watch |
| Driver 2 (extra) | REDRIVER2 https://github.com/OpenDriver2/REDRIVER2 | 1418 | 2026-06-26 | Playable, MIT (new find) | candidate |
| Nothing found | Midtown Madness 2, Motocross Madness, Moto Racer, Toca, Colin McRae, Rollcage (only RollCageX, a free clone), Crazy Taxi, Flatout, Trackmania, Stunt GP, POD, Viper Racing, Grand Prix 2 | | | confirmed by search | |

## Flight and space

| Game | Project (URL) | Stars | Last push | State | Pick |
|---|---|---|---|---|---|
| TIE Fighter | OpenTIE https://github.com/elyosh/OpenTIE | 338 | 2026-10-04 | Active; SDL3 GPU (no GL) | **PICK** (needs a GL 2.1 backend) |
| X-Wing | OpenXW https://github.com/elyosh/OpenXW | 117 | 2026-10-06 | same | same |
| X-Wing Alliance | OpenXWA https://github.com/elyosh/OpenXWA | 157 | 2026-10-12 | same | same |
| Freelancer | Librelancer https://github.com/Librelancer/Librelancer | 612 | 2026-10-10 (archived) | Moved to https://codeberg.org/Librelancer/Librelancer (3 stars, active 2026-10-19); .NET | **PICK** (only one) |
| FreeSpace 1/2 | FreeSpace Open https://github.com/scp-fs2open/fs2open.github.com | 495 | 2026-10-18 | Only engine | **PICK** |
| Privateer | Vega Strike engine https://github.com/vegastrike/Vega-Strike-Engine-Source | 364 | 2026-10-20 | Gemini Gold runs on it | **PICK** |
| Nothing found | Wing Commander 3/4, Tachyon, Independence War, Starlancer, MechWarrior | | | | |

## Platformers and 2D action

| Game | Project (URL) | Stars | Last push | State | Pick |
|---|---|---|---|---|---|
| Tomb Raider 1-3 | OpenLara https://github.com/XProger/OpenLara | 5130 | 2026-07-26 | TR1-3, many platforms, GL/GLES, C++ | **PICK** (most popular, GL 2 / ES ports; BE unchecked) |
| Tomb Raider 1-3 | TRX (ex TR1X) https://github.com/LostArtefacts/TRX | 1017 | 2026-10-20 | Patches the original release; very active | alt (fidelity, GL 3.3) |
| Tomb Raider | OpenTomb https://github.com/opentomb/OpenTomb | 1445 | 2025-02-24 (archived) | TR1-5, dead | no |
| Tomb Raider 2 / 3 | TR2Main https://github.com/Arsunt/TR2Main (203, 2023); tomb3 https://github.com/Trxyebeep/tomb3 (118, 2023) | | | Exe-patch style | no |
| Tomb Raider 4/5 | TombEngine https://github.com/TombEngine/TombEngine (346, 2026-07); TOMB5 https://github.com/TOMB5/TOMB5 (520, 2021) | | | Level engine / decomp | no |
| Sonic CD | RSDKv3 https://github.com/RSDKModding/RSDKv3-Decompilation | 712 | 2026-10-10 | Complete | **PICK** |
| Sonic 1/2 (2013) | RSDKv4 https://github.com/RSDKModding/RSDKv4-Decompilation | 1149 | 2026-09-06 | Complete | **PICK** |
| Sonic Mania | RSDKv5 https://github.com/RSDKModding/RSDKv5-Decompilation (569, 2026-10); Sonic-Mania-Decompilation https://github.com/RSDKModding/Sonic-Mania-Decompilation (821, 2026-05) | | | Complete, heavier | alt |
| Sonic 3 | Sonic 3 A.I.R. https://github.com/Eukaryot/sonic3air | 660 | 2026-10-18 | Different game, C++ | extra |
| Captain Claw | OpenClaw https://github.com/pjasicek/OpenClaw | 517 | 2022-07-13 | Rewrite, SDL2 | **PICK** (only one) |
| Abe's Oddysee/Exoddus | alive_reversing https://github.com/AliveTeam/alive_reversing | 443 | 2026-09-01 | Active | **PICK** |
| Heart of Darkness | hode (usineur) https://github.com/usineur/hode | 151 | 2023-12-08 | Playable, SDL | **PICK**; original cyxx/hode https://github.com/cyxx/hode (16, archived) |
| Cave Story | doukutsu-rs https://github.com/doukutsu-rs/doukutsu-rs | 1316 | 2026-10-13 | Rust | most popular, Rust risk |
| Cave Story | NXEngine-evo https://github.com/nxengine/nxengine-evo | 552 | 2026-03-04 | C++/SDL2, many console ports | **PICK** (C++) |
| Cave Story | CSE2 https://github.com/gameblabla/CSE2 | 202 | 2026-03-12 | Decompilation, C++, portable | alt |
| VVVVVV | official https://github.com/TerryCavanagh/VVVVVV | 8037 | 2026-10-19 | Complete | **PICK** |
| Crimsonland | crimson https://github.com/banteg/crimson | 189 | 2026-10-19 | Python | only one |
| Soldat | OpenSoldat https://github.com/opensoldat/opensoldat | 1463 | 2026-10-18 | Active; Free Pascal | only one |
| Beats of Rage | OpenBOR https://github.com/DCurrent/openbor | 1281 | 2026-10-18 | Active | **PICK** |
| M.U.G.E.N | Ikemen GO https://github.com/ikemen-engine/Ikemen-GO | 1627 | 2026-10-18 | Go | only one |
| Duke Nukem II (extra) | RigelEngine https://github.com/lethal-guitar/RigelEngine | 1012 | 2026-09-28 | Active | extra |
| Space Cadet Pinball | SpaceCadetPinball https://github.com/k4zmu2a/SpaceCadetPinball | 4720 | 2024-08-21 | Decompilation, main line. Forks: Android https://github.com/iscle/SpaceCadetPinball (156), ports by headshot2017 for NDS/DC/PS2 (36-86) | **PICK** |
| Nothing found | Max Payne, Rayman 2, Croc, Earthworm Jim 3D, PoP 3D, LEGO Rock Raiders, Crash/Spyro, Worms, Deus Ex, Thief (OpenDarkEngine https://github.com/volca02/openDarkEngine, 174 stars, last 2018), System Shock 2, Mafia, NOLF, Blood 2, Postal 2 | | | | |

Data pulled 2026-10-20. "Last push" is `pushed_at` from the GitHub API for the first ~27 repos (Strategy batch) and the date of the last commit on the default branch (commits.atom) for the rest, so the latter can be older than a real push to a side branch (Warsmash shows 2025-12 by commit, 2026-10 by push). Stars of 1k+ from HTML are rounded (6.0k, 3.5k...). "?" = could not fetch. Pick = recommended for the PS3 port (BE ppc64, GL 2.1, ~217 MB RAM): most popular AND C/C++/SDL over .NET/Java/Unity/Go/Rust.

## Strategy

| Game | Project (URL) | Stars | Last push | State | Pick |
|---|---|---|---|---|---|
| Locomotion | [OpenLoco](https://github.com/OpenLoco/OpenLoco) | 1944 | 2026-10-14 | Complete, only project | Pick |
| StarCraft BW | [OpenBW](https://github.com/OpenBW/openbw) | 259 | 2026-08-13 | Deterministic engine, SDL/browser | Pick (C++) |
| | Titan Reactor (replay viewer on OpenBW, three.js; repo not found) | ? | ? | Viewer only, not a game | |
| | BWAPI / JBWAPI | n/a | | Bot APIs for the original exe, not engines | |
| Warcraft I/II | [Stratagus](https://github.com/Wargus/stratagus) (engine) | 738 | 2026-10-18 | Active, C++ | engine |
| | [Wargus](https://github.com/Wargus/wargus) (WC2 data/scripts) | 442 | 2026-10-10 | Playable | Pick |
| | [War1gus](https://github.com/Wargus/war1gus) (WC1) | 135 | 2026-06-22 | Playable | Pick for WC1 |
| Warcraft III | [Warsmash](https://github.com/Retera/WarsmashModEngine) | 581 | 2026-10-05 (commit 2025-12-09) | Alpha, Java/LibGDX; no other engine found (0 A.D. is only "inspired") | Only option |
| C&C TD/RA | [OpenRA](https://github.com/OpenRA/OpenRA) (C#/.NET, GL 3.2) | 17607 | 2026-10-11 | Very active, remake with own rules | most popular, .NET |
| | [Vanilla Conquer](https://github.com/TheAssemblyArmada/Vanilla-Conquer) (C++) | 467 | 2026-07-16 | Original game, Linux ok; MorphOS BE prior art | **Pick** |
| | [CnC_Remastered_Collection](https://github.com/electronicarts/CnC_Remastered_Collection) (EA official GPL) | 21.4k | 2025-01-16, archived | Source drop (game DLLs), feeds Vanilla Conquer, not a standalone port | source |
| | [Chronoshift](https://github.com/TheAssemblyArmada/Chronoshift) | 175 | 2020-07-06, archived | Dead earlier RA port | |
| C&C TS/RA2 | OpenRA mods (Dune 2000 etc.), OpenTS and reTS (TS/RA2, clean-room; repos not fetched) | ? | ? | reTS: engine not public yet (README 2026-07-15); EA did not release TS/RA2 | none |
| C&C Generals/ZH | [OpenSAGE](https://github.com/OpenSAGE/OpenSAGE) (C#) | 1653 | 2025-12-16 | Reimplementation, .NET | |
| | [GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) (fork of EA's GPL drop) | 1438 | 2026-10-19 | Active, DX8, Windows-first | most active |
| | [Thyme](https://github.com/TheAssemblyArmada/Thyme) | 697 | 2024-10-31 | Stalled portable port | |
| | EA official Generals/Renegade repos | ? | | Source drop 2025 | XL either way |
| Perimeter | [Perimeter](https://github.com/KD-lab-Open-Source/Perimeter) | 557 | 2026-09-21 | Official, only option | Only option |
| Homeworld | [HomeworldSDL](https://github.com/HomeworldSDL/HomeworldSDL) | 146 | 2024-10-13 | Dormant; group moved to GitLab | |
| | [Gardens of Kadesh](https://gitlab.com/gardens-of-kadesh/gardens-of-kadesh) (GitLab) | 18 | 2026-10-20 | Active SDL port, same original source | **Pick** (live successor) |
| | [GardensOfKadesh/Homeworld](https://github.com/GardensOfKadesh/Homeworld) (wasm) | 85 | 2025-09-28 | Browser port | |
| Black & White | [openblack](https://github.com/openblack/openblack) | 1589 | 2026-10-18 | Active; only option | Only option |
| Dungeon Keeper 1 | [KeeperFX](https://github.com/dkfans/keeperfx) | 1022 | 2026-10-20 | Active, GPL, C | Pick |
| Dungeon Keeper 2 | [OpenKeeper](https://github.com/tonihele/OpenKeeper) (Java/jME) | 508 | 2026-10-19 | Active, incomplete | Only option |
| Magic Carpet 2 | [remc2](https://github.com/turican0/remc2) | 153 | 2026-10-20 | Playable, only option | Only option |
| Syndicate Wars | [syndwarsfx (was swars)](https://github.com/swfans/syndwarsfx) | 266 | 2026-10-19 | Active | Only option |
| Syndicate | FreeSynd on [SourceForge SVN](https://freesynd.sourceforge.io/) (no GitHub; v0.9 Nov 2025) | n/a | 2025-11 | Playable, GPLv3, CMake | Only option |
| Theme Hospital | [CorsixTH](https://github.com/CorsixTH/CorsixTH) | 4606 | 2026-10-19 | Active, only mainstream | Pick |
| Settlers 2 | [Widelands](https://github.com/widelands/widelands) (own game, inspired by S2/S1) | 3112 | 2026-10-15 | Active, in Debian | most popular, not S2 itself |
| | [Return to the Roots](https://github.com/Return-To-The-Roots/s25client) | 598 | 2026-10-19 | The real S2 engine | **Pick** for S2 data |
| Settlers 3 | [settlers-remake](https://github.com/paulwedeck/settlers-remake) (Java) | 144 | 2025-12-02, archived | Dead | Only option |
| Caesar III | [Julius](https://github.com/bvschaik/julius) | 3408 | 2026-09-12 | Complete, faithful | **Pick** |
| | [Augustus](https://github.com/Keriew/augustus) (fork of Julius, extra features) | 2124 | 2026-10-16 | Active superset | drop-in alternative |
| Pharaoh/Cleopatra | [Akhenaten](https://github.com/dalerank/Akhenaten) (fork of Julius code) | 517 | 2026-10-12 | Early campaign plays | Only option |
| Heroes II | [fheroes2](https://github.com/ihhub/fheroes2) | 3.5k | 2026-10-16 | Complete, only active one | Pick |
| Heroes III | [VCMI](https://github.com/vcmi/vcmi) | 6.0k | 2026-10-19 | Playable; no other engine found | Pick |
| Master of Orion | [1oom-fork/1oom](https://github.com/1oom-fork/1oom) | 112 | 2026-09-02 | Active continuation, in Debian (ppc64el) | **Pick** |
| | [darkstar/1oom](https://github.com/darkstar/1oom) (original; the doc's link) | 12 | 2021-06-04 | Stale | replace link |
| | [FreeOrion](https://github.com/freeorion/freeorion) (inspired, different game) | 1.1k | 2026-10-19 | Active, C++/Python | not MoO |
| X-COM UFO/TFTD | [OpenXcom](https://github.com/OpenXcom/OpenXcom) | 2.2k | 2026-09-28 | Vanilla engine | **Pick** |
| | [OXCE](https://github.com/MeridianOXC/OpenXcom) (superset fork, most mods) | 210 | 2026-09-29 | Active | drop-in later |
| X-COM Apocalypse | [OpenApoc](https://github.com/OpenApoc/OpenApoc) | 599 | 2026-09-16 | Playable, C++ | Only option |
| Jagged Alliance 2 | [Stracciatella](https://github.com/ja2-stracciatella/ja2-stracciatella) | 544 | 2026-10-16 | Playable | Pick |
| | JA2 v1.13 (org 1dot13 on GitHub, original code, Windows) | ? | ? | Moved from SVN 2022 | |
| Civilization I | OpenCiv1, now [OpenCivOne on Codeberg](https://codeberg.org/rhorvat/OpenCivOne) (C#/.NET, ~204 stars on GitHub before the move) | 1 (Codeberg) | 2026-10-20 | Active | |
| Civilization II | [Civ2-clone](https://github.com/axx0/Civ2-clone) (C#/Raylib) | 108 | 2026-08-07 | Partial | |
| | [CivOne](https://github.com/CivOne/CivOne) | 0 | 2022-02-15 | Stale | |
| | [freeciv](https://github.com/freeciv/freeciv) (inspired, ships rulesets for Civ1/Civ2; we already ship it) | 1.6k | 2026-10-20 | Complete, C | **Pick** |
| Age of Empires II | [openage](https://github.com/SFTtech/openage) | 14.5k | 2026-10-15 | Gameplay broken, GL 3.3, Qt6 | most popular, Watch |
| | [freeaoe](https://github.com/sandsmark/freeaoe) | 241 | 2021-08-03 | Inactive | |
| | [folkertvanverseveld/aoe](https://github.com/folkertvanverseveld/aoe) (AoE 1 + Rise of Rome, partial disassembly) | 129 | 2026-09-20 | Active, early | |
| | 0 A.D. (inspired, own game; GitHub mirror [0ad/0ad](https://github.com/0ad/0ad) archived; main repo at gitea.wildfiregames.com) | 2.8k | 2024-08-17 (mirror) | Complete, C++, GL 2.1 capable | inspired, not AoE |
| Total Annihilation | [Beyond All Reason](https://github.com/beyond-all-reason/Beyond-All-Reason) | 4.3k | 2026-10-20 | Game on Recoil, GL 3+ | too heavy |
| | [RecoilEngine](https://github.com/beyond-all-reason/RecoilEngine) (ex-Spring) | 707 | 2026-10-20 | Engine | |
| | [Zero-K](https://github.com/ZeroK-RTS/Zero-K) | 873 | 2026-10-19 | Game on Recoil | |
| Stronghold | [Sourcehold](https://github.com/Sourcehold/Sourcehold) | 280 | 2024-07-08 | Frozen | |
| | Stone Kingdoms ([GitLab](https://gitlab.com/stone-kingdoms/stone-kingdoms), LOVE/Lua) | ? | forks active 2026 | In development | |
| | [OpenSHC](https://github.com/gynt/OpenSHC) (Crusader modding base) | 0 | 2026-10-17 | Early | |
| Outpost 2 | [OpenOP2](https://github.com/OpenOP2/OpenOP2) (OpenRA mod) | 16 | 2025-05-25 | WIP | Only option |

Nothing found (searched again): Warcraft III decompilation, Alpha Centauri, Master of Orion 2 (only MoO1 projects), Cossacks, Heroes 4, Disciples, Spellforce, Rise of Nations, Empire Earth, Battle Realms, Commandos, Desperados, Populous TB, Settlers 4, Anno 1503, Railroad Tycoon 2/3, Tropico.

## RPG

| Game | Project (URL) | Stars | Last push | State | Pick |
|---|---|---|---|---|---|
| Diablo + Hellfire | [DevilutionX](https://github.com/diasurgical/devilutionX) | 9.9k | 2026-10-20 | Complete, very active, BE prior art (Tiger PPC) | **Pick** |
| | [devilution](https://github.com/diasurgical/devilution) (matching decompilation, Windows exe) | 9.0k | 2025-09-15 | Finished; source for DevilutionX | source |
| | [freeablo](https://github.com/wheybags/freeablo) | 2.2k | 2023-04-13 | Stalled | |
| Fallout 1 | [fallout1-ce](https://github.com/alexbatalov/fallout1-ce) | 2.9k | 2025-01-15 | Complete (Sustainable Use License) | Pick |
| Fallout 2 | [fallout2-ce](https://github.com/alexbatalov/fallout2-ce) | 2.5k | 2025-02-16 | Complete; quiet since Feb 2025 | Pick |
| | [fallout2-ce org repo](https://github.com/fallout2-ce/fallout2-ce) (seems the active continuation; relation not confirmed) | 153 | 2026-10-20 | Active | check |
| Fallout 1/2 | [Falltergeist](https://github.com/Falltergeist/Falltergeist) | 887 | 2022-07-15 | Unfinished | |
| | [sfall](https://github.com/sfall-team/sfall) (extender for the original exe, Windows only) | 444 | 2026-10-03 | Not an engine | |
| Arcanum | [arcanum-ce](https://github.com/alexbatalov/arcanum-ce) | 171 | 2026-05-13 | WIP | Only option |
| BG/IWD/PST | [GemRB](https://github.com/GemRB/gemrb) | 1.3k | 2026-10-19 | Playable; no other IE engine found | Pick |
| M&M 6/7/8 | [OpenEnroth](https://github.com/OpenEnroth/OpenEnroth) | 873 | 2026-10-11 | MM7 playable; no other found | Pick |
| Morrowind | [OpenMW](https://github.com/OpenMW/openmw) (GitHub mirror; home is [GitLab](https://gitlab.com/OpenMW/openmw), 948 stars) | 6.7k | 2026-10-20 | Complete | **Pick** |
| | [TES3MP](https://github.com/TES3MP/openmw-tes3mp) (multiplayer fork of OpenMW) | 872 | 2024-02-13 | Behind OpenMW | |
| Gothic 1/2 | [OpenGothic](https://github.com/Try/OpenGothic) | 1.6k | 2026-10-18 | Playable, Vulkan only | only active one |
| | [REGoth](https://github.com/REGoth-project/REGoth) | 653 | 2019-04-18 | Superseded | |
| | [REGoth-bs](https://github.com/REGoth-project/REGoth-bs) | 499 | 2020-05-29 | Stalled | |
| Arena | [OpenTESArena](https://github.com/afritz1/OpenTESArena) | 1.4k | 2026-10-08 | Pre-alpha, software renderer | Only option |
| Daggerfall | [Daggerfall Unity](https://github.com/Interkarma/daggerfall-unity) | 3.5k | 2026-09-14 | Complete, Unity | not portable |
| | [XL Engine](https://github.com/luciusDXL/XL-Engine) | 135 | 2020-04-23, archived | Dead | |
| Star Control II | The Ur-Quan Masters (SourceForge; mirrors and mod forks not checked) | ? | | Complete, shipped | done |
| Albion | [UAlbion](https://github.com/csinkers/ualbion) | 171 | 2026-05-04 | "Somewhat playable", .NET | Only option |
| Betrayal at Krondor | [BaKGL](https://github.com/xavieran/BaKGL) (xBaK successor) | 73 | 2026-09-16 | Active | Only option |
| Dink Smallwood | GNU FreeDink (Savannah; only a dead GitHub mirror, 0 stars) | n/a | | Complete, GPL | Only option |
| Diablo II | [OpenDiablo2](https://github.com/OpenDiablo2/OpenDiablo2) (Go) | 11.1k | 2021-10-21, archived | Dead | |
| | [OpenD2](https://github.com/eezstreet/OpenD2) (C++) | 605 | 2021-05-31 | Dead | |
| | [D2MOO](https://github.com/ThePhrozenKeep/D2MOO) (decomp of game DLLs, Windows) | 185 | 2026-10-19 | Active, not a port | |

## Adventure, sim, casual (and Ultima, Sims, SimCity 4, Spore again)

| Game | Project (URL) | Stars | Last push | State | Pick |
|---|---|---|---|---|---|
| LEGO Island | [isle-portable](https://github.com/isledecomp/isle-portable) | 1.1k | 2026-10-18 | Active, many platforms, GL 1.1/GLES/software | **Pick** |
| | [isle](https://github.com/isledecomp/isle) (decompilation, Windows) | 3.5k | 2026-09-18 | Source for the port | |
| Space Cadet Pinball | [SpaceCadetPinball](https://github.com/k4zmu2a/SpaceCadetPinball) | 4.7k | 2024-08-21 | Complete | Pick |
| Windows adventures | [ScummVM](https://github.com/scummvm/scummvm) | 2.9k | 2026-10-20 | Runs on our PS3 | Pick |
| SimCity 1 | [micropolis](https://github.com/SimHacker/micropolis) | 1.1k | 2026-02-10 | Official GPL, historical | Pick |
| | [MicropolisCore](https://github.com/SimHacker/MicropolisCore) | 211 | 2026-10-19 | C++ rewrite | |
| SimCity 2000 | [OpenSC2K](https://github.com/nicholas-ochoa/OpenSC2K) (older [pdeva copy](https://github.com/pdeva/OpenSC2K): 94, 2018) | 5.3k | 2026-10-15 | No simulation yet (JS) | Only option |
| SimCity 3000/4 | nothing found | | | | none |
| Theme Park World | [OpenTPW](https://github.com/OpenTPW/OpenTPW) | 161 | 2026-09-07 | Not playable | Only option |
| Zoo Tycoon | [OpenZT](https://github.com/openztcc/OpenZT) (plus tool GrAPE) | 37 | 2026-09-13 | Early; was listed as "nothing found" | Only option |
| Minesweeper | [ReversingMinesweeper](https://github.com/repnz/ReversingMinesweeper) | 146 | 2019-06-25 | Reconstructed C | |
| | ReactOS winmine (in [ReactOS](https://github.com/ReactOS/reactos), 18.3k, active) | | | Open C source of a Windows Minesweeper | alternative |
| Lemmings 2/3, Incredible Machine, SkiFree, Moorhuhn, LEGO Loco, Chessmaster | nothing found | | | | none |
| Ultima VII (I + II) | [Exult](https://github.com/exult/exult) | 694 | 2026-10-12 | Complete, C++/SDL | **Pick** |
| Ultima IV | [xu4 on Codeberg](https://codeberg.org/xU4/xu4) (GitHub [xu4-engine/u4](https://github.com/xu4-engine/u4) archived: 73, 2025-06-05) | 1 | 2026-02-28 | Complete; also in ScummVM | via ScummVM |
| Ultima VI | [Nuvie](https://github.com/nuvie/nuvie) | 165 | 2025-03-12 | Fully playable; also in ScummVM | via ScummVM |
| Ultima VIII | Pentagram (SourceForge, no GitHub found) | n/a | | Playable-ish; in ScummVM | via ScummVM |
| Ultima IV/VI/VIII | [ScummVM](https://github.com/scummvm/scummvm) Ultima engines | 2.9k | 2026-10-20 | Already on PS3 | **Pick** |
| Ultima Underworld 1/2 | [UnderworldGodot](https://github.com/hankmorgan/UnderworldGodot) | 331 | 2026-09-30 | Pre-alpha, Godot | |
| | [UnderworldExporter](https://github.com/hankmorgan/UnderworldExporter) (Unity predecessor) | 340 | 2024-02-14, archived | Dead | |
| | [Underworld Adventures](https://github.com/vividos/UnderworldAdventures) | 75 | 2026-07-30 | C++/SDL/GL, dormant since 2007 but new commits | **Pick** (C++, GL) |
| | [uw-engine](https://github.com/khedoros/uw-engine) | 9 | 2024-10-04 | Hobby | |
| The Sims | [FreeSO](https://github.com/riperiperi/FreeSO) (Sims Online engine, base of Simitone) | 986 | 2025-08-22 | .NET/MonoGame | |
| | [Simitone](https://github.com/riperiperi/Simitone) | 500 | 2025-10-11 | Sims 1 on FreeSO, .NET | Only Sims 1 option |
| The Sims 2 | [OpenTS2](https://github.com/LazyDuchess/OpenTS2) (Unity) | 437 | 2026-07-25 | Active | Only option |
| SimCity 4 | nothing found | | | | none |
| Spore | no engine found; [SPORE-ModAPI](https://github.com/Spore-Community/SPORE-ModAPI) (mods for the original exe) | 86 | 2026-10-16 | Not a port | none |
| | [Thrive](https://github.com/Revolutionary-Games/Thrive) (Godot, inspired, own game) | 3.8k | 2026-10-20 | Active | not Spore |
| Age of Empires II | see Strategy table | | | | |

## Sources

- GitHub repository pages, REST API and `commits.atom` feeds for every URL above; GitHub search for discovery.
- Codeberg API (Librelancer, xU4, OpenCivOne), GitLab API (Gardens of Kadesh, OpenMW), voidpoint.io API (EDuke32).
- [awesome-decomp-games](https://github.com/SubmitGame/awesome-decomp-games) (discovery).
- Heretic2R: [GamingOnLinux](https://www.gamingonlinux.com/2026/03/heretic-ii-has-a-new-reverse-engineered-source-port); Postal: [Debian postal1](https://ftp-master.debian.org/new/postal1_2015.git20250526+ds-1.html); re3's takedown history: [TheGamer](https://www.thegamer.com/reverse-engineered-gta-code-restored-dmca/); FreeSynd: [freesynd.sourceforge.io](https://freesynd.sourceforge.io/); FreeDink: [gnu.org](https://www.gnu.org/software/freedink/).
