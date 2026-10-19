# Emulators on the PS3 (research, 2026-10-19)

Parked: the user puts games new on the PS3 first, and most emulators have GameOS ports already. Nothing here was run on the console; speeds and RAM are estimates.


Rules applied: only a GL 3+/GLES 3/Vulkan need with no fallback is a hard no; build/dependency problems are costs.
Speed estimates are guesses from the PPE (in-order, ~P4-class at best), not measurements.

| Rank | Emulator | Debian ppc64 | Verdict | Notes |
|---|---|---|---|---|
| 1 | MAME 0.289 | mame 0.289+dfsg1-1 (74 MB deb, 517 MB installed, needs Qt6 libs, mame-data 17 MB) | try now | Debian builds ppc64 with BIGENDIAN=1 PTR64=1 OSD=sdl, so BE is a supported target. UML has only x64/arm64 native backends; ppc64 uses the C backend (drcbec), so MIPS3/PPC/SH-x systems are slow. 8-bit-era arcade (6502/6809/Z80) runs on plain interpreters: fine. Video: `-video soft` always works, `-video opengl` is a GL 2.1 path, bgfx opengl backend may work (UNVERIFIED). RAM: small drivers ~100-200 MB RSS (UNVERIFIED), big ones more. Benchmark: `-bench N` (unthrottled, prints "Average speed: X%"). Free ROMs: mamedev.org/roms (gridlee, circus, alienar, robby ...; 6-28 KB zips, free non-commercial, distribution "this site only": do not bundle; fetch in setup). |
| 2 | Mednafen 1.32.1 | mednafen (4 MB) | try now | BE-clean (Linux PPC supported upstream). Cores NES, SNES, GB/GBA, Lynx, PCE, PCFX, NGP, WSwan, VB, SMS/MD, Saturn, PSX. Own GL path (opengl driver, needs only old GL) plus softfb/sdl. `-sound 0 -nothrottle 1` for speed runs, FPS on screen. Needs user ROMs; BIOS for PSX/Saturn/PCE-CD. |
| 3 | RetroArch 1.22.2 + Debian cores | retroarch (6.7 MB) + cores: nestopia, mgba, gambatte, sameboy, bsnes-mercury (3 profiles), desmume, beetle-pce-fast, beetle-psx, beetle-vb, beetle-wswan | try now | `video_driver=gl` is GL 2.1 capable, `sdl2` software fallback; `glcore` (GL 3.2) is the no-go driver. RetroArch has an official PS3 GameOS port so most cores are BE-clean. Bench: fastforward (`fastforward_ratio=0`) with the Gallium HUD fps; `--max-frames N` exists. Other cores (snes9x, genesis_plus_gx, picodrive, fceumm, fbneo, mame2003-plus, stella, handy, prosystem, scummvm, dosbox-pure, mupen64plus-next, pcsx_rearmed, ppsspp, flycast) are NOT in Debian: need our build. |
| 4 | Nestopia 2.0.0 (nestopia, nestopia-jg) / FCEUX 2.6.5 | yes | try now (NES only) | Light; NES at full speed expected. FCEUX has a Qt GUI, heavier deps. |
| 5 | mGBA 0.10.5 (mgba-sdl) | yes | try now | BE-capable (ports to Wii/PPC exist). GBA full speed likely. Needs GBA BIOS optional (built-in HLE BIOS). |
| 6 | Stella 7.0 | yes | try now | SDL2 renderer, 2600 trivial for the CPU. Needs ROMs. |
| 7 | Hatari 2.6.1 | yes | try now | Atari ST; needs a TOS or EmuTOS image (emutos not in the ports index). Light. |
| 8 | openMSX 21.0 | yes (+ cbios free BIOS, openmsx-data) | try now | C-BIOS boots without a user BIOS; software needed. Moderate CPU. |
| 9 | Fuse 1.10 (fuse-emulator-sdl) | yes; spectrum-roms is non-free (enable ps3-licensed) | try now | Spectrum, trivial CPU load. |
| 10 | DOSBox 0.74-3 / DOSBox-X 2026.08 | yes | try now | DOSBox 0.74 has no dynarec on ppc64 (core=normal): 486-class games at best; DOSBox-X heavier. Free content: DOS shareware. |
| 11 | FS-UAE 3.2.35 | yes | try (needs Kickstart ROM from user) | PPC JIT not used on ppc64; 68000 interpreter in C is ok for OCS games on this PPE. |
| 12 | DeSmuME 0.9.13 | desmume + libretro-desmume | try later | Software 3D renderer works without GL 3; interpreter only (JIT x86/ARM): many DS games under full speed. melonDS not in Debian ports (software renderer exists, OpenGL renderer wants GL 3.2). |
| 13 | PCSXR 1.9.94 / beetle-psx | yes | try later | Dynarec is x86/PPC32-era; on ppc64 the interpreter runs: PSX games expected well below full speed. beetle-psx software renderer is the safe choice. |
| 14 | Mupen64Plus | only plugin metapackages and data in the index, core/ui not found | needs our build | Dynarec: x86/ARM only, interpreter on PPC. Rice/glide64 plugins use GL 2.x fixed function: plausible but slow. |
| 15 | VICE (C64) | not in the index | needs our build | Light CPU; GTK UI is a dependency cost. |
| 16 | PPSSPP | not in the index | needs our build / likely out | GLES2 path exists but interpreter-only on PPC: far from playable. |
| 17 | Dolphin, PCSX2, RPCS3, Cemu, Flycast, Xemu | dolphin-emu only dolphin-emu-data in index | out | GL 3+/Vulkan required or hopeless CPU load (Dolphin's PPC JIT is for PPC guest on x86/ARM hosts; no host-PPC backend). |
| - | zsnes, higan/ares (ares 148 is in the index) | ares yes | out | ares/bsnes want GL 3.2+/Vulkan renderers (ares has a software path via its own video driver; unverified), too heavy. |

## Test content
- MAME: zips from https://www.mamedev.org/roms/<name>/<name>.zip (verified HTTP 200: gridlee 25 KB, circus 6 KB, alienar 17 KB, robby 28 KB). Not redistributable by us: fetch in the recipe's setup().
- NES: a 24 KB CC0 test ROM (vertical stripes scroll, background colour cycle, pulse tone) was generated by build/tmp/emulators/ps3bench-nes.py (output ps3bench.nes there). Not committed: scope cut.
- MSX: cbios deb. Spectrum: spectrum-roms (non-free). Other consoles: user's own ROMs or homebrew to be vetted (240p Test Suite's license is unclear on GitHub: "Other").

## Install commands for the PS3 (all in debian-ports ppc64, nothing installed by me)
apt install mame mame-data retroarch libretro-nestopia libretro-mgba libretro-gambatte libretro-desmume mednafen nestopia-jg fceux mgba-sdl stella hatari openmsx cbios fuse-emulator-sdl dosbox dosbox-x fs-uae

## Not done (scope cut)
No recipes, no selftest run, nothing committed. Draft recipe design: mame.sh base `mame gridlee -rompath ~/.cache/mame-free-roms -nowindow -resolution 1920x1080 -video opengl -homepath ~/.mame`, variant bench `-bench 300 -video none -sound none` with STEADY_RE 'Average speed: ([0-9.]+)%'. Untested: whether `-bench` output goes to stderr, bgfx on nv30, RetroArch fastforward via Space.
