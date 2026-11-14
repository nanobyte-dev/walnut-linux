# DDNet: technical notes

## Status

Our DDNet 20.1.1 (`ddnet`, `ddnet-data`, `ddnet-server`, `20.1.1+ds-1+ps3.N`,
[packages/ddnet](packages/ddnet/)): Debian's newer `ddnet` source, put in by hand and pinned by its .dsc
hash (`SOURCE_SNAPSHOT`, because the pinned snapshot still had 20.0 and ppc64 only a stale 16.0.2), plus
one patch. [0001](packages/ddnet/patches/0001-Sockets-non-blocking-on-big-endian-64-bit-too-FIONBI.patch)
makes the sockets non-blocking on big-endian 64-bit: the unsigned long FIONBIO bug that
[Teeworlds](../teeworlds/technical.md) had too, where the client sat in `recvfrom()` at the loading
screen. Debian's own ppc64 build has the same bug, so `dpkg -l ddnet` must show `+ps3`.

On 2026-11-14 (kernel .1529, ps3rsx 0.1534, Mesa ps3.1508, gamedb:batch/2026-11-14-s1114b-smoke) the client and its local `ddnet-server` ran with the stall recorder. With the VRAM swap off (the smoke default) they filled the 384 MB disk swap and the kernel OOM-killed DDNet at 58 s; with `NOVSWAP=0` it plays (the timer, the HUD, the player on the map) at 83 fps steady, but with 35.9 s of stalls in the window, a 25.7 s longest frame, 26 `ps3rsx: out of VRAM` lines (buffers up to 10.9 MB) and a white rectangle in the sky, probably a texture whose allocation failed. RSS 121 MB, VRAM 184 MB, 16 MB free at the lowest. The recipe needs both the `ddnet` and `ddnet-server` packages (a run without the server sticks on "Connecting to 127.0.0.1:8303").

Smoke-tested once (gamedb:batch/2026-10-19-ssdroot4-smoke): it runs with sound, but its log has 95 "out of
VRAM" lines (205 MB of VRAM in use) and a 15.5 s longest frame; a run with the stalls recorder is to say
where they come from (docs/TESTS.md). Not played yet. The recipe ([benchmark/recipe.sh](benchmark/recipe.sh))
starts a `DDNet-Server` on the PS3 and the SDL2 client connects at 1920x1080 fullscreen; with no single
player mode, the player runs around alone. DDNet's default on a non-x86 build is the Vulkan backend, then
OpenGL 3.0 and down, one failed context at a time; everything below 3.3 is its legacy fixed-function
renderer, so the base asks for `gfx_backend OpenGL` with a 2.1 context. Variants `gl15`, `gl11`,
`default` (the fall-back chain, with its cost), `menu`, `window`.
