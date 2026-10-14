# Performance (started 2026-10-14)

Goal: a responsive lightweight desktop. The PPE is in-order (per thread closer to a Pentium 4
than a Core 2), and Linux gets 165 MB of the 256 MB XDR, so the target is "snappy LXQt", not
Core 2 Duo numbers. Measure before and after every change.

## Found so far

| Problem | Effect | Fix |
|---|---|---|
| ps3_defconfig enables lockdep and lock/list debugging (PROVE_LOCKING, DEBUG_LOCKDEP, DEBUG_LIST, DEBUG_WW_MUTEX_SLOWPATH, DEBUG_RWSEMS, DEBUG_IRQFLAGS) | every lock checked against a dependency graph; kernel image 30 MB in RAM (lockdep tables ~10 MB of BSS) | turned off: 15 MB |
| nfsroot default rsize/wsize = 4 KB | 8 MB/s sequential, ~24 ms per page-fault read, ~95% system time during installs | 64 KB on the cmdline |
| PulseAudio (33 MB), blueman helper (25 MB) running at idle | ~58 MB of 165 | removed / on demand |
| No perf events | can't profile | CONFIG_PERF_EVENTS |

## Next

1. Baseline after the fixes above: boot to shell time, LXQt start time (splash to desktop),
   idle `free -m`, `perf top` during LXQt start.
2. GART reservation (32 MB of RAM): measure peak use under the desktop; shrink if possible.
3. Swap in VRAM: ~200 MB of the RSX's 256 MB is unused. A swap device in ps3rsx that copies
   pages with the GPU (like the old ps3vram) instead of compressing them on the PPE (zram).
4. Desktop footprint: LXQt components vs lighter alternatives, with numbers.
5. Local root (USB drive, the live-image goal) if NFS still shows up in profiles.
