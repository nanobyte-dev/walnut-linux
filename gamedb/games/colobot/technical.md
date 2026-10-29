# Colobot: Gold Edition: technical notes

## Status

Debian's Colobot: Gold Edition 0.2.2-alpha (`colobot` 0.2.2-4+b2 and its data). Smoke run on 2026-10-29
(`ps3-bench-20261028T231944Z-smoke-colobot-base`, the gl14 device, `-runscene missions101`, the VRAM swap on):
the first mission plays at 27 fps, the colours right on the webcam. It ran in a window, for two reasons:
the recipe's `-resolution 1920x1080` sets Colobot's `m_resolutionOverride`, with which it ignores the
ini's `Fullscreen` (the recipe now leaves it out), and without any colobot.ini (a first start from the
menu) Colobot opens an 800x600 window.

## Our package

`colobot` 0.2.2-4+ps3.N ([packages/colobot](packages/colobot/)):
[0001](packages/colobot/patches/0001-app-a-fresh-configuration-starts-fullscreen-at-the-d.patch) a fresh
configuration (no `Resolution`, no `Fullscreen`, no `-resolution`) starts fullscreen at the desktop's
mode, 1920x1080 on the PS3 (no mode change); a saved configuration, `-resolution` and the options menu
still decide. On the PC (amd64 build, Xvfb 1280x720, an empty home): fullscreen 1280x720, and it wrote
`Resolution=1280x720`, `Fullscreen=1`. On the PS3 (2026-10-29, the dev build, recipe variant `fresh`, no colobot.ini):
fullscreen 1920x1080 by itself, 27 fps, 1% low 16, and the ini it wrote has `Resolution=1920x1080`, `Fullscreen=1`.
[Packaging 0002](packages/colobot/packaging/0002-debian-build-the-colobot-binary-only-on-Debian-s-dat.patch):
doxygen (a Build-Depends-Indep) isn't installable on debian-ports' ppc64, so only the binary is built, on
Debian's own data packages (depended on by upstream version, >= 0.2.2).

## Known problems

- Fixed: every file Colobot wrote on the PS3 had a NUL for its 513th byte (then one each further 512): the
  colobot.ini of two runs read `ShadowM\0ppingResolution=1024`, and the same would cut saved games and
  the player's programs. Cause: `COutputStreamBuffer::overflow()` wrote the character that didn't fit
  into its full 512-byte buffer with `PHYSFS_write(m_file, &ch, 1, 1)`, `ch` an `int`: its first byte is
  the character on little-endian machines (the PC's file was clean) and 0 on big-endian ones.
  [0003](packages/colobot/patches/0003-COutputStreamBuffer-write-the-overflow-char-not-its-.patch)
  writes the character: the ini a PS3 run wrote with it has no NUL (2026-10-29). Worth offering upstream.

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [gl3-census-f-engines.md](../../research/gl3-census-f-engines.md), "Scan: other games that need GL 3+ (one line each)": OpenGL 1.4 default engine; `-graphics gl21`/`gl33` devices exist (shadows need them); Debian `colobot` 0.2.2-4+b2; ~300 MB. Verdict: GL2 already, runs on the 1.4 or gl21 device; worth a try. One of the best finds of the scan.
- 2026-10-19, [gl3-census.md](../../research/gl3-census.md), "Not GL 3 problems": GL 1.4 default device, in Debian ppc64.
