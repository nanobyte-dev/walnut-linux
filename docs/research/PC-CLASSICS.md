# Windows PC classics (1990-2010) with open engines

Popular PC games of 1990-2010 that now have an open-source engine, a decompilation, a reimplementation
or an official source release, as candidates for the PS3. Researched 2026-10-20 from the projects'
READMEs, osgameclones.com, PCGamingWiki, the awesome-unofficial-pc-ports list, GamingOnLinux and news
articles. Nothing here has been built or measured on our console yet. Every alternative project per game,
with GitHub stars, last activity and the pick for our port, is in
[Alternatives and popularity](#alternatives-and-popularity).

The user's focus: games that never had a console port (marked **no** in the Console column) come first,
since a PS3 port of those is new on a console. The user's favourites from this era: Jazz Jackrabbit 2,
Elasto Mania, Atomic Bomberman, Hercules, Tarzan, NFS 2-6, U1/U2 (Unreal or Ultima, open), The Sims,
Age of Empires II, Spore.

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
| 15 | Homeworld (1999) | Gardens of Kadesh | Official source; fixed-function GL; CPU is the question | M-L |
| 16 | Elasto Mania (2000; Remastered only reached consoles in 2021) | elma-classic (official source, Dec 2025) | The real game's source from its developers; a favourite | to evaluate |
| 17 | NFS II SE (1997; had a PS1 version) | NFSIISE | A C++ reimplementation with 773 stars, active 2026; its state and renderer are unchecked | to evaluate |

Had console versions, but strong and cheap on the PS3: re3/reVC (GTA III/VC, Wii and Wii U ports exist,
librw has GL 2.1), dethrace (Carmageddon, big-endian fixes upstream), DevilutionX (Diablo, Mac OS X Tiger
PPC build), the Sonic RSDK decompilations, TRX (Tomb Raider 1-3), Vanilla Conquer (C&C TD/RA, freeware data).

## The user's favourites

| Game | Console | Project | State | Verdict |
|---|---|---|---|---|
| Jazz Jackrabbit 2 (1998) | no | [Jazz² Resurrection](https://github.com/deathkiller/jazz2-native) | Playable full game, maintained | Candidate: GL 3.3 to 2.1 port of a 2D renderer (M) |
| Jazz Jackrabbit 1 (1994) | no | OpenJazz | Playable; Wii and 3DS ports (?) | S; free shareware data |
| Elasto Mania (2000) | Remastered 2021 | **[elma-classic](https://github.com/elastomania/elma-classic)**: the developers' official "Elasto Mania Classic Source Code" (Dec 2025; active mirror elmadev/elma-classic); also the predecessor [Action SuperCross](https://github.com/elastomania/across) | The real game's source; platform, renderer and license to check | Candidate: evaluate first (X-Moto, which we ship, is the fallback) |
| Atomic Bomberman (1997) | no | [atomic-bomberman](https://github.com/elraro/atomic-bomberman) (elraro) | "Playable": every level, AI, network; "nobody has play-tested it thoroughly"; C++20, SDL3, GL 3.3, GPL-3 | Candidate (M); free assets |
| Hercules (1997), Tarzan (1999) | yes (PS1) | nothing found | | Not possible |
| Need for Speed II (1997) | yes | [re-nfs2](https://github.com/thiekus/re-nfs2) | Early; hooks the original Windows exe through a winmm.dll wrapper | Watch |
| NFS II SE (1997) | yes | **[NFSIISE](https://github.com/zaps166/NFSIISE)** (773 stars, active 2026) | A reimplementation in C++ | Candidate: evaluate (state, renderer, byte order) |
| NFS III (1998) | yes | [nfs-recompiled](https://github.com/motor-dev/nfs-recompiled) (static recompilation, "a toy project"), [nfs3-re](https://github.com/melroy89/nfs3-re) (decompilation) | Early | Watch |
| NFS Underground 2 (2004) | yes | [speed-re](https://github.com/troikoss/speed-re) | "Incomplete decompilation"; Linux driving demo still loads the 32-bit retail exe | Watch |
| NFS Most Wanted (2005) | yes | GameCube matching decompilation (console code, big-endian PPC) | The PC side has only a plugin SDK | Watch: the GC decomp is PPC code already |
| NFS 1-6 | yes | [OpenNFS](https://github.com/AmrikSadhra/OpenNFS) | Loads tracks, "extremely early" | Watch |
| Unreal / UT99 | UT99 yes (PS2, DC) | [SurrealEngine](https://github.com/dpjudas/SurrealEngine) | UT v436 and Unreal Gold v226 "relatively playable"; many native functions missing; D3D11, Vulkan, GL 3.2 | XL |
| UT2004 (2004) | no | no open engine; OldUnreal's official revival is closed source, x86-64 and aarch64 binaries only | | Not possible |
| Ultima VII (1992) | SNES (cut down) | Exult | Complete, old BE-friendly code (?) | S |
| Ultima IV, VI, VIII | | ScummVM (Ultima engine), Pentagram | U8 finishable "not everything fully working" | S via ScummVM |
| Ultima Underworld 1/2 | no | Underworld Adventures (C++/GL, new commits in 2026 after years dormant), UnderworldGodot (pre-alpha) | | XL |
| The Sims (2000) | different | [Simitone](https://github.com/riperiperi/Simitone) on FreeSO | Upstream Windows-only; Linux fork on .NET 9 + MonoGame | L: no .NET for big-endian ppc64 (Mono route) |
| The Sims 2 (2004) | different | [OpenTS2](https://github.com/LazyDuchess/OpenTS2) | Unity reimplementation, active (Mar 2026) | XL: Unity has no PPC runtime |
| SimCity 4 (2003) | no | nothing found | | Not possible |
| Spore (2008) | different (Spore Hero on Wii, Creatures on DS) | nothing found; Hidden Palace released a February 2008 prototype in 2026, which the modding scene uses, and fans hope EA releases the source as it did for C&C | | Not possible today (a decompilation would start from the prototype's debug information) |
| Age of Empires II (1999) | PS2 (2001) | [openage](https://github.com/SFTtech/openage) (C++20, Qt6, Python; GL 3.3) | Gameplay "basically non-functional" while the simulation is rebuilt; v0.5.0 (Oct 2023) was "not much more than a tech demo"; needs an AoE II TC, HD or DE install. freeaoe is inactive (?). No decompilation found | Watch: XL until openage plays again |

## Action and shooters

| Game (year) | Console | Project | State | Renderer | BE prior art | Data | Effort |
|---|---|---|---|---|---|---|---|
| GTA III (2001), Vice City (2002) | yes | re3 / reVC; the upstream repos are gone (HTTP 451), the most maintained fork covering III, VC and LCS is [hez-gta-re3](https://github.com/Hezkore/hez-gta-re3) | Fully playable | librw: GL 2.1+, GLES 2, D3D9 | **yes**: Wii U and native Wii ports (2026) | own | S-M |
| Jedi Knight: Dark Forces II (1997) | no | [OpenJKDF2](https://github.com/shinyquagsire23/OpenJKDF2) | 95.9% reimplemented excluding the rasterizer | GL 3.3, GL 1.1 (32-bit Win), GLES 3; Dreamcast, DSi | no (but scaled down to DC) | own (GOG) | M |
| Dark Forces (1995), Outlaws (1997) | DF: PS1; Outlaws: no | [TheForceEngine](https://github.com/luciusDXL/TheForceEngine) | v1.0 playable (DF); Outlaws in progress | GL plus software renderer | unknown | own | M |
| Indiana Jones and the Infernal Machine (1999) | N64 | OpenJones3D | early (?) | ? | unknown | own | XL |
| Jedi Outcast / Academy (2002/03) | yes | [OpenJK](https://github.com/JACoders/OpenJK) | playable | GL 2.x | **packaged for ppc/ppc64 on OpenBSD, NetBSD, FreeBSD** | own | M-L (RAM) |
| Half-Life / CS 1.6 | yes | [Xash3D FWGS](https://github.com/FWGS/xash3d-fwgs) | playable | GL 1.x/2.x, GLES, software | ppc64le upstream; a Mac OS 9 PPC fork | own | M |
| Half-Life 2, Portal (2004/07) | yes | [nillerusr/source-engine](https://github.com/nillerusr/source-engine) (from the 2020 leak) | builds and runs on Linux, Android, ARM | ToGL / ToGLES (D3D9 to GL) | no | own | L (RAM, CPU) |
| Doom 3 (2004) | Xbox | dhewm3 | playable | GL ARB2 paths | ? | own | L (RAM) |
| RTCW, Wolfenstein: ET, Quake 3, Elite Force | mostly yes; ET no | iortcw, ET: Legacy, ioquake3 | playable | GL 1.x/2.x | **Q3's QVM had a big-endian PPC JIT** | own; **ET is free** | M |
| Heretic II (1998) | no | [Heretic2R](https://github.com/m-x-d/Heretic2R) (2026) | single player completable | Q2-style GL (?) | unknown | own (v1.06) | M |
| Hexen II (1997) | no | uHexen2 / Hammer of Thyrion | playable | GL 1.x | BE builds (?) | own or demo | S |
| Serious Sam TFE/TSE (2001/02) | Xbox | [Serious Engine 1](https://github.com/tx00100xt/SeriousSamClassic) (official 2016) | playable | GL 1.x/2.x | unknown | own | L (CPU, crowds) |
| Duke 3D, Shadow Warrior, Blood, Redneck Rampage | Duke yes; others no | EDuke32, VoidSW, NBlood, Rednukem | playable | Polymost GL plus software | PPC Mac history (?) | Duke shareware, SW freeware | S |
| Rise of the Triad (1994) | no | rott | playable | SDL software | ? | shareware | S |
| Descent 1/2 | PS1 | DXX-Rebirth | playable | GL 1.x/2.x | PPC Mac history (?) | shareware | S |
| Descent 3 (1999) | no | [Descent3](https://github.com/DescentDevelopers/Descent3) (GPL 2024) | playable; cutscene decoders stripped | GL 1.x era | original shipped on PPC Mac | own (GOG) | M |
| Forsaken (1998) | yes | ForsakenX | playable (released public domain 2007) | GL (?) | unknown | own | M |
| Postal (1997) | no | [POSTAL-SourceCode](https://github.com/RWS-Studios/POSTAL-SourceCode) (official GPL 2016; Debian packages it) | playable (?) | SDL2 software | engine was on Mac 68k/PPC (?) | Postal Classic is free | S-M |
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
| Tomb Raider 1-3 (1996-98) | yes | [OpenLara](https://github.com/XProger/OpenLara) (5.1k stars, the most popular; GL/GLES, many small platforms), [TRX](https://github.com/LostArtefacts/TRX) (1k, the faithful one) | both play TR1-3; TRX's TR4 is early | OpenLara: GL 2/GLES; TRX: GL (3.3 core optional) | unknown | own | M |
| Sonic CD (2011 PC) | yes | [RSDKv3 decompilation](https://github.com/RSDKModding/RSDKv3-Decompilation) | complete | SDL2 software, optional GL | Switch, 3DS, Vita forks | own (Steam) | S-M |
| Sonic 1 and 2 (2013) | yes | [RSDKv4 decompilation](https://github.com/Rubberduckycooly/Sonic-1-2-2013-Decompilation) | complete | SDL2 software | same | own | S-M |
| Captain Claw (1997) | no | [OpenClaw](https://github.com/pjasicek/OpenClaw) | rewrite, SDL2 + Box2D, last push 2022 | SDL2 | unknown | own | M |
| Abe's Oddysee / Exoddus (1997/98) | yes | [R.E.L.I.V.E.](https://github.com/AliveTeam/alive_reversing) | both being reimplemented | SDL2 | unknown | own | M |
| Heart of Darkness (1998) | PS1 | hode | playable (?) | SDL | ? | own | S |
| Cave Story (2004) | later | [NXEngine-evo](https://github.com/nxengine/nxengine-evo) (C++, SDL2, many console ports); doukutsu-rs is more popular but Rust | complete | SDL2 | console ports | freeware | S |
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
| Homeworld (1999) | no | [Gardens of Kadesh](https://gitlab.com/gardens-of-kadesh/gardens-of-kadesh) (the live successor of HomeworldSDL, official 2003 source) | works on Linux, Win, macOS | fixed-function GL | ? | own | M-L |
| Black & White (2001) | no | [openblack](https://github.com/openblack/openblack) | active reimplementation | bgfx (GL, Vulkan) | no | own (patched 1.2) | L |
| Dungeon Keeper (1997) | no | [KeeperFX](https://github.com/dkfans/keeperfx) | active; Linux port community alpha | software / SDL | no | own | L (immature Linux port) |
| Dungeon Keeper 2 (1999) | no | [OpenKeeper](https://github.com/tonihele/OpenKeeper) | incomplete, Java + jMonkeyEngine | GL | no | own | XL |
| Magic Carpet 2 (1995) | no | [remc2](https://github.com/turican0/remc2) | "all of MC2 seems to be playable" | SDL2 software | no | own (GOG) | M-L |
| Syndicate Wars (1996) | PS1 | [syndwarsfx](https://github.com/swfans/syndwarsfx) | single player complete | software | no; x86 32-bit only | own | L (x86 asm) |
| Syndicate (1993) | yes | FreeSynd | playable | SDL | ? | own | S-M |
| Theme Hospital (1997) | PS1 | CorsixTH | playable | SDL2 | ? | own; free demo | S-M |
| Settlers 2 (1996) | DS | Return to the Roots | playable | SDL / GL | ? | own | M |
| Settlers 3 (1998) | no | [JSettlers](https://github.com/paulwedeck/settlers-remake) | alpha, Java | Java2D / LWJGL | no | own or demo | L |
| Caesar III (1998) | no | [Julius](https://github.com/bvschaik/julius) / Augustus | complete | SDL2 software | Nix lists powerpc64 (untested) | own | M |
| Pharaoh + Cleopatra (1999) | no | [Akhenaten](https://github.com/dalerank/Akhenaten) | in progress: early campaign plays | SDL2 | no | own (patch 1.3) | M |
| Heroes of Might and Magic II (1996) | no | [fheroes2](https://github.com/ihhub/fheroes2) | complete | SDL2 | no | own; free demo | M |
| Heroes of Might and Magic III (1999) | no | VCMI | playable | SDL2 | ? | own | M-L (RAM, Boost) |
| Master of Orion (1993) | no | [1oom](https://github.com/1oom-fork/1oom) | complete, in Debian | SDL software | ppc64el build only | own | S |
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
| Betrayal at Krondor (1993) | no | [BaKGL](https://github.com/xavieran/BaKGL) (xBaK's successor) | active (2026) | GL | | own | XL |
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

Zoo Tycoon has an early reimplementation, [OpenZT](https://github.com/openztcc/OpenZT). Nothing found: SimCity 3000/4, The Incredible Machine, Lemmings 2/3, SkiFree source,
Moorhuhn, LEGO Loco, Chessmaster.

## Alternatives and popularity

Every open project found for each game above, ranked by popularity, with the pick for our port.
Fetched 2026-10-20.

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

### The user's favourites (and Ultima, Sims, AoE II)

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

### Action and shooters

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

### Racing

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

### Flight and space

| Game | Project (URL) | Stars | Last push | State | Pick |
|---|---|---|---|---|---|
| TIE Fighter | OpenTIE https://github.com/elyosh/OpenTIE | 338 | 2026-10-04 | Active; SDL3 GPU (no GL) | **PICK** (needs a GL 2.1 backend) |
| X-Wing | OpenXW https://github.com/elyosh/OpenXW | 117 | 2026-10-06 | same | same |
| X-Wing Alliance | OpenXWA https://github.com/elyosh/OpenXWA | 157 | 2026-10-12 | same | same |
| Freelancer | Librelancer https://github.com/Librelancer/Librelancer | 612 | 2026-10-10 (archived) | Moved to https://codeberg.org/Librelancer/Librelancer (3 stars, active 2026-10-19); .NET | **PICK** (only one) |
| FreeSpace 1/2 | FreeSpace Open https://github.com/scp-fs2open/fs2open.github.com | 495 | 2026-10-18 | Only engine | **PICK** |
| Privateer | Vega Strike engine https://github.com/vegastrike/Vega-Strike-Engine-Source | 364 | 2026-10-20 | Gemini Gold runs on it | **PICK** |
| Nothing found | Wing Commander 3/4, Tachyon, Independence War, Starlancer, MechWarrior | | | | |

### Platformers and 2D action

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

### Strategy

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

### RPG

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

### Adventure, sim, casual (and Ultima, Sims, SimCity 4, Spore again)

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

## Next steps

Evaluation and porting are with the main session from 2026-10-20 (the user).

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

For the alternatives and their popularity:
- GitHub repository pages, REST API and `commits.atom` feeds for every URL above; GitHub search for discovery.
- Codeberg API (Librelancer, xU4, OpenCivOne), GitLab API (Gardens of Kadesh, OpenMW), voidpoint.io API (EDuke32).
- [awesome-decomp-games](https://github.com/SubmitGame/awesome-decomp-games) (discovery).
- Heretic2R: [GamingOnLinux](https://www.gamingonlinux.com/2026/03/heretic-ii-has-a-new-reverse-engineered-source-port); Postal: [Debian postal1](https://ftp-master.debian.org/new/postal1_2015.git20250526+ds-1.html); re3's takedown history: [TheGamer](https://www.thegamer.com/reverse-engineered-gta-code-restored-dmca/); FreeSynd: [freesynd.sourceforge.io](https://freesynd.sourceforge.io/); FreeDink: [gnu.org](https://www.gnu.org/software/freedink/).
