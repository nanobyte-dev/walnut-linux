# Darling on PowerPC for Mac OS X games (research, 2026-10-19)

Verdict: not worth it for games. Darling has no working Carbon, AGL, QuickTime or DrawSprocket even on x86, so a PowerPC port would inherit that gap; a playable Carbon/AGL game is 1.5-3 person-years. What it would uniquely unlock is a few dozen PowerPC-era titles; classic Mac OS through SheepShaver (docs/research/CLASSIC-MACOS.md) reaches thousands. Nothing built.


Research only. Nothing was installed on the PS3 and nothing was run over SSH. No commits were made and docs/ was not touched.
Sources inspected: shallow clone of github.com/darlinghq/darling at 60ba801 (2026-09-06, "fix-hitoolbox-build"),
plus submodules xnu, darlingserver, dyld, objc4, corefoundation, foundation, cocotron, libc, OpenAL and glut.
The clone was on the overflow disk (/home had 9.4% free) and was deleted after this was written.
Claims marked **(guess)** are my estimates. Counts marked **(judgement)** are my own classification of a
published list.

## Verdict

**Not justified as a game project. Effort: XL (1.5 to 3 person-years) for "Carbon/AGL games run", L for a
command-line proof of concept.** The argument "x86 to ppc is less effort than starting from scratch" holds
for about a third of the stack: the loader and the Mach/BSD emulation are mostly portable C. It fails for
the part games need. Darling has no Carbon, AGL, QuickTime, DrawSprocket or InputSprocket on x86 either.
Those are stubs that print "STUB". A ppc port would inherit that gap, not fill it.

What the layer would uniquely unlock is small:
- **Mac-only titles (no Windows version at all) that need OS X and have no open port: a few dozen at most,
  mostly 2D shareware (guess).** Ambrosia is a worked example: of 42 titles, only about 4 are OS X-only,
  Mac-only and have no port (Aki Mahjong, GooBall, Redline, SketchFighter 4000 Alpha). SketchFighter was
  re-released for modern Macs in 2020.
- **Many 2001-2004 Mac-only games are Carbon CFM (PEF) binaries that also run on Mac OS 9.** The SheepShaver
  native route (docs/research/CLASSIC-MACOS.md, effort M) covers those already, subject to SheepShaver's
  9.0.4 ceiling (see the unknowns).
- **The larger pool is Mac ports of Windows games that never got a Linux version (about 40 notable titles).**
  On a PC they run under Wine, so they are not unique in general. They are unique on the PS3, because the PPE
  cannot run x86 Windows code at playable speed. But they are commercial Carbon/AGL/QuickTime ports, mostly
  needing 256 MB or more of RAM and a G4 at 800 MHz or faster. They are the hardest case for the library
  gap and the tightest fit for 256 MB.
- **The classic Mac OS 7-9 library is two orders of magnitude bigger.** Macintosh Repository lists 7,779 game
  entries, most of them classic-era, and SheepShaver plus Basilisk II already run that code.

Recommendation: do SheepShaver native first (M, existing code). Treat Darling-ppc as a research toy. If the
user wants a taste, the cheapest experiment is the command-line proof of concept in step 1 below, using
Apple's own ppc dyld and libSystem rather than a Darling build. It needs no Darwin/ppc toolchain.

## 5. Games: what the layer would uniquely unlock

### 5a. OS X-era PowerPC games (2001-2006) by category

Base list: Greg Gant's curated "A-tier late PowerPC gaming list" (105 entries covering OS 9 and OS X,
https://blog.greggant.com/posts/2024/02/19/the-a-tier-late-powerpc-gaming-list.html). Exclusions and
categories are my **(judgement)**. Check each title before relying on it.

**Excluded: a native Linux version or an open engine/port exists (about 25 of the list).** These need nothing new:
Marathon trilogy (Aleph One), Glider PRO (source release, Aerofoil), Maelstrom (GPL), Myth I/II (Loki Linux
port of Myth II, Project Magma), all Pangea games (Iliyas Jorio's ports: Nanosaur 1/2, Bugdom 1/2, Otto Matic,
Cro-Mag Rally, Mighty Mike, Billy Frontier; github.com/jorio), Doom/Doom II/Final Doom and Doom 3, Quake I-III,
RtCW (iortcw), UT/UT2003/UT2004/Unreal, Duke 3D, Shadow Warrior, Descent 1-3, Diablo (DevilutionX),
Warcraft II (Stratagus/Wargus), Fallout 1/2 (fallout-ce, fallout2-ce), Medal of Honor AA (OpenMoHAA),
Dark Forces (The Force Engine), Baldur's Gate II (GemRB), Escape from Monkey Island (ScummVM Grim),
Wolfenstein 3D, World of Goo (Linux), Lugaru (GPL), Avara (open source), Ares (Antares).

**OS X-capable, no Linux version, no open port: Windows-origin commercial ports (about 40).** These are unique
on the PS3 only because x86 Windows is unreachable there:
4x4 Evolution 2, Age of Empires II and III, American McGee's Alice, Black & White, Call of Duty 1/2,
Civilization III, C&C Generals (EA released the source in 2025; community ports are in progress, so this
one will probably leave the list), Diablo II, Dungeon Siege, Halo CE (Westlake), Homeworld 2,
Indiana Jones and the Emperor's Tomb, Kelly Slater's Pro Surfer, Max Payne, Myth III, NOLF 1/2,
Oni (the OniX fan rebuild is in progress, https://www.moddb.com/mods/onix), Red Faction, RollerCoaster Tycoon 3,
SimCity 4, The Sims and The Sims 2, StarCraft, Ghost Recon, Rainbow Six 3, Splinter Cell (also on PS3 GameOS
as the HD trilogy, so it is excluded by the user's rule), Tomb Raider Chronicles and Angel of Darkness,
THPS 2/3/4, Tropico 1/2, Tiger Woods 2003/2005, Warcraft III, Virtual Pool, X-Plane 7/8.
- **Fit for 256 MB and the PPE (guess): about 12 to 18 of these.** Their box minimum is 128 MB or less, with a G3/G4 at
  400-600 MHz. Likely fits: Civ III, Tropico, The Sims 1, AoE II, Diablo II, StarCraft, Myth III (128 MB,
  G3/400, 8 MB VRAM, QuickTime 5; ATPM review https://www.atpm.com/9.07/myth.shtml), Oni, THPS 2/3, Virtual Pool,
  Alice. Halo needs 256 MB minimum and an 800 MHz G4
  (https://halopedia.org/Halo:_Combat_Evolved_for_Macintosh), so no. The Sims 2, SimCity 4, CoD 2 and
  AoE III are too heavy.
- **APIs: Carbon (often CFM so that one binary also ran on OS 9), AGL, QuickTime for movies, InputSprocket or
  HID Manager, Sound Manager or OpenAL (judgement/guess).** Inspect a binary (otool -L, the 'cfrg' resource)
  before investing.
- Several of the OS9-to-OSX titles (Civ III, Tropico, The Sims, AoE II, Diablo II, StarCraft, Oni, Max Payne,
  Warcraft III, Alice, THPS 2, Ghost Recon) are dual-boot CFM Carbon or have OS 9 builds. **On an OS 9-capable
  emulator they do not need Darling at all,** if the build runs on 9.0.4.

**Mac-only (never on Windows) and OS X-only, with no port: small (judgement).**
- Gant's list has essentially one: WingNuts 2 (Freeverse). Verify whether it had a Windows release.
- Ambrosia catalog (https://en.wikipedia.org/wiki/Ambrosia_Software, 42 titles):
  - OS X-only and Mac-only: Aki Mahjong Solitaire (2003), GooBall (2005), Redline (2006), SketchFighter 4000 Alpha (2006).
  - Classic plus OS X, Mac-only, so covered by SheepShaver if they run on 9.0.4: Bubble Trouble X, Apeiron X, El Ballo, Coldstone titles.
  - Uplink, Darwinia, DEFCON, Aquaria and Multiwinia have Linux versions.
  - EV Nova, Deimos Rising, Pillars of Garendall and pop-pop had Windows versions.
- Other shareware houses of the era (Freeverse, Strange Flavour, Spiderweb's classic Avernum/Geneforge,
  Delta Tao, Casady & Greene) would add tens more, mostly 2D puzzle/arcade/RPG titles **(guess)**.
  Spiderweb's games had Windows versions and some have Linux remasters.
- **Rough total: 20-50 titles worldwide that only a ppc Mac OS X layer can run, with no Windows version, no
  port and no OS 9 build (guess).** Several are Universal Binaries (EV Nova 1.1, SketchFighter), so x86
  Darling on a PC would already reach them if it ran Carbon, which it does not.

### 5b. Classic Mac OS 7-9 (the SheepShaver/Basilisk route), for comparison

- **Size.** Macintosh Repository lists 7,779 game entries (https://www.macintoshrepository.org/games/,
  "Found 7779 software entries"). Most are classic-era. Macintosh Garden is of similar order (its certificate
  failed, so I have no count).
- **Unique draws already noted in CLASSIC-MACOS.md.** Myth I/II originals (Magma covers them), Oni, EV/EVO/Nova,
  Ferazel's Wand (Mac-only, never on OS X), Mars Rising, Cythera, Harry the Handsome Executive, Apeiron,
  Bubble Trouble, Spectre, Dark Castle, and many Aspyr/MacSoft ports of Windows games for OS 8-9: Deus Ex Mac,
  Future Cop LAPD, Episode I Racer, Rogue Spear, SimCity 2000/3000, MechWarrior 2 (RAVE), Tomb Raider I-III.
  Tomb Raider I-III are also PSone Classics on GameOS.
- **Problem APIs in the classic library.** RAVE and QuickDraw 3D, used by early 3D Mac games, have no open
  implementation. Under SheepShaver they would run as Apple's own software renderers, which is slow.
- **Ratio.** The classic route offers thousands of titles through existing code (effort M). The OS X-only ppc
  route offers dozens of unique titles plus about 40 Windows ports, through a layer that does not exist (XL).

## 1. Darling today (summary)

| Piece | Where (in the darling tree) | State | Arch-specific |
|---|---|---|---|
| mldr (Mach-O loader, ELF) | src/startup/mldr, 2,850 lines | Loads fat/thin Mach-O and dyld from user space. Fat headers are byte-swapped correctly (mldr.c:353), and thin Mach-O is read in host order, which is fine for BE ppc on a BE host | ~15 `#ifdef __x86_64__/__i386__` blocks. Stack setup and the entry trampoline are inline asm (mldr.c:820-935). arm64 has a stub (`#error TODO: aarch64`, mldr.c:259) |
| commpage | mldr/commpage.c, 161 lines | Data only (i386 cpu_capabilities, cpuid) | x86 |
| ELF bridge (host libs in a Mach-O process) | src/libelfloader/wrapgen (855 lines), mldr/elfcalls (538 lines, thread trampolines in x86 asm), src/native/CMakeLists.txt `wrap_elf()` for GL, GLU, EGL, X11, cairo, freetype... | Generated pass-through wrappers. These work only because Darwin x86-64 and Linux x86-64 share the SysV AMD64 calling convention | Trivial on x86; **the big problem on ppc (below)** |
| BSD/Mach syscall emulation (libsystem_kernel) | external/xnu/darling/src/libsystem_kernel/emulation, 25,673 lines | Converts Darwin syscalls to Linux and talks RPC to darlingserver | 25 files with x86 ifdefs, 6 .S files (406 lines): syscall entry, sig_restorer, xtrace hooks, TLS, bsdthread |
| darlingserver (Mach IPC/tasks in user space) | external/darlingserver: ~7.2k lines of its own code plus a 33 MB "duct-tape" xnu osfmk | Mach ports and messages are real xnu code running in user space | 203 files mention x86 (mostly xnu headers); process.cpp and thread.cpp carry x86 thread state |
| dyld | external/dyld (dyld3-era, modern) | Modern dyld | ppc removed upstream. The classic loader still had `__ppc__` relocation code in dyld-195.5 (10.7) and none in dyld-210.2.3 (10.8) |
| objc4 | external/objc4 (modern; messengers for i386/x86_64/arm/arm64 only) | Old (fragile) ABI kept for i386 | ppc messenger existed: objc4-437 runtime/Messengers.subproj/objc-msg-ppc.s, 1,493 lines |
| CoreFoundation | external/corefoundation, ~170k lines (Apple CF-Lite based) | Works | Endian-clean (CF ran on ppc) |
| Foundation | external/foundation, ~110k lines | Its README says "Apportable Cocotron, and some others" replaced the old GNUstep base | Unknown BE status |
| AppKit, CoreGraphics | external/cocotron (Darling fork): AppKit 133k, Onyx2D 46k, CoreGraphics 10.9k, QuartzCore 3.8k lines | Darling docs: only simple Hello-World Cocoa GUI apps work | Cocotron is portable ObjC (Windows/Linux); BE untested (guess) |
| Carbon/HIToolbox | src/frameworks/Carbon: 5.2k lines, HIToolbox 1.65k lines (MacWindows.cpp 203 lines) | Mostly stubs; Carbon.c has 22 of 23 functions as stubs | n/a |
| QuickDraw | ApplicationServices/QD, 192 lines | Stub | n/a |
| AGL | frameworks/AGL/src/AGL.c, 496 lines | 79 of 80 functions are STUB returning fake handles | n/a |
| CGL / OpenGL | frameworks/OpenGL/OpenGL.c, 334 lines | CGL on EGL, re-exporting host libGL through wrap_elf | ABI bridge |
| QuickTime | frameworks/QuickTime/src/QuickTime.c, 27,630 lines | 4,601 of 4,601 functions are stubs | n/a |
| DrawSprocket, ForceFeedback | 235 and 195 lines | All stubs (35 and 21) | n/a |
| OpenAL.framework | frameworks/OpenAL, 853 lines | 115 stubs (the external/OpenAL submodule may be the real one; not checked) | n/a |
| CoreAudio | src/CoreAudio, 74k lines | Apple's CoreAudio utility code; the backend not examined | n/a |
| Project status | latest release v0.1.20260608 | x86-64 only per docs.darlinghq.org; TARGET_i386 is ON in CMakeLists.txt:134, so i386 slices are still built | The arm64 port (issue #642 from 2020, PR #1753 from 2026) is unmerged after about 6 years |

**Apple code that once supported ppc: all available, verified on github.com/apple-oss-distributions.**
- objc4-437 has objc-msg-ppc.s.
- xnu-1228 (10.5) has osfmk/ppc. xnu-792 (10.4) has osfmk/ppc/commpage (pthread.s, spinlocks.s, bzero_32/128.s,
  bcopy_g3/g4/970.s, gettimeofday.s, mach_absolute_time.s).
- Libc-498 (10.5) has ppc/ and ppc64/.
- dyld-97.1 (10.5) has stub_binding_helper.s and ImageLoaderMachO.cpp. The ppc paths last until dyld-195.5.

**Endianness.** Darling's own loader is endian-aware where it has to be: fat headers are swapped and thin
Mach-O is read in host order. Its wire formats (darlingserver RPC, xtrace) were never tested on BE
(unknown). Converted Linux structs are a separate layer: Linux ppc32 `struct stat`, `sigaction`, `ucontext`
and termios differ from x86, so the conversion headers need ppc variants. The arm64 Linux syscall table
(linux-syscalls/linux-arm64.h) shows the pattern.

## 2. What a ppc32 port needs (summary, with effort)

**Toolchain (L, a critical unknown).** LLVM removed Darwin/PowerPC in 2018 (RFC with approvals:
https://lists.llvm.org/pipermail/llvm-dev/2018-August/125526.html). Darling's sources need clang (ObjC 2,
blocks, ARC, C++17 in dyld/objc4). GCC still has powerpc-darwin (Iain Sandoe's work), but GCC cannot build
modern Darling ObjC code. So building Darling *for* ppc means resurrecting the Darwin ABI in a 2026 LLVM
PowerPC backend and pairing it with an ld64/cctools that still emits ppc. The alternative is to avoid building
Darwin-side code at all (see the PoC route).

**Loader, mldr (S).**
- Add ppc thread state (LC_UNIXTHREAD srr0/r1) and setup_stack for ppc.
- The ppc commpage contains code, not just data: libSystem jumps to fixed addresses at 0xFFFF8000 and up
  (7 pages, ending at 0xFFFFF000). On Linux ppc64 the compat 32-bit TASK_SIZE and STACK_TOP are exactly
  0xFFFFF000 (arch/powerpc/include/asm/task_size_64.h:45,71), so the initial Linux stack sits where the
  commpage must go. mldr has to switch stacks, unmap that range and map the commpage. That is feasible.
- NetBSD wrote the same thing: sys/arch/powerpc/powerpc/darwin_commpage_machdep.S
  (https://2004.eurobsdcon.org/uploads/media/EBSD04_21.pdf).

**Darwin ppc ABI vs Linux ppc32 SysV: the structural blocker for the ELF bridge (M-L).** Apple's
32-bit PowerPC Function Calling Conventions
(https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/LowLevelABI/100-32-bit_PowerPC_Function_Calling_Conventions/32bitPowerPC.html)
differ from Linux SysV in these ways:
- **GPR2 is "available for general use" (volatile) on Darwin.** On Linux ppc32, r2 is the thread pointer. Darwin
  code clobbers glibc's TLS, so every Darwin-to-ELF call and every signal delivered while Darwin code runs
  must restore r2. This is SheepShaver's r2 problem again.
- **Floating-point arguments also consume GPR slots on Darwin** (a float skips 1 GPR, a double skips 2). SysV
  assigns them independently. So any call with an int or pointer after a float has different register
  assignment. Example: `glMap1f(target, u1, u2, stride, order, points)` puts stride in r6 on Darwin and r4 on Linux.
- **Structs are always returned through a hidden pointer in r3 on Darwin.** SysV returns structs of 8 bytes or less in r3:r4.
- **bool and _Bool are 4 bytes on Darwin.**
- **Doubles inside structs use "power" alignment (4) after the first member on Darwin.** SysV uses 8.
- **Linkage area: Darwin's is 24 bytes with LR at 8(SP) plus a mandatory parameter area; SysV saves LR at 4(SP)
  in the caller's frame.**
- **Darwin has a 224-byte red zone.** Linux 32-bit signal delivery does not skip one (guess). Fix with
  sigaltstack on every thread.
- **Varargs are passed like normal arguments on Darwin.** SysV uses a va_list register save area plus a CR6 bit.

Consequence: wrapgen's pass-through wrappers are wrong on ppc. Every bridged host function (GL, X11, cairo,
freetype, ALSA) needs a generated argument-marshalling thunk. GL's thunks can be generated from Khronos gl.xml.
X11 varargs functions (XCreateIC, XVaCreateNestedList) need hand-written ones. On x86-64 none of this exists.

**Syscalls and Mach traps (M).**
- Darwin ppc uses `sc` with r0 = number: negative means Mach trap, 0x7FF2 is the "ultra-fast trap"
  pthread_self. On success it returns to PC+8, on error to PC+4. Linux uses `sc` with CR0.SO for errors.
- A Darling-built libSystem calls Linux directly, so no trapping is needed.
- Apple's own 10.4 libSystem.B.dylib is monolithic (libc and syscall stubs together), so its `sc` would hit the
  Linux syscall table with Darwin numbers. That needs seccomp-bpf SECCOMP_RET_TRAP filtered on instruction
  pointer, then SIGSYS emulation. Syscall User Dispatch is not available because powerpc does not select
  GENERIC_ENTRY in 6.12.
- **pthread TLS.** Darwin ppc reads its per-thread cookie through commpage routines: `mfspr r3,259` (USPRG3) or
  trap 0x7FF2 (xnu-792 osfmk/ppc/commpage/pthread.s). Linux ppc64 uses SPRG3 for vdso getcpu, so our commpage
  needs its own per-thread lookup, for example stack-masking or a fast trap.

**Signals (M).** Linux ppc32 ucontext must be converted to Darwin ppc_thread_state/ppc_float_state/
ppc_vector_state for sigexc and Mach exception messages, restoring r2 on entry.

**dyld (M).** Port the ppc relocation types (PPC_RELOC_BR24, HI16/LO16/HA16, SECTDIFF, PB_LA_PTR, JBSR...) and
the lazy-binding stubs (__picsymbol_stub, __symbol_stub, stub_binding_helper) from dyld-97 or dyld-195.5
ImageLoaderMachOClassic. 10.2-10.5 binaries predate LC_DYLD_INFO. Also add the ppc-only symbol variants:
`$LDBLStub` (64-to-128-bit long double transition) and `$UNIX2003`.

**objc4 (M).** Port objc-msg-ppc.s from objc4-437 into the old-ABI (objc-runtime-old) path that Darling keeps for i386.

**C++.** Apple GCC 4.0 on ppc uses the Itanium C++ ABI, but with Darwin's ABI quirks (4-byte bool, power alignment).
C++ across the Darwin/ELF boundary must be avoided, which in practice it is.

**Classic PEF/CFM apps (L on top).** Mac OS X ran them through closed CarbonCore/LaunchCFMApp. No open loader
exists outside SheepShaver/emulators. The PEF format is documented by Apple. CFM uses r2 as TOC with
cross-fragment glue, the opposite convention from Mach-O. Better left to SheepShaver.

## 3. The library gap (summary)

- **Carbon HIToolbox, QuickDraw, Resource Manager, Sound Manager, InputSprocket/DrawSprocket, AGL, QuickTime:**
  Darling has stubs only (table above). No open Carbon HIToolbox reimplementation exists. Partial
  Toolbox/QuickDraw sources to borrow at source level:
  - Executor (github.com/autc04/executor, System 7-era Toolbox with a 68k emulator and PowerCore PPC).
  - Advanced Mac Substitute (v68k.org, 68k, 1-bit QuickDraw).
  - Iliyas Jorio's Pomme (github.com/jorio/Pomme, a partial Toolbox C API used for the Pangea ports, with
    Resource Manager and Sound Manager subsets). Pomme is the most practical.
  - QuickTime playback could be FFmpeg (MOV, Cinepak, SVQ1/3, IMA4, MACE) behind a minimal Movie Toolbox
    (guess on coverage).
- **Cocoa AppKit at the 10.4 level:** Cocotron via Darling, at Hello-World level on x86. GNUstep is source-level
  only; libgnustep-base builds on Debian ppc64, but GNUstep cannot link Apple ObjC-1-ABI Mach-O binaries.
- **Using Apple's own frameworks instead** (from the user's 10.4/10.5 install: CoreGraphics, HIToolbox,
  QuickTime, CoreAudio): these talk Mach IPC to WindowServer (private CGS protocol), coreaudiod/the HAL and
  IOKit (HID, IOGraphics). Reimplementing those servers is XL. NetBSD COMPAT_DARWIN (2003-2004, over 20k lines,
  macppc) ran 10.2/10.3 CLI tools and XDarwin with Apple's userland but never Aqua/WindowServer
  (EuroBSDCon 2004 paper above). Darling itself never attempted Apple's WindowServer.
- **Prior art for ppc Mach-O on a non-Darwin OS:** only NetBSD COMPAT_DARWIN. Maloader (github.com/shinh/maloader)
  is x86 only. No ppc Darling fork and no ppc issues exist in Darling's tracker. QEMU mac99 runs 10.4/10.5
  (reference only, and far too slow on a PPE).

## 6. Effort, unknowns, proof of concept

| Stage | Effort (person-weeks, guess) |
|---|---|
| 0. Toolchain to build Darling for ppc (LLVM Darwin-ppc resurrection plus ld64 ppc) | 8-16 (L); avoidable for the PoC |
| 1. CLI PoC with Apple's ppc dyld + libSystem (10.4), our loader, commpage, seccomp syscall emulation, minimal Mach traps reusing Darling's emulation and darlingserver code | 4-8 (M) |
| 2. ELF bridge with ABI thunks (GL generated from gl.xml, X11, ALSA), signals, threads | 6-12 (L) |
| 3. SDL- or GLUT-based Mac PPC game (replace the app's bundled SDL.framework with a shim to host SDL2; GLUT shim) | 4-8 (M) after stage 2 |
| 4. Carbon game: Event/Window Manager, QuickDraw subset, Resource/File Manager, Sound Manager, AGL on GLX, InputSprocket on SDL, QuickTime on FFmpeg | 40-100+ (XL), and per-game long-tail work |
| Total to "a Carbon/AGL game plays" | about 70-150 person-weeks |

Critical unknowns:
1. Whether Darwin code's r2 clobbering can be fenced cheaply at every Darwin-to-ELF edge and every signal.
2. The toolchain: whether anyone maintains an LLVM with Darwin-ppc. If not, Darling's own libraries cannot be
   built for ppc, and only the "Apple binaries" route remains.
3. How much of Apple's 10.4 libSystem/CF init path needs Mach IPC beyond the basics (bootstrap, notify,
   lookupd/DirectoryService).
4. Cell specifics: the 128-byte dcbz breaks Apple's bzero_32 commpage variant, so supply the 128-byte one
   (bzero_128.s exists for G5). Microcoded instructions are slow.
5. 256 MB RAM: the Mac framework footprint plus a game sized for a 256 MB G4.
6. Which OS9-to-OSX titles run on SheepShaver's 9.0.4 ceiling. Some Carbon titles need CarbonLib versions or
   OS 9.1+ (unknown).

Smallest PoC, if anyone wants it:
1. A ppc Mach-O CLI binary (`/bin/echo` or `/usr/bin/true` from the user's 10.4 install) loaded by a small ppc32 ELF loader:
   - Lift mldr's load32 and fat code.
   - Map the user's /usr/lib/dyld and /usr/lib/libSystem.B.dylib, plus a commpage built from xnu-792 sources.
   - Emulate BSD syscalls (SIGSYS) with Darling's conversion code and stub the Mach traps.
   - Success means "hello" on the PS3 console.
2. A GLUT or SDL demo.
3. Only then a Carbon 2D shareware game.

Files the user must supply: from their own 10.4 or 10.5 PowerPC install, /usr/lib/dyld, /usr/lib/libSystem.B.dylib
(and the /usr/lib/system/ pieces), and for GUI stages /System/Library/Frameworks/{CoreFoundation,
CoreServices,ApplicationServices,Carbon,AGL,OpenGL,QuickTime}.framework. These are Apple's binaries: never commit,
publish or put them in images. The game files are theirs too.

## Sources

- Darling clone 60ba801 (paths and line counts above, measured with find/wc); https://docs.darlinghq.org/print.html;
  https://github.com/darlinghq/darling/pull/1753 ; issue #642.
- Apple OSS (verified via api.github.com and raw.githubusercontent.com): objc4-437 Messengers.subproj, xnu-1228
  osfmk/ppc, xnu-792 osfmk/ppc/commpage/pthread.s, Libc-498 ppc/ppc64, dyld-97.1 src, dyld-132.13/195.5/210.2.3
  ImageLoaderMachOClassic.cpp (`__ppc__` count 6/6/0).
- Apple 32-bit PowerPC calling conventions (URL above). Linux v6.12 arch/powerpc/include/asm/task_size_64.h and arch/powerpc/Kconfig.
- LLVM Darwin-ppc removal RFC (URL above). NetBSD COMPAT_DARWIN paper (URL above).
- Games: Greg Gant's list (URL above); https://en.wikipedia.org/wiki/Ambrosia_Software ; https://github.com/jorio ;
  https://www.atpm.com/9.07/myth.shtml ; https://halopedia.org/Halo:_Combat_Evolved_for_Macintosh ;
  https://www.macintoshrepository.org/games/ ; https://www.moddb.com/mods/onix ; https://en.wikipedia.org/wiki/SketchFighter_4000_Alpha
- Local: docs/research/CLASSIC-MACOS.md (the SheepShaver route).
