# Teeworlds: technical notes

## Status

Our `teeworlds`, `teeworlds-server` (`0.7.5-3+ps3.N`, [packages/teeworlds](packages/teeworlds/)) with
Debian's `teeworlds-data`. It runs: in game on dm1 at 183 fps (2026-10-18, gamedb:batch/2026-10-18-smoke-reruns-watermark),
after our big-endian fix of its sockets ([the investigation](investigations/2026-10-18-fionbio-blocking-sockets.md);
the upstream offer in [upstream.md](upstream.md)). Not played yet; not checked yet: the sound by ear, a second
run, and whether the DXT5 path logs libspeq lines. Teeworlds deploys by name: `make deploy
PKGS="teeworlds teeworlds-server"`, then `dpkg -l teeworlds teeworlds-server | grep ps3` must show our
version.

Smoke-tested: one launch to see whether it starts and plays (`SUITE=0`, no benchmark numbers kept; the smoke runs are described in [SUITE.md](../../../docs/games/SUITE.md#smoke-runs)). The recipe ([benchmark/recipe.sh](benchmark/recipe.sh)) starts a `teeworlds-server` on the PS3 (map dm1, no
master server registration, 4 slots) and the SDL2 client connects at 1920x1080; there is no single player
and no bots, so the player runs around alone. Variants `notc`, `menu`, `window`, `s3tcppe`, `s3tcspu`.
`gfx_texture_compression 1` makes the client upload `GL_COMPRESSED_RGBA`, which Mesa turns into DXT5, and the
tile maps go up as one 3D texture. No IPv6 in the kernel: it prints socket errors for it, harmless.

## Investigations

- [2026-10-18 the sockets stayed blocking](investigations/2026-10-18-fionbio-blocking-sockets.md) (0001).
