# DarkPlaces: technical notes

Debian's DarkPlaces (0~20180908 and the 2026 git snapshot) has only GL 3.2 core and GLES2 renderpaths, so it
can't run on the RSX. The last snapshot with the GL 1.1/1.3/2.0 renderpaths (`RENDERPATH_GL11/GL13/GL20`, an
SDL2 window, `vid_gl20 0` for the combiner path) is 0~20180412~beta1, Debian buster's orig tarball
(snapshot.debian.org, [upstream.mk](packages/darkplaces-gl2/upstream.mk)). Our `darkplaces-gl2` ([its package](packages/darkplaces-gl2/), `TYPE=upstream`,
`make darkplaces-gl2-deb`, 2 MB) builds it with Debian's 13 patches (link options, no SSE rasterizer off x86,
GPU skinning off) and `-std=gnu17` (the code uses `false`/`true` as enum names; gcc 15 defaults to C23). It
installs `/usr/games/darkplaces-gl2` beside Debian's engine and `/usr/games/nexuiz-gl2`. The one game on it is
[Nexuiz Classic](../../games/nexuiz/technical.md); built, not yet installed on the PS3.

**Big-endian**: DarkPlaces swaps through `LittleLong` and friends (`mem_bigendian` at run time) and Debian
builds its successor on s390x. Under qemu-user the dedicated server loaded `aggressor` from the real Nexuiz
data (1222 faces, 386 clusters, progs.dat, csprogs.dat, 206 entities, "Server spawned") and ran two minutes
without a crash: file loading and QuakeC are checked, not the renderer, the sound or the client.
