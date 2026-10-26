# 0 A.D.: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Alternatives and popularity / Strategy": 0 A.D. is inspired by Age of Empires, its own game; complete, C++, GL 2.1 capable.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "Ranked table": no hard no: GL 2.0 + VBO + FBO (EXT ok), a GLSL path, an ARB-program fallback still in `gl/Device.cpp`; Vulkan optional. No ppc64 binary (Debian dropped ppc64 on 2026-02-24). Not SpiderMonkey: mozjs 128 compiled fine on ppc64; the real FTBFS is two `#error` in `BinarySerializer.cpp` (lines 165, 456): convert JS ArrayBuffer data and JS strings to little-endian (saves, replays, sync hashes); fix M. No ppc64 JIT in SpiderMonkey 128, so the JS (AI, GUI) runs interpreted. Measured 800 MB RSS (arcadia, 2 players + 1 AI, 1280x720 low, 3 min), minus llvmpipe ~600-700 MB. Data 3.5 GB installed. Effort L. Verdict: worth a build, low priority; CPU is the larger risk.
- 2026-10-19, [gl3-census-c-rts.md](../../research/gl3-census-c-rts.md), "0 A.D. (Pyrogenesis engine, A27 "Agni" / A28)": out for RAM (2 GB+ typical) and no ppc64 build (debian-ports arch list amd64 arm64 i386 ppc64el); GL is not the problem (GL 2.1 with ARB or GLSL shaders). Scores: popularity 5, novelty 5, playability 1, effort XL; data free, ~2500 MB.
- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": probably out of reach: listed with GL 3.x+ only games, "also far too heavy".
