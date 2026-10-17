# Performance (started 2026-10-14)

Goal: a responsive lightweight desktop. The PPE is in-order (per thread closer to a Pentium 4
than a Core 2), and Linux gets 165 MB of the 256 MB XDR, so the target is "snappy LXQt", not
Core 2 Duo numbers. Measure before and after every change.

**Status 2026-10-17:** a record of the first days. Since then the desktop is Openbox + PCManFM
(DESKTOP-PLAN.md), the root is on the USB disk, the GART grows on demand from CMA, swap lives in
VRAM (ps3vswap, PLAN.md) and the release kernel gives Linux 222 MB. The "Next" list below is done
or superseded; game and driver performance is in GAME-PLAN.md, open ideas in IDEAS.md.

## Found so far

| Problem | Effect | Fix |
|---|---|---|
| ps3_defconfig enables lockdep and lock/list debugging (PROVE_LOCKING, DEBUG_LOCKDEP, DEBUG_LIST, DEBUG_WW_MUTEX_SLOWPATH, DEBUG_RWSEMS, DEBUG_IRQFLAGS) | every lock checked against a dependency graph; kernel image 30 MB in RAM (lockdep tables ~10 MB of BSS) | turned off: 15 MB |
| nfsroot default rsize/wsize = 4 KB | 8 MB/s sequential, ~24 ms per page-fault read, ~95% system time during installs | 64 KB on the cmdline |
| PulseAudio (33 MB), blueman helper (25 MB) running at idle | ~58 MB of 165 | removed / on demand |
| No perf events | can't profile | CONFIG_PERF_EVENTS |

## Measured (2026-10-14, after the fixes above)

- Boot: systemd 13.8 s (1.2 kernel + 12.6 userspace); SSH answers 21 s after the kexec (was ~40 s).
- RAM: 177 MB total, 117 MB available at the shell (was 165 / ~80).
- Icon theme install: 86 s (with 4 KB NFS it had not finished unpacking after 20+ minutes).
- **`perf record -a -g` hung the PS3**: its `sleep 60` workload blocked in D state ~4 min after
  boot, then userspace and the network stopped (netconsole kept printing hung-task warnings).
  Suspected: system-wide call-graph sampling (Cell PMU behind lv1). Don't use `-a -g`; try
  `perf stat` first, then `perf record` on one process without `-g`, supervised.

- LXQt start (X up to desktop window): 25 s (was 2-3 min). Idle desktop: 75 MB available,
  90 MB in zram, no swap traffic; lxqt-panel 24 MB, pcmanfm-qt 32 MB, Xorg 21 MB RSS.
- `top`/`vmstat` show ~98% hardirq ("hi") even with X stopped and ps3rsx unloaded: an artifact
  of tick-based CPU accounting on the PS3. A CPU-bound shell loop gets the whole CPU (11.2 s
  real, 11.17 s user).
- That loop is 11x slower than on the dev PC (Ryzen-class): branchy, interpreted code is where
  the in-order PPE hurts most.

## Next

1. Baseline after the fixes above: boot to shell time, LXQt start time (splash to desktop),
   idle `free -m`, `perf top` during LXQt start.
2. GART reservation: 16 MB since 2026-10-15 (was 32). Measured with Openbox + ClassiCube:
   27 MB used while Mesa put vertex buffers in GART and each process/context had 4 x 512 KB
   pushbufs; 9 MB once vertex buffers moved to VRAM and PS3 pushbufs shrank (2 x 64 KB per
   screen, 2 x 256 KB per context). GTK apps no longer load the GL driver (GDK_GL=disable).
   ClassiCube's occasional stutters coincided with kswapd/zram activity and NFS major faults.
3. Swap in VRAM: ~200 MB of the RSX's 256 MB is unused. A swap device in ps3rsx that copies
   pages with the GPU (like the old ps3vram) instead of compressing them on the PPE (zram).
4. Desktop footprint: LXQt components vs lighter alternatives, with numbers.
5. Local root (USB drive, the live-image goal) if NFS still shows up in profiles.
