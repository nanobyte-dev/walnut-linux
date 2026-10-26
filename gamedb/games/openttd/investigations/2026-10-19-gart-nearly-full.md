+++
id = "gart-nearly-full"
games = ["openttd"]
driver_patches = []
game_patches = []
sessions = []
batches = ["2026-10-19-ssdroot4-staging-pool"]
+++

# OpenTTD: "no memory to back the GART" with the staging pool (2026-10-19)

**Symptom.** One of 13 OpenTTD runs since Mesa ps3.544 (`20261019T102932Z-openttd-base`, with
`NV30_STAGING_POOL_MB=24`) logged five `ps3rsx: no memory to back the GART (56 of 63 MiB in use)` within a
second, at 37683 s of uptime; `bench/analysis/logscan.py` flagged it as NEW. The run still finished
(`ended=window`), nothing showed in the picture, and the other twelve runs are clean.

**Cause.** OpenTTD uploads an 8 MB staging buffer per frame; the 24 MB pool (Mesa 0040) and the rest of
the process's buffers reach the 63 MiB GART when something else arrives at the same moment (a second
4 MB buffer, the title screen's textures). This is a different path from the GART-full crashes of other
games: the kernel cannot get pages (CMA chunks) to back the GART, not a full aperture. The draw that asked
for the buffer may have been dropped.

**Fix.** None needed yet. If it repeats (OpenTTD `blit8` with the pool x6, dmesg watched), cap the pool at
16 MB or have the pool release on a failed allocation.
