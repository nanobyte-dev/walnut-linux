# Mindustry: technical notes

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "Ranked table": Mindustry v160.7 (Java): no hard no (Arc/libGDX GL 2.0 path). Needs Arc/LWJGL/SDL natives for ppc64 BE (the jar carries x86/arm only). Debian's openjdk-21/25 on ppc64 is full HotSpot (C1/C2 JIT), so the VM is not the blocker; the cost is building the natives and checking Arc's byte order. Measured 760 MB RSS at the menu with `-Xmx300m -XX:+UseSerialGC` (100-200 MB of it llvmpipe). Effort L. Verdict: not worth it now (revisit).
- 2026-10-19, [gl3-census-b-2d.md](../../research/gl3-census-b-2d.md), "Mindustry (Anuken, Java, Arc framework with SDL2 JNI backend, release v160.7)": not GL3-blocked: runs on a GL 2.1 context (measured, menu) with GLSL 1.10-style shaders. Blockers: the JVM on ppc64 BE, Arc's prebuilt natives (x86/arm only) and memory (663-704 MB RSS at the menu with -Xmx256m). Scores: popularity 5, novelty 5, playability 1, effort XL; data free, 89 MB jar.
