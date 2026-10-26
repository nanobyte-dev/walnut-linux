# DDNet: technical notes

## Status

Our DDNet 20.1.1 (`ddnet`, `ddnet-data`, `ddnet-server`, `20.1.1+ds-1+ps3.N`,
[packages/ddnet](packages/ddnet/)): Debian's newer `ddnet` source, put in by hand and pinned by its .dsc
hash (`SOURCE_SNAPSHOT`, because the pinned snapshot still had 20.0 and ppc64 only a stale 16.0.2), plus
one patch. [0001](packages/ddnet/patches/0001-Sockets-non-blocking-on-big-endian-64-bit-too-FIONBI.patch)
makes the sockets non-blocking on big-endian 64-bit: the unsigned long FIONBIO bug that
[Teeworlds](../teeworlds/technical.md) had too, where the client sat in `recvfrom()` at the loading
screen. Debian's own ppc64 build has the same bug, so `dpkg -l ddnet` must show `+ps3`.

Smoke-tested once (gamedb:batch/2026-10-19-ssdroot4-smoke): it runs with sound, but its log has 95 "out of
VRAM" lines (205 MB of VRAM in use) and a 15.5 s longest frame; a run with the stalls recorder is to say
where they come from (docs/TESTS.md). Not played yet. The recipe ([benchmark/recipe.sh](benchmark/recipe.sh))
starts a `DDNet-Server` on the PS3 and the SDL2 client connects at 1920x1080 fullscreen; with no single
player mode, the player runs around alone. DDNet's default on a non-x86 build is the Vulkan backend, then
OpenGL 3.0 and down, one failed context at a time; everything below 3.3 is its legacy fixed-function
renderer, so the base asks for `gfx_backend OpenGL` with a 2.1 context. Variants `gl15`, `gl11`,
`default` (the fall-back chain, with its cost), `menu`, `window`.
