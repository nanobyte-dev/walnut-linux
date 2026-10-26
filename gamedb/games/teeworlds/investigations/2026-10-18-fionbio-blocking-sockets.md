+++
id = "fionbio-blocking-sockets"
games = ["teeworlds"]
driver_patches = []
game_patches = ["teeworlds/0001"]
sessions = []
batches = ["2026-10-18-smoke-reruns-watermark"]
+++

# Teeworlds: the sockets stayed blocking on big-endian 64-bit (2026-10-17/18)

**Symptom.** The client connected to the local server and then hung: the screen kept the last loading
frame, the CPU idled.

**Evidence.** Found in the ppc64 chroot under qemu-user (Xvfb, llvmpipe, qemu's gdb stub with Debian's
dbgsym, libfaketime at a tenth of the speed so the connection's 10 s timeouts outlast a frame). The client
got the token and the accept, then blocked in `recvfrom()` inside `CNetClient::Recv`, waiting for a packet
the server never sent: the server waits for the client's NETMSG_INFO, which the client sends only on its
next loop pass.

**Cause.** Big-endian, not the network or the textures: `net_set_non_blocking()` gives `ioctl(FIONBIO)` a
pointer to an `unsigned long`; Linux reads an `int` there, which on ppc64 is the long's high half, 0. The UDP
sockets stayed blocking.

**Fix.** Our 0001 passes an `int` everywhere but Windows (840a3b5, +ps3.422): in game on dm1 at 183 fps
(gamedb:batch/2026-10-18-smoke-reruns-watermark). No Debian or upstream report existed; upstream master still
has the `unsigned long`. The draft for upstream is in [upstream.md](../upstream.md).
