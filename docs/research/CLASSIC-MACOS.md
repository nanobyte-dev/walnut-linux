# Classic Mac OS on the PS3 (research, 2026-10-19)

SheepShaver in native PowerPC mode looks feasible with a 32-bit powerpc build and a Cell patch; the open risk is dcbz clearing 128 bytes on the Cell. Not built; needs the user's Mac ROM and Mac OS CD.


Date 2026-10-19. Research only: nothing was installed on the PS3, no SSH, no commits, docs/ untouched.
Source tree inspected: `build/tmp/macos/src/macemu` (shallow clone of github.com/kanjitalk755/macemu,
HEAD 892eeb7, 2026-08-30 merge of PR #320, so the fork is alive). Delete that tree once this is read.
Claims marked (guess) are my estimates, not measurements.

## Verdict

**Build needed, effort M, and worth a first test.** SheepShaver's native PowerPC mode is still in the
source and is the only way Mac OS 9 would run at useful speed on the PPE (Mac code executes directly on
the host CPU; the ROM's own 68k emulator then runs 68k apps at near-native-PPC-emulator speed). Nobody
seems to have done this on a PS3 or Cell, so it is novel. But it needs a 32-bit `powerpc` userland, a
tiny patch for the Cell PVR, and there is one genuine show-stopper candidate: `dcbz` on the Cell is 128
bytes, classic Mac code assumes 32. The fallback (SheepShaver's interpreter, QEMU TCG) works but is slow.
Basilisk II (68k, System 7 to 8.1) is cheap and should run fine, but is not in Debian for ppc64.

Critical unknowns, in order:
1. `dcbz` line size (128 B on Cell vs 32 B assumed by Mac OS 9, its libraries and games).
2. Whether the native-mode machinery (r2 TLS handling, sheepthreads, signal register layout) works with
   a `powerpc` 32-bit glibc 2.43 userland on our 64-bit kernel (it is old code; Debian dropped sheepshaver).
3. Cell PVR 0x0070 is unknown to the ROM patcher (see below): a small patch, but the right values for
   cache/TLB info need care.
4. Unhandled instructions on the PPE (lswx/stswx string ops are microcoded/trapped on 970-class cores;
   Linux emulates misaligned/string cases, slowly). Probably fine, unmeasured.

## 1. SheepShaver native PPC mode

**Does it exist?** Yes. `SheepShaver/src/Unix/configure.ac` lines 88-110: if the compiler defines
`__powerpc__` or `__ppc__`, `EMULATED_PPC=no` ("We use native CPU if possible"); otherwise the kpx_cpu
interpreter/JIT is used. `--enable-ppc-emulator=yes` forces the emulator even on a PPC host. In native
mode configure forces "Real Addressing" (line 1236) and adds `paranoia.cpp Linux/sheepthreads.c ppc_asm.S`
(line 664). `SheepShaver/src/Unix/main_unix.cpp` has ~25 `#if !EMULATED_PPC` blocks (native path).
A 2008 Fedora changelog (via search) says even the ppc package used the emulator because native needed the
private pthreads in `Linux/sheepthreads.c`; that code is still present.

**32-bit only.** `Unix/sigregs.h` (the `#ifndef EMULATED_PPC` part) has `uint32` registers and casts
`uc_mcontext.regs` to a `pt_regs`-derived struct; the memory model is 32-bit Mac addresses mapped 1:1
into the host space ("real addressing"). `main_unix.cpp` comments: TLS pointer is r2 in 32-bit mode,
r13 in 64-bit, and the code `set_r2(TOC)`s after every signal. Talospace (SheepSforza, ppc64le) confirms
SheepShaver is "still pervasively 32-bit, even in a 64-bit configuration": mmap returns 64-bit addresses
and breaks it; their fix was a fixed hint address (0x18000000).
So: build as 32-bit `powerpc` (ELF, ppc32) and run under the kernel's compat layer.

**Kernel.** `build/tmp/netboot/boot/config-6.12.111+ps3.152-ps3-release` has `CONFIG_COMPAT=y`,
`CONFIG_COMPAT_BINFMT_ELF=y`, `CONFIG_ALTIVEC=y`, `CONFIG_PPC_4K_PAGES=y`, `CONFIG_DEFAULT_MMAP_MIN_ADDR=4096`.
(packages/patched/kernel/linux/config/ps3.config is a fragment on top of Debian's config; it does not mention COMPAT, the built
config does.) No KVM options are set.

**Userland.** debian-ports still builds both: `https://deb.debian.org/debian-ports/dists/unstable/Release`
(fetched 2026-10-19) lists `powerpc` and `ppc64`; both Packages files were regenerated today; libc6
2.43-7, libsdl2-2.0-0 2.32.74+ds-1 and libgtk-3-0t64 3.24.52-3 are the same version on both and
`Multi-Arch: same`. So `dpkg --add-architecture powerpc` is possible, but our own ps3 apt repo
(aptly) carries rebuilt ppc64 libs; a `Multi-Arch: same` libc6 must match versions, so the cleaner test is
a **separate 32-bit debootstrap chroot** (`debootstrap --arch=powerpc sid ... https://deb.debian.org/debian-ports`),
bind-mounting `/tmp/.X11-unix`, `/dev/snd`, using the PS3's X server. The chroot can be built on the
x86 build host (qemu-user) like our other chroots; testing needs the PS3. Needs libsdl2:powerpc, libgtk-3
(or GTK2/3 prefs editor, `prefs_editor_gtk3.cpp` exists), libasound.

**Low memory.** `main_unix.cpp` ~1050-1100: with a linker script (`ldscripts/linux-ppc.ld`, so the
binary sits out of the way) it tries `vm_mac_acquire_fixed(0, RAMSize)` i.e. Mac RAM from address 0;
otherwise it maps the Low Memory area 0x0000-0x3000 at 0 and RAM elsewhere. Either needs
`sysctl vm.mmap_min_addr=0` (our default is 4096; SheepSforza and the old docs say the same). No SELinux on
Debian. A newer-kernel regression exists where mprotect on a page at 0 fails (bugs.archlinux.org/task/79656);
6.12 on ppc untested. RAM is plain anonymous mmap: only touched pages cost host memory. Low-memory
globals accessed by the nanokernel trigger SIGSEGV handled by SheepShaver (comment block at the top of
main_unix.cpp), so it works with the guest-visible 4 KiB page size.

**Privileged instructions / BATs / segment registers.** SheepShaver does not run the Mac nanokernel; it
patches the ROM so that nanokernel work is done by its own code (`rom_patches.cpp`: "Skip SR/BAT init"
line 753, "Don't load SRs and BATs" line 993, "Don't read PVR" 771, "Don't read MSR"). Remaining privileged
instructions (mfmsr, mtsr, mtsrin, tlbie, mfspr of DEC/MMCR/PMC/SDR1/PVR) raise SIGILL, and
`sigill_handler` (`main_unix.cpp` ~2138-2270) emulates them (mfmsr returns 0xf072, PVR returns the
theoretical PVR). Hence no BAT/SR use by real hardware: the 64-bit CPU lacking BATs is a non-issue.
The Mac runs in user mode with the host MMU; that is why it stops at Mac OS 9.0.4 (no real MMU).

**PVR.** `main_unix.cpp` ~400-610 reads `/proc/cpuinfo` ("cpu : Cell Broadband Engine" is an entry in
`cpu_specs`, PVR 0x00700000, line 518), defaults 604 or 7400 (AltiVec build). Remap list (line ~594)
turns 7450/7455/7457/7447A/7448 and the 970 family (0x39, 0x3c, 0x44) into 7400; **Cell (0x70) is not in
the remap list**, and `rom_patches.cpp` ~ line 780-910 (a switch on `PVR >> 16`) has no case 0x70
(prints "WARNING: Unknown CPU type", leaves cache/TLB info table unset). Fix: add 0x0070 to the remap
(simplest) or a case copying the 970 values. Also set an env/pref `cpu`? (there is a prefs path at line
~446 that parses a user PVR: untested whether a pref overrides.) This is the "tiny patch".

**dcbz (the big one).** Mac OS 9 and apps assume 32-byte cache lines (G3/G4). On 970/G5/Cell `dcbz`
clears 128 bytes (Talospace "dcbz considered harmful": talospace.com/2018/08/making-your-talos-ii-into-power-mac_29.html,
about KVM-PR; the 970 case in `rom_patches.cpp` publishes 128-byte blocks to the OS, but only if PVR is
not remapped to 7400 first, which the 970 remap does, so G5 users get the G4 values; I found no
report of G5 breakage nor a fix in SheepShaver). Options: (a) tell Mac OS the truth (add a Cell case with
data cache block size 0x80, like the 970 entry); OS-internal code (BlockZero etc.) adapts, apps/game
runtimes with hard-coded 32-byte dcbz (MWCRuntime memset/BlockClear in some CodeWarrior builds) would
corrupt memory beyond their block; (b) scan and patch dcbz in loaded code (what KVM-PR does per page):
not possible in a user-space native design without mprotect+SIGSEGV tricks; (c) fall back to the
interpreter (kpx_cpu emulates dcbz at 32). A quick test will tell how bad it is: boot Mac OS 9, run a
few apps.

**68k emulator.** In native mode there is none in SheepShaver: the PowerMac ROM contains the Apple 68k
emulator as PPC code, which runs natively. In emulated mode the kpx_cpu PPC emulator runs that ROM code.
(SheepShaver does not have a separate 68k->PPC recompiler; `EMULATED_68K` bits are Basilisk's job.)
Hence Mac OS 9's 68k apps run at the speed of Apple's emulator on a ~3.2 GHz in-order core, roughly
a fast PowerMac (guess).

**Known users.** Searches found no report of SheepShaver on PS3 Linux / Yellow Dog / Cell, or on G5
Linux. Wikipedia/gunkies list Linux ppc as a supported target historically (SheepShaver 2.x era, G3/G4
only). SheepSforza (github.com/classilla/sheepsforza, 2022) is the one modern PPC-64 port, on
ppc64le with the interpreter, with `mmap_min_addr=0` and a fixed-address workaround; he found it
usable but slow (StuffIt of a 37 MB .sit took ~5 minutes) and notes no exception handling in the
interpreter core.

## 2. If native does not work

- **SheepShaver interpreter** (`--enable-ppc-emulator=yes`, kpx_cpu; `--enable-jit` is x86 only). On a
  big-endian host the guest needs no byte swapping, which helps. SheepSforza data point: POWER9 ~4 GHz
  out of order, usable desktop, heavy tasks slow. Cell PPE in-order at 3.2 GHz would be maybe 3-5x
  slower than that (guess): Mac OS 9 desktop slow but running, games mostly not playable.
- **QEMU TCG** (`qemu-system-ppc -M mac99 -cpu g4`): Debian ppc64 has `qemu-system-ppc` 1:11.1.2+ds-3
  in main. TCG has a ppc64 BE host backend. Reports on ARM and x86 hosts say Mac OS 9 under TCG is slow
  and slower than SheepShaver (68kmla.org threads, community.mnt.re/t/old-mac-os-on-reform-with-qemu/881);
  Talospace says OS 9 is still "pretty snappy" under TCG on a 4 GHz POWER9. Whole-system emulation of the
  Mac's devices costs more than SheepShaver's high-level hooks. On the PPE, expect it slower than
  SheepShaver native by 20x or more (guess); useful for installing media, not games. Also needs the
  Cell to not choke on QEMU's 256 MB RAM budget; mac99 with 128 MB guest is feasible, tight.
- **KVM-PR**: not possible in practice. Our kernel has no KVM; PR mode would need the host MMU/SLB
  under our control, but under lv1 the guest uses hypercalls for the hash table and has MSR[HV]=0 (not
  verified against Kconfig: the raw.githubusercontent fetch was rate-limited). Even on POWER9 KVM-PR
  could not boot OS 9 from CD per Talospace. Skip.

## 3. Basilisk II

- Debian `basilisk2` (contrib): sid 0.9.20251105+dfsg-2 for amd64 arm64 armhf i386 loong64 **ppc64el**
  riscv64 (packages.debian.org). **Not built for ppc64 or powerpc** (contrib Packages for both are empty
  of it, checked 2026-10-19; contrib has only 143 packages on ppc64). So build from source:
  `macemu/BasiliskII/src/Unix` autoconf; the `uae_cpu` interpreter is portable C++, configure handles
  `powerpc*` (lines ~195-201, linker script `linux-ppc.ld` for 32-bit real addressing). On 64-bit hosts
  build with `--enable-addressing=banks` or direct per configure; JIT is x86/x86-64 only. SDL2 video/audio.
  Can run 64-bit ppc64 (Debian does it on ppc64el), so no chroot needed; BE host needs no byte swaps.
- Speed (guess): UAE interpreter on a 3.2 GHz in-order core: a 68040 around 25-40 MHz equivalent, enough
  for System 7.5/8.1 and 68k games; the PPE's weak branch handling makes interpreters ~half the speed of a
  big out-of-order core.
- Needs a 68k ROM (Quadra 650/800/900/950 style 1 MB) the user owns.

## 4. Legal / data

User must supply:
- **PowerMac "Old World" ROM** (the 4 MB PCI PowerMac ROM, e.g. Power Mac 7300/7500/9600, or the "Mac OS
  ROM" file from a Mac OS 8.5/8.6/9.0.x CD's System Folder; 9.0.4 needs an old-world ROM): own-hardware only.
- **68k ROM** for Basilisk II, same rule.
- Mac OS install media: 7.5.3 was released free by Apple (19 disk images), 7.5.5 update on top; Apple
  has since removed the downloads (2015 Apple Discussions thread). Redistribution status is murky; do not
  ship. Mac OS 7.6, 8.x and 9.x are commercial: user's own CD (Mac OS 9.0.4 or 9.0 plus update). Nothing
  should enter our repo or published images.
- HFS disk image (SheepShaver uses plain raw/HFS images; `hfsutils`/Disk Copy for creating).

Draw titles (classic-Mac-only, no modern port, from memory, verify before relying):
- Bungie: *Myth: The Fallen Lords* / *Myth II* (PPC), *Pathways Into Darkness*, *Oni* (Mac port;
  Daodan is a Windows reimplementation); *Marathon* trilogy is covered by Aleph One (does not count).
- Ambrosia/Pangea: *Escape Velocity Override/Nova* (EV Nova has no open port; Naev is a successor),
  *Barrack*, *Apeiron*, *Harry the Handsome Executive*, *Mars Rising*; Pangea's *Nanosaur*, *Bugdom*,
  *Cro-Mag Rally*, *Otto Matic* have open ports (do not count); *Maelstrom* has SDL ports (does not count).
- *Glider PRO*, *Dark Castle*, *Crystal Quest*, *Spaceward Ho!*, *Sim Tower* (Mac), *SimCity 2000/3000*
  Mac versions, *Prince of Persia* Mac (SDLPoP covers), *Cosmic Osmo*, *Spectre*, *Shufflepuck Cafe*,
  *Fate of the Dragon*, *Lode Runner* Mac, early *Warcraft II* Mac, *Diablo* Mac (DevilutionX covers),
  *Descent*/*Quake*/*Unreal* Mac (ports exist). Many LucasArts/Sierra Mac adventures are covered by ScummVM.
- Games for 68k Basilisk II: *Dark Castle*, *Shufflepuck*, *Crystal Quest*, *Spaceward Ho!*, *Glider*,
  *Dungeons of Doom*, *Maelstrom 1.0*, *SimAnt*, *MacCamel* era shareware.
The genuinely novel draw is Myth I/II, Oni, EV Nova, Glider PRO and the Ambrosia library on the PS3;
they are 2D or software-rendered, so a 640x480 or 800x600 guest fits SheepShaver without GPU acceleration.

## 5. Display / input / RAM

- SheepShaver SDL2 video: guest depth 8/16/32 at chosen size, scaled by SDL (renderer scaling) to 1080p
  fullscreen; our SDL2 (GL on the RSX nv30) can do the scaling; set `screen win/800/600` or fullscreen
  mode in prefs (`screen`), and a hardware cursor is default with SDL2 (avoids slow 68k polling).
  Frame conversion from the Mac's big-endian 32-bit ARGB on a big-endian host needs no swap (cheaper
  than on x86); note our SDL BGRA/RGBA quirks (docs/GAME-PLAN.md row about SDL3 window surface) apply.
- Mouse/keyboard: SDL; `keycodes` file; Cmd/Option swap. Sound: SDL audio (SheepShaver has OSS/ESD and SDL
  audio code; Mac OS 9 sound manager is 16-bit stereo). Use our ALSA/PipeWire stack.
- RAM: SheepShaver itself is small. A 64 MB Mac (ramsize pref) fits OS 9 comfortably (OS 9 minimum is
  32 MB; 64 MB to run games); 128 MB is the ceiling of the 217 MB budget but would crowd the desktop;
  use game mode (PS3_GAME_MODE) to free memory. Disk images on /home or the data disk.

## 6. Novelty check

No PS3 port found: web searches for PS3 / GameOS + SheepShaver / Basilisk II / Mini vMac returned nothing
(only generic macemu pages; macemu's own README lists macOS, Linux x86/arm64 and MinGW only). Not
exhaustive (PSX-Place not searched directly).

## Concrete first test

1. On the build host: `debootstrap --arch=powerpc --foreign sid build/tmp/macos/chroot32 https://deb.debian.org/debian-ports`
   (qemu-user-static powerpc), install `build-essential autoconf libsdl2-dev libgtk-3-dev libasound2-dev libx11-dev`.
2. In the chroot: `cd macemu/SheepShaver/src/Unix && ./autogen.sh && ./configure --enable-ppc-emulator=no && make`
   with the Cell patch (add `case 0x0070` to the PVR remap in `main_unix.cpp` ~594 and, safer, a Cell case in
   `rom_patches.cpp` copying the 970 entry, 128-byte data block). Build with `-Wl,-T,ldscripts/linux-ppc.ld`
   (the Makefile does).
3. On the PS3, when the user is not benchmarking and says go: copy the chroot or just the binary plus 32-bit
   libs, `sysctl vm.mmap_min_addr=0`, run with a `~/.sheepshaver_prefs` (`ramsize 67108864`, `rom`, `disk`,
   `screen win/640/480`, `nosound false`). User supplies: old-world ROM (4 MB), a Mac OS 9.0.4 install CD image
   (or install 7.5.3/8.1 for Basilisk), plus an empty HFS image created by SheepShaver.
4. First outcomes to record: does it boot to Finder; is `dcbz` corruption visible (random crashes in apps);
   a clock/benchmark (Speedometer, MacBench); then try one game (Glider PRO, EV Nova demo, Myth II demo).
5. Fallback in parallel (cheap, no chroot): native ppc64 Basilisk II build for System 7.5.3/8.1 plus a 68k ROM.
6. If native dcbz fails and cannot be tamed: rebuild with `--enable-ppc-emulator=yes` (interpreter) and
   measure; stop there (effort L for any recompiler work, not worth it).

## Sources
- https://github.com/kanjitalk755/macemu (cloned; paths above)
- https://www.talospace.com/2022/02/sheepsforza-sheepshaver-power-mac.html ; https://github.com/classilla/sheepsforza
- https://www.talospace.com/2018/08/making-your-talos-ii-into-power-mac_29.html
- https://deb.debian.org/debian-ports/dists/unstable/ (Release, main/contrib Packages for ppc64 and powerpc)
- https://packages.debian.org/search?keywords=basilisk&searchon=names ; https://tracker.debian.org/pkg/basilisk2
- https://bugs.archlinux.org/task/79656 (mprotect at 0x0)
- https://community.mnt.re/t/old-mac-os-on-reform-with-qemu/881 ; https://68kmla.org (QEMU vs SheepShaver speed anecdotes)
- https://lowendmac.com/2013/classic-mac-os-downloads-and-updates/ (7.5.3 free status)
- Local: build/tmp/netboot/boot/config-6.12.111+ps3.152-ps3-release
