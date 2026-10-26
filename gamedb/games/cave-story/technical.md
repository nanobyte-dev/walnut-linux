# Cave Story: technical notes

Also known as: Doukutsu Monogatari.

## Engine options

- NXEngine-evo (the C++ pick: SDL2, many console ports; 552 stars)
- doukutsu-rs (the most popular, 1.3k stars: Rust, GL/GLES3 or the SDL2 renderer with `CAVESTORY_NO_OPENGL=1`)
- CSE2 (a portable C++ decompilation)

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-20, [pc-classics.md](../../research/pc-classics.md), "Platformers and 2D action": Cave Story (2004): NXEngine-evo (C++, SDL2, many console ports) is the pick; doukutsu-rs is more popular but Rust; complete; freeware; effort S.
- 2026-10-19, [game-reeval.md](../../research/game-reeval.md), "doukutsu-rs (Cave Story)": no hard no: `backend-sdl` tries desktop GL, GLES, then `SDL2Renderer` (`backend_sdl2.rs:715-721`); its GL path uses GLES3/GL 3 shaders, so it must run on the SDL2 renderer here. Not in Debian: cargo build with `--no-default-features --features backend-sdl,ogg-playback,exe` (M). Big-endian: `byteorder` for data files; `sound/mod.rs:522` uses `i16::from_ne_bytes` on audio; BE cost S-M; Rust supports powerpc64-unknown-linux-gnu. RAM estimate 40-100 MB. Data: Cave Story freeware (2004, about 5 MB), not open-licensed. Effort M. Verdict: WORTH A BUILD.
