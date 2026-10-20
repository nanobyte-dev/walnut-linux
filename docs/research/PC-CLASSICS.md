# Windows PC classics (1990-2010) with open engines

Popular PC games of 1990-2010 that now have an open-source engine, a decompilation, a reimplementation
or an official source release, as candidates for the PS3. Researched 2026-10-20 from the projects'
READMEs, osgameclones.com, PCGamingWiki, the awesome-unofficial-pc-ports list, GamingOnLinux and news
articles. Nothing here has been built or measured on our console yet.

The user's focus: games that never had a console port (marked **no** in the Console column) come first,
since a PS3 port of those is new on a console. The user's favourites from this era: Jazz Jackrabbit 2,
Elasto Mania, Atomic Bomberman, Hercules, Tarzan, NFS 2-6, U1/U2 (Unreal or Ultima, open), The Sims,
Age of Empires II.

## How to read this

The rules (the user, 2026-10-20):
- **The graphics API is never a veto.** GL 3, Vulkan, D3D: the renderer gets ported to GL 2.1 with
  effects dropped. It is a cost in the Effort column.
- **Memory under 2-3 GB on a PC is a candidate.** Textures get compressed (DXT) and downscaled, and VRAM
  swap helps. The PS3 has about 217 MB usable.
- **Takedowns don't matter.** re3/reVC are candidates like any other.
- **Original game data is fine.** It never goes into git or the public apt suite. The goal
  ([PLAN](../PLAN.md) 5.10): the engine package is public and copies the assets from the user's CD, disc
  image or existing install on first run, through a "First time setup" dialog. Free data (shareware,
  freeware, demo, own assets) is marked, since those packages can be complete out of the box.

What still rules a game out: no playable open engine, or an engine that still needs the original x86
executable to run.

Columns:
- **Console**: whether the game itself had a console port in its time (from general knowledge, not
  re-checked). "different" means a console game of the same name that is not this PC game.
- **BE prior art**: a big-endian port (PowerPC Mac, Wii, Wii U, GameCube, Amiga, MorphOS) that already
  fixed the byte order. With it, our port is mostly a build; without it, expect byte-order bugs in file
  loading (OpenRCT2 needed this work).
- **Effort** on the PS3, a guess until each is tried:
  - **S**: build, fix what breaks, tune.
  - **M**: byte-order fixes, or a small renderer port.
  - **L**: a big renderer port, a runtime (Mono, Java natives), or heavy memory cuts.
  - **XL**: the engine itself is unfinished.
- **(?)**: from one secondary source or general knowledge, not confirmed in the project's README.

## Shortlist

Never on a console, ranked by effort:

| # | Game | Project | Why | Effort |
|---|---|---|---|---|
| 1 | 3D Pinball Space Cadet (1995) | SpaceCadetPinball | Complete; Wii, Wii U, MorphOS and AmigaOS4 ports exist; tiny | S |
| 2 | Atomic Bomberman (1997) | atomic-bomberman (elraro) | Playable, ships its own free graphics and sound, so it can go in the public suite; GL 3.3 2D renderer | M |
| 3 | Chris Sawyer's Locomotion (2004) | OpenLoco | "Completed reimplementing the game in C++" (Dec 2025); OpenTTD/OpenRCT2 family, both already run on the PS3 | M |
| 4 | LEGO Island (1997) | isle-portable | Active; has an OpenGL 1.1 backend; light 1997 game | M |
| 5 | Jazz Jackrabbit 2 (1998) | Jazz² Resurrection | Playable full game; 2D GL 3.3 renderer to port | M |
| 6 | Fallout 1/2 (1997/98) | fallout1-ce, fallout2-ce | Complete; already packaged for powerpc64 by OpenBSD and Nix | M |
| 7 | Caesar III (1998), Pharaoh (1999) | Julius / Augustus, Akhenaten | Complete (Julius), in progress (Akhenaten); SDL2, light | M |
| 8 | Heroes of Might and Magic II (1996) | fheroes2 | Complete, very active, SDL2, free demo data | M |
| 9 | Jedi Knight: Dark Forces II (1997) | OpenJKDF2 | Reimplementation with Dreamcast and DSi ports; needs a GL 2.1 renderer | M |
| 10 | Half-Life 1 / CS 1.6 (1998/2000; had PS2 and Xbox versions) | Xash3D FWGS | GL 1.x renderer; a big-endian fork runs HL on Mac OS 9 PPC | M |
| 11 | Warcraft III (2002) | Warsmash | Java/LibGDX rewrite, alpha | L |
| 12 | Descent 3 (1999) | Descent 3 (GPL 2024) | Official source; GL 1.x-era renderer; shipped on PPC Macs originally | M |
| 13 | Black & White (2001) | openblack | Active reimplementation; bgfx (GL/Vulkan); heavy | L |
| 14 | Might and Magic 7 (1999) | OpenEnroth | MM7 playable; GL renderer to check | L |
| 15 | Homeworld (1999) | HomeworldSDL | Official source; fixed-function GL; CPU is the question | M-L |

Had console versions, but strong and cheap on the PS3: re3/reVC (GTA III/VC, Wii and Wii U ports exist,
librw has GL 2.1), dethrace (Carmageddon, big-endian fixes upstream), DevilutionX (Diablo, Mac OS X Tiger
PPC build), the Sonic RSDK decompilations, TRX (Tomb Raider 1-3), Vanilla Conquer (C&C TD/RA, freeware data).

## The user's favourites

| Game | Console | Project | State | Verdict |
|---|---|---|---|---|
| Jazz Jackrabbit 2 (1998) | no | [Jazz² Resurrection](https://github.com/deathkiller/jazz2-native) | Playable full game, maintained | Candidate: GL 3.3 to 2.1 port of a 2D renderer (M) |
| Jazz Jackrabbit 1 (1994) | no | OpenJazz | Playable; Wii and 3DS ports (?) | S; free shareware data |
| Elasto Mania (2000) | Remastered 2021 | none for Elma; the developers released the source of its predecessor, [Action SuperCross](https://github.com/elastomania/across) | X-Moto (we ship it) is the open clone | Check Action SuperCross's platform and license |
| Atomic Bomberman (1997) | no | [atomic-bomberman](https://github.com/elraro/atomic-bomberman) (elraro) | "Playable": every level, AI, network; "nobody has play-tested it thoroughly"; C++20, SDL3, GL 3.3, GPL-3 | Candidate (M); free assets |
| Hercules (1997), Tarzan (1999) | yes (PS1) | nothing found | | Not possible |
| Need for Speed II (1997) | yes | [re-nfs2](https://github.com/thiekus/re-nfs2) | Early; hooks the original Windows exe through a winmm.dll wrapper | Watch |
| NFS II SE / III (1997/98) | yes | [nfs-recompiled](https://github.com/motor-dev/nfs-recompiled) | Static recompilation, "a toy project", mostly AI-generated | Watch |
| NFS Underground 2 (2004) | yes | [speed-re](https://github.com/troikoss/speed-re) | "Incomplete decompilation"; Linux driving demo still loads the 32-bit retail exe | Watch |
| NFS Most Wanted (2005) | yes | GameCube matching decompilation (console code, big-endian PPC) | The PC side has only a plugin SDK | Watch: the GC decomp is PPC code already |
| NFS 1-6 | yes | [OpenNFS](https://github.com/AmrikSadhra/OpenNFS) | Loads tracks, "extremely early" | Watch |
| Unreal / UT99 | UT99 yes (PS2, DC) | [SurrealEngine](https://github.com/dpjudas/SurrealEngine) | UT v436 and Unreal Gold v226 "relatively playable"; many native functions missing; D3D11, Vulkan, GL 3.2 | XL |
| UT2004 (2004) | no | no open engine; OldUnreal's official revival is closed source, x86-64 and aarch64 binaries only | | Not possible |
| Ultima VII (1992) | SNES (cut down) | Exult | Complete, old BE-friendly code (?) | S |
| Ultima IV, VI, VIII | | ScummVM (Ultima engine), Pentagram | U8 finishable "not everything fully working" | S via ScummVM |
| Ultima Underworld 1/2 | no | Underworld Adventures (dormant since 2007), UnderworldGodot (pre-alpha) | | XL |
| The Sims (2000) | different | [Simitone](https://github.com/riperiperi/Simitone) on FreeSO | Upstream Windows-only; Linux fork on .NET 9 + MonoGame | L: no .NET for big-endian ppc64 (Mono route) |
| The Sims 2 (2004) | different | [OpenTS2](https://github.com/LazyDuchess/OpenTS2) | Unity reimplementation, active (Mar 2026) | XL: Unity has no PPC runtime |
| SimCity 4 (2003) | no | nothing found | | Not possible |
| Age of Empires II (1999) | PS2 (2001) | [openage](https://github.com/SFTtech/openage) (C++20, Qt6, Python; GL 3.3) | Gameplay "basically non-functional" while the simulation is rebuilt; v0.5.0 (Oct 2023) was "not much more than a tech demo"; needs an AoE II TC, HD or DE install. freeaoe is inactive (?). No decompilation found | Watch: XL until openage plays again |

## Action and shooters

| Game (year) | Console | Project | State | Renderer | BE prior art | Data | Effort |
|---|---|---|---|---|---|---|---|
| GTA III (2001), Vice City (2002) | yes | re3 / reVC | Fully playable | librw: GL 2.1+, GLES 2, D3D9 | **yes**: Wii U and native Wii ports (2026) | own | S-M |
| Jedi Knight: Dark Forces II (1997) | no | [OpenJKDF2](https://github.com/shinyquagsire23/OpenJKDF2) | 95.9% reimplemented excluding the rasterizer | GL 3.3, GL 1.1 (32-bit Win), GLES 3; Dreamcast, DSi | no (but scaled down to DC) | own (GOG) | M |
| Dark Forces (1995), Outlaws (1997) | DF: PS1; Outlaws: no | [TheForceEngine](https://github.com/luciusDXL/TheForceEngine) | v1.0 playable (DF); Outlaws in progress | GL plus software renderer | unknown | own | M |
| Indiana Jones and the Infernal Machine (1999) | N64 | OpenJones3D | early (?) | ? | unknown | own | XL |
| Jedi Outcast / Academy (2002/03) | yes | [OpenJK](https://github.com/JACoders/OpenJK) | playable | GL 2.x | **packaged for ppc/ppc64 on OpenBSD, NetBSD, FreeBSD** | own | M-L (RAM) |
| Half-Life / CS 1.6 | yes | [Xash3D FWGS](https://github.com/FWGS/xash3d-fwgs) | playable | GL 1.x/2.x, GLES, software | ppc64le upstream; a Mac OS 9 PPC fork | own | M |
| Half-Life 2, Portal (2004/07) | yes | [nillerusr/source-engine](https://github.com/nillerusr/source-engine) (from the 2020 leak) | builds and runs on Linux, Android, ARM | ToGL / ToGLES (D3D9 to GL) | no | own | L (RAM, CPU) |
| Doom 3 (2004) | Xbox | dhewm3 | playable | GL ARB2 paths | ? | own | L (RAM) |
| RTCW, Wolfenstein: ET, Quake 3, Elite Force | mostly yes; ET no | iortcw, ET: Legacy, ioquake3 | playable | GL 1.x/2.x | **Q3's QVM had a big-endian PPC JIT** | own; **ET is free** | M |
| Heretic II (1998) | no | Heretic2R (2026) | single player completable | Q2-style GL (?) | unknown | own (v1.06) | M |
| Hexen II (1997) | no | uHexen2 / Hammer of Thyrion | playable | GL 1.x | BE builds (?) | own or demo | S |
| Serious Sam TFE/TSE (2001/02) | Xbox | [Serious Engine 1](https://github.com/tx00100xt/SeriousSamClassic) (official 2016) | playable | GL 1.x/2.x | unknown | own | L (CPU, crowds) |
| Duke 3D, Shadow Warrior, Blood, Redneck Rampage | Duke yes; others no | EDuke32, VoidSW, NBlood, Rednukem | playable | Polymost GL plus software | PPC Mac history (?) | Duke shareware, SW freeware | S |
| Rise of the Triad (1994) | no | rott | playable | SDL software | ? | shareware | S |
| Descent 1/2 | PS1 | DXX-Rebirth | playable | GL 1.x/2.x | PPC Mac history (?) | shareware | S |
| Descent 3 (1999) | no | [Descent3](https://github.com/DescentDevelopers/Descent3) (GPL 2024) | playable; cutscene decoders stripped | GL 1.x era | original shipped on PPC Mac | own (GOG) | M |
| Forsaken (1998) | yes | ForsakenX | playable (released public domain 2007) | GL (?) | unknown | own | M |
| Postal (1997) | no | official source (GPL 2016) | playable (?) | SDL2 software | engine was on Mac 68k/PPC (?) | Postal Classic is free | S-M |
| Hitman: Codename 47 (2000) | no | [re47](https://github.com/0danny/re47) | early | D3D | | own | XL |
| Halo CE (2001) | Xbox | [halo-re](https://github.com/halo-re/halo) | patches the Xbox XBE | | | own | XL |

## Racing

| Game (year) | Console | Project | State | Renderer | BE prior art | Data | Effort |
|---|---|---|---|---|---|---|---|
| Carmageddon (1997) | different | [dethrace](https://github.com/dethrace-labs/dethrace) | 92-94% of functions; playable | SDL2 software, GL option | **yes**: big-endian fixes upstream (2022, 2024), Amiga 68k port | own; demos free | S-M |
| Re-Volt (1999) | yes | RVGL | playable, online | legacy GL, SDL2 | Nix lists powerpc for the launcher (?) | own; community packs free | closed source: not portable unless released |
| Midtown Madness (1999) | no | [Open1560](https://github.com/0x1F9F1/Open1560) | partial rewrite, x86 assembly for the rest | has a GL renderer | | own | XL |
| Star Wars Episode I: Racer (1999) | N64, DC | [SW_RACER_RE](https://github.com/tim-tim707/SW_RACER_RE) | 71% (Jun 2026), dinput.dll hook into the GOG exe | D3D6 | | own | Watch |
| Death Rally (1996) | no | [dRally](https://github.com/urxp/drally) | WIP, no multiplayer | SDL | **Amiga port** | freeware | M |
| NFS titles | yes | see the favourites table | | | | | Watch |

Nothing found: Midtown Madness 2, Motocross Madness, Moto Racer, Toca, Colin McRae, Rollcage, Crazy Taxi,
Flatout, Trackmania, Stunt GP, POD, Viper Racing, Grand Prix 2.

## Flight and space

| Game (year) | Console | Project | State | Renderer | Data | Effort |
|---|---|---|---|---|---|---|
| X-Wing, TIE Fighter, X-Wing Alliance (1993-99) | no | [OpenXW](https://github.com/elyosh/OpenXW), [OpenTIE](https://github.com/elyosh/OpenTIE), OpenXWA | active, playable with bugs (2026) | D3D12, Vulkan, Metal only (SDL3 GPU) | own | L (new GL 2.1 backend; simple games) |
| Freelancer (2003) | no | [Librelancer](https://codeberg.org/Librelancer/Librelancer) | first mission playable (2024); moved to Codeberg | GL, C#/.NET | own | L (runtime) |
| FreeSpace 1/2 (1998/99) | no | FreeSpace Open | playable | GL (modern) | own; FS2 demo free | L |
| Privateer (1993) | no | Gemini Gold (Vega Strike) | 1.0 released | GL | own assets | L |

Nothing found: Wing Commander 3/4, Tachyon, Independence War, Starlancer, MechWarrior.

## Platformers and 2D action

| Game (year) | Console | Project | State | Renderer | BE prior art | Data | Effort |
|---|---|---|---|---|---|---|---|
| Tomb Raider 1-3 (1996-98) | yes | [TRX](https://github.com/LostArtefacts/TRX) | all three fully playable; TR4 early | GL (3.3 core optional) | unknown | own | M |
| Sonic CD (2011 PC) | yes | [RSDKv3 decompilation](https://github.com/RSDKModding/RSDKv3-Decompilation) | complete | SDL2 software, optional GL | Switch, 3DS, Vita forks | own (Steam) | S-M |
| Sonic 1 and 2 (2013) | yes | [RSDKv4 decompilation](https://github.com/Rubberduckycooly/Sonic-1-2-2013-Decompilation) | complete | SDL2 software | same | own | S-M |
| Captain Claw (1997) | no | [OpenClaw](https://github.com/pjasicek/OpenClaw) | rewrite, SDL2 + Box2D, last push 2022 | SDL2 | unknown | own | M |
| Abe's Oddysee / Exoddus (1997/98) | yes | [R.E.L.I.V.E.](https://github.com/AliveTeam/alive_reversing) | both being reimplemented | SDL2 | unknown | own | M |
| Heart of Darkness (1998) | PS1 | hode | playable (?) | SDL | ? | own | S |
| Cave Story (2004) | later | doukutsu-rs | complete | GL / SDL2 backends | Rust on ppc64 BE is a risk | freeware | M |
| VVVVVV (2010) | later | official source | complete | SDL2 | Wii port (?) | free Make and Play data | S |
| Crimsonland (2003) | later remake | [crimson](https://github.com/banteg/crimson) | full game, rewritten in Python + raylib | raylib | | own | L (Python too slow on the PPE) |
| Soldat (2002) | no | OpenSoldat | "not in a finished state" | SDL2 GL, Free Pascal | | free | L (Pascal toolchain) |
| Beats of Rage engine | n/a | OpenBOR | active | SDL software | console ports (PSP) | free mods | S |
| M.U.G.E.N engine | n/a | Ikemen GO | active | GL, Go | | fan content | L (Go on BE) |

Nothing found: Max Payne, Rayman 2, Croc, Earthworm Jim 3D, Prince of Persia 3D, LEGO Racers, LEGO Rock
Raiders, Crash/Spyro PC, Worms, Deus Ex, Thief and System Shock 2 (OpenDarkEngine only shows levels),
Mafia (MafiaUnity archived), NOLF, Blood 2, Postal 2.

## Strategy

| Game (year) | Console | Project | State | Renderer | BE prior art | Data | Effort |
|---|---|---|---|---|---|---|---|
| Chris Sawyer's Locomotion (2004) | no | [OpenLoco](https://github.com/OpenLoco/OpenLoco) | complete (Dec 2025), multiplayer next | SDL3 software drawing | no; Linux release 64-bit | own (Steam, GOG) | M |
| StarCraft: Brood War (1998) | N64 | OpenBW | complete deterministic engine; playable browser build (Jul 2026) | SDL 2D | no | **free** (Blizzard's free download, 3 MPQs) | M |
| Warcraft I, II (1994/95) | WC2: PS1, Saturn | War1gus, [Wargus](https://github.com/Wargus/wargus) on Stratagus | works | SDL2 / GL | unknown | own (DOS version) | S-M |
| Warcraft III (2002) | no | [Warsmash](https://github.com/Retera/WarsmashModEngine) | alpha, clean-room, Java + LibGDX | GL (LibGDX) | no | own (patch 1.22-1.26) | L (Java natives on BE) |
| C&C Tiberian Dawn, Red Alert (1995/96) | yes | [Vanilla Conquer](https://github.com/TheAssemblyArmada/Vanilla-Conquer) (official GPL) | runs on Win, Linux, macOS, BSD | software / GL | **MorphOS Red Alert port** fixed many BE issues (a fork) | **freeware** | M |
| C&C Generals / Zero Hour (2003) | no | [GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) (official GPL 2025) | active; Linux only via Docker | DX8 | no | own | XL (DX8 port, CPU, RAM) |
| C&C Renegade (2002) | no | official GPL source (2025) | code only | DX8 | no | own | XL |
| Perimeter (2004) | no | [Perimeter](https://github.com/KD-lab-Open-Source/Perimeter) (official) | playable, some parts experimental | D3D via DXVK (Vulkan) | no | own | L |
| Homeworld (1999) | no | [HomeworldSDL](https://github.com/HomeworldSDL/HomeworldSDL) (official 2003) | works on Linux, Win, macOS | fixed-function GL | ? | own | M-L |
| Black & White (2001) | no | [openblack](https://github.com/openblack/openblack) | active reimplementation | bgfx (GL, Vulkan) | no | own (patched 1.2) | L |
| Dungeon Keeper (1997) | no | [KeeperFX](https://github.com/dkfans/keeperfx) | active; Linux port community alpha | software / SDL | no | own | L (immature Linux port) |
| Dungeon Keeper 2 (1999) | no | OpenKeeper | incomplete, Java + jMonkeyEngine | GL | no | own | XL |
| Magic Carpet 2 (1995) | no | [remc2](https://github.com/turican0/remc2) | "all of MC2 seems to be playable" | SDL2 software | no | own (GOG) | M-L |
| Syndicate Wars (1996) | PS1 | [swars](https://github.com/swfans/swars) | single player complete | software | no; x86 32-bit only | own | L (x86 asm) |
| Syndicate (1993) | yes | FreeSynd | playable | SDL | ? | own | S-M |
| Theme Hospital (1997) | PS1 | CorsixTH | playable | SDL2 | ? | own; free demo | S-M |
| Settlers 2 (1996) | DS | Return to the Roots | playable | SDL / GL | ? | own | M |
| Settlers 3 (1998) | no | [JSettlers](https://github.com/paulwedeck/settlers-remake) | alpha, Java | Java2D / LWJGL | no | own or demo | L |
| Caesar III (1998) | no | [Julius](https://github.com/bvschaik/julius) / Augustus | complete | SDL2 software | Nix lists powerpc64 (untested) | own | M |
| Pharaoh + Cleopatra (1999) | no | [Akhenaten](https://github.com/dalerank/Akhenaten) | in progress: early campaign plays | SDL2 | no | own (patch 1.3) | M |
| Heroes of Might and Magic II (1996) | no | [fheroes2](https://github.com/ihhub/fheroes2) | complete | SDL2 | no | own; free demo | M |
| Heroes of Might and Magic III (1999) | no | VCMI | playable | SDL2 | ? | own | M-L (RAM, Boost) |
| Master of Orion (1993) | no | [1oom](https://github.com/darkstar/1oom) | complete, in Debian | SDL software | ppc64el build only | own | S |
| X-COM UFO Defense / TFTD | PS1 | OpenXcom | playable | SDL | ? | own | M |
| Jagged Alliance 2 (1999) | no | JA2 Stracciatella | playable (C++ and Rust) | SDL2 | ? | own | M-L (Rust on BE) |
| Civilization I, II | SNES, PS1 | CivOne, Civ2-clone | partial; C#/.NET | | | own | L |
| Age of Empires II (1999) | PS2 | openage | engine in development | GL 3.3, Python | | own | XL |
| Total Annihilation (1997) | no | Recoil / Beyond All Reason (inspired) | active | GL 3+ | | free content | L (too heavy) |
| Stronghold (2001) | no | Sourcehold (frozen), Stone Kingdoms (LOVE, in development) | | | | | XL |
| Outpost 2 (1997) | no | OpenOP2 (an OpenRA mod) | WIP | OpenRA | | own | L (.NET) |
| C&C RA, Dune 2000, Tiberian Sun | yes | OpenRA | playable | GL 3.2 (old GL 2.1 path in git history), .NET | | free | L (.NET runtime) |

Nothing found: Warcraft III decompilation, Alpha Centauri, Master of Orion 2, Cossacks, Heroes 4,
Disciples, Spellforce, Rise of Nations, Empire Earth, Battle Realms, Commandos, Desperados, Populous: The
Beginning, Settlers 4, Anno 1503, Railroad Tycoon 2/3, Tropico. Tiberian Sun and Red Alert 2 were not in
EA's source release.

## RPG

| Game (year) | Console | Project | State | Renderer | BE prior art | Data | Effort |
|---|---|---|---|---|---|---|---|
| Diablo + Hellfire (1996) | PS1 | [DevilutionX](https://github.com/diasurgical/devilutionX) | complete, very active | SDL software | **Mac OS X Tiger PPC build** (45 fps on an iBook G4), Amiga 68k, a BE network fix | own; **shareware free** | S |
| Fallout 1/2 (1997/98) | no | fallout1-ce, [fallout2-ce](https://github.com/alexbatalov/fallout2-ce) | complete | SDL2 | packaged for powerpc64 (OpenBSD, Nix) | own | M |
| Arcanum (2001) | no | arcanum-ce | WIP (?) | SDL2 | | own | L |
| Baldur's Gate 1/2, Icewind Dale, Planescape: Torment | EE ports 2019 | GemRB | playable | SDL, GL ES 2 | **Mac OS PPC listed** | own | M |
| Might and Magic 7 (1999) | no | [OpenEnroth](https://github.com/OpenEnroth/OpenEnroth) | MM7 playable; MM6, MM8 planned | GL (version to check) | no | own (GOG) | L |
| Morrowind (2002) | Xbox | OpenMW | playable | OSG, near GL 2.1 | ? | own | L (RAM) |
| Gothic 1/2 (2001/02) | later Switch | OpenGothic | playable | Vulkan only | no | own | L-XL |
| Arena (1994) | no | OpenTESArena | pre-alpha | software | | **free** (Bethesda) | XL |
| Daggerfall (1996) | no | Daggerfall Unity | 1.1 | Unity | no | free | not portable (Unity) |
| Star Control II (1992) | 3DO | The Ur-Quan Masters | complete | SDL | Fedora ppc build (2007) | free | done: we ship it |
| Albion (1995) | no | UAlbion | "somewhat playable" (2020) | Veldrid, C#/.NET | | own | XL |
| Betrayal at Krondor (1993) | no | xBaK | alpha, not playable | SDL 1.2 | | own | XL |
| Dink Smallwood (1997) | no | FreeDink (?) | complete (?) | SDL2 | | freeware | S |
| Diablo II (2000) | no | OpenDiablo2 | archived 2021-23, Go | | | own | not possible |

## Adventure, sim, casual

| Game (year) | Console | Project | State | Notes | Effort |
|---|---|---|---|---|---|
| LEGO Island (1997) | no | [isle-portable](https://github.com/isledecomp/isle-portable) | active; Win, Linux, macOS, Web, Android, iOS, Vita, Switch, 3DS | OpenGL 1.1, GLES 2/3, software backends; own v1.1 data | M |
| 3D Pinball Space Cadet (1995) | no | [SpaceCadetPinball](https://github.com/k4zmu2a/SpaceCadetPinball) | complete | Wii, Wii U, MorphOS, AmigaOS4 ports; data from Windows XP | S |
| Windows-era adventures | various | ScummVM (already runs on our PS3) | | Blade Runner (1997), Grim Fandango (software renderer), Myst, Riven, The Longest Journey, Broken Sword, Curse of Monkey Island, Little Big Adventure, Zork Nemesis, Grand Inquisitor; own data | S |
| SimCity (1989) | SNES | Micropolis (official GPL) | complete | | S |
| SimCity 2000 (1993) | yes | OpenSC2K | views cities, no simulation (WebGL) | | XL |
| Theme Park World (1999) | PS1 | OpenTPW | not playable | | XL |
| Minesweeper | no | ReversingMinesweeper | reconstructed C | a new clone is less work | S |

Nothing found: SimCity 3000/4, Zoo Tycoon, The Incredible Machine, Lemmings 2/3, SkiFree source,
Moorhuhn, LEGO Loco, Chessmaster.

## Next steps

1. A cheap probe of the S and M picks: build each in the ppc64 builder, run on the PS3 with free data
   where it exists (Space Cadet needs the XP data; Atomic Bomberman, the Diablo shareware, ET: Legacy,
   Vanilla Conquer and Duke 3D shareware need nothing bought).
2. The user's answers: U1/U2 (Unreal or Ultima), and which games they own (CD, GOG, Steam).
3. Effort estimates for the GL 3.3 renderers (Jazz² Resurrection, Atomic Bomberman) the way
   [gl3-census/](gl3-census/) did for the earlier list.

## Sources

Project pages and READMEs: linked in the tables. Other sources:
- [osgameclones.com](https://osgameclones.com/), [awesome-unofficial-pc-ports](https://github.com/Sebastrion/awesome-unofficial-pc-ports), [Wikipedia: list of game engine recreations](https://en.wikipedia.org/wiki/List_of_game_engine_recreations)
- re3 on Wii: [Generation Amiga](https://www.generationamiga.com/2026/08/15/gta-vice-city-gets-an-unexpected-native-nintendo-wii-port/), [GTA Boom](https://www.gtaboom.com/gta-iii-now-runs-natively-on-the-original-wii-7302)
- DevilutionX on Tiger PPC: [MacRumors](https://spy.macrumors.com/threads/devilutionx-diablo-1-for-tiger-ppc.2449064/)
- Xash3D: [RaptorCS ppc64le](https://forums.raptorcs.com/index.php/topic,483.0.html), [Mac OS 9](https://zeli.app/story/49089814)
- Vanilla Conquer on MorphOS: [amiga-news](https://amiga-news.de/en/news/AN-2023-03-00035-EN.html)
- dethrace on Amiga: [itch.io](https://arcziii.itch.io/carmageddon-68k)
- Descent 3: [GamingOnLinux](https://gamingonlinux.com/2024/08/descent-3-open-source-project-gets-a-first-release)
- Heretic II: [GamingOnLinux](https://www.gamingonlinux.com/2026/03/heretic-ii-has-a-new-reverse-engineered-source-port)
- OpenXWA: [Generation Amiga](https://www.generationamiga.com/2026/08/01/openxwa-rebuilds-x-wing-alliance-for-windows-linux-and-macos/)
- OpenBW: [Team Liquid](https://www.teamliquid.net/forum/brood-war/517127-openbw-introduction?view=all), [browser build](https://ixbt.games/en/news/2026/07/26/issledovatel-ii-vypustil-brauzernuiu-versiiu-starcraft-na-dvizke-openbw-s-podderzkoi-multipleera.html)
- Warsmash: [Hive Workshop](https://www.hiveworkshop.com/threads/overview-and-setup.331776)
- UT2004 revival: [GamingOnLinux](https://gamingonlinux.com/2026/03/oldunreal-release-new-preview-update-for-the-classic-unreal-tournament-2004)
- EA's C&C source release: [Slashdot](https://games.slashdot.org/story/25/02/27/2144203/ea-releases-source-code-for-old-command-and-conquer-games)
- OpenRA's GL requirement: [release notes 2021](https://www.openra.net/news/release-20210321/)
- Elasto Mania / Action SuperCross: [elastomania.com](https://elastomania.com/news/10.html)
- OpenJK on PPC: [OpenBSD packages](https://www.openbsdhandbook.com/packages/release/games/openjk/)
- Q3 VM on big-endian: RaptorCS thread (Dec 2024, via the action sweep)
