#!/usr/bin/env python3
"""stalls.py <run dir>...: why the frames over 100 ms took that long (docs/STALLS.md). Reads the
record ps3-stallrec wrote alongside the game (stalls.rec, ps3-bench.sh BENCH_STALLS=1 or run.sh
STALLS=1) and the HUD's frame times, and for every stall (a frame over 100 ms in the measured
window, the frames report.py counts as stalls) sums what the recorder saw while it lasted. Each
recorder sample (default 10 ms) gets one cause, from the game's main thread and the counters:
  disk        a page fault that read a file from the disk (major fault, disk reads, no swap-in)
  swap_zram   a fault that swapped in from zram (pswpin, zram0 reads)
  swap_vswap  a fault that swapped in from the VRAM swap (ps3vswap0 reads; its own counters say
              whether from VRAM or from its disk partition)
  fault       a page fault with no I/O seen (a page another thread is reading, a lock)
  gart        GART growth: CMA allocations (cma_alloc_*: ps3rsx migrating 1 MiB chunks free)
  reclaim     direct reclaim or compaction (allocstall, pgscan_direct, compact_stall moved while
              the thread ran or waited in the kernel)
  gpu         asleep in a DRM ioctl or a fence wait (dma_fence, dma_resv, ps3rsx, drm wchan)
  io          the game's own reads: in D state in read/pread/openat/... (not a page fault)
  cpu         running (shader compiles, the game's own work, kernel work it asked for)
  runq        runnable but not running: the process got under a quarter of the slice's CPU
  wait        asleep by choice: futex (another thread), poll/recvmsg (X), nanosleep
  unknown     D state elsewhere, or no sample
Prints per run: the stall seconds by cause in the window, the same for the load (the client's
start until the load ended) and for the warm-up (the load's end until the window's start: the rest of
the start-up span, docs/PROFILING.md "Start-up profiling"), and the 10 longest stalls with their evidence (faults, swap-ins, disk
reads, reclaim, CPU, the thread's states, wchans and syscalls, kernel stacks, Xorg and kswapd).
report.py imports summary() for its stall_*_s columns. Times are seconds from the client's start,
as in bench.txt."""
import re
import sys
from collections import Counter
from pathlib import Path

BENCH = Path(__file__).resolve().parent.parent   # the bench tree: this is bench/analysis/
sys.path[:0] = [str(BENCH / "analysis"), str(BENCH / "harness")]
import report  # noqa: E402

STALL_MS = report.STALL_MS
CAUSES = ["disk", "swap_zram", "swap_vswap", "fault", "gart", "reclaim", "gpu", "io", "cpu", "runq",
          "wait", "unknown"]
# report.py's columns: causes grouped
COLUMNS = {"stall_disk_s": ("disk",), "stall_swap_s": ("swap_zram", "swap_vswap"),
           "stall_reclaim_s": ("reclaim", "gart"), "stall_gpu_s": ("gpu",), "stall_cpu_s": ("cpu", "runq"),
           "stall_io_s": ("io",), "stall_wait_s": ("wait",), "stall_other_s": ("fault", "unknown")}
GPU_RE = re.compile(r"dma_fence|dma_resv|ps3rsx|drm_|nouveau|ttm_")
READS = {"read", "pread64", "readv", "preadv", "preadv2", "openat", "open", "newfstatat", "statx",
         "getdents64", "fsync", "fdatasync"}
DIRECT = ("allocstall_", "pgscan_direct", "pgsteal_direct", "compact_stall")


class Rec:
    """a parsed stalls.rec"""

    def __init__(self, path):
        self.hdr, self.vm, self.disk, self.sysnr, self.other = {}, {}, {}, {}, {}
        self.W, self.D = {}, {}
        self.t, self.s = [], []     # sample times (s, recorder clock) and their fields
        self.frames = []            # recorder time of each frame's end, by frame index
        self.procs, self.stacks, self.clocks, self.end, self.cost = [], [], [], {}, {}
        nf = 0
        for line in Path(path).read_text(errors="replace").splitlines():
            if not line:
                continue
            c = line[0]
            if c.isdigit():
                f = line.split(" ")
                t = int(f[0]) / 1e6
                d = {}
                for kv in f[1:]:
                    k, _, v = kv.partition("=")
                    d[k] = v
                self.t.append(t)
                self.s.append(d)
                if "f" in d:
                    n = int(d["f"])
                    self.frames += [t] * n
                    nf += n
            elif c == "#":
                f = line[2:].split()
                if not f:
                    continue
                kind, kv = f[0], dict(x.split("=", 1) for x in f[1:] if "=" in x)
                if kind == "stallrec":
                    self.hdr = kv
                elif kind == "clock":
                    self.clocks.append((0.0, float(kv["real"])))  # at the recorder's start
                elif kind == "vmstat":
                    self.vm = {int(k): v for k, v in kv.items()}
                elif kind == "disk":
                    self.disk = {int(k): v for k, v in kv.items()}
                elif kind == "sys":
                    self.sysnr = {int(v): k for k, v in kv.items()}
                elif kind == "other":
                    self.other = {int(k): v for k, v in kv.items()}
                elif kind == "hz":
                    self.hdr["hz"] = kv.get("hz")
                elif kind == "end":
                    self.end = kv
                elif kind == "cost_us":
                    self.cost = kv
            elif c in "WD":
                _, i, name = line.split(" ", 2)
                (self.W if c == "W" else self.D)[i] = name
            elif c == "P":
                f = line.split()
                self.procs.append((int(f[1]) / 1e6, " ".join(f[2:])))
            elif c == "F":
                f = line.split()
                n0 = int(f[2].split("=")[1])
                self.frames += [None] * n0  # written before the recorder found the file
                nf += n0
            elif c == "C":
                f = line.split()
                self.clocks.append((int(f[1]) / 1e6, float(f[2].split("=")[1])))
            elif c == "K":
                f = line.split(" ", 4)
                self.stacks.append((int(f[1]) / 1e6, f[3], f[4] if len(f) > 4 else ""))
        self.hz = int(self.hdr.get("hz") or 100)
        self.interval = int(self.hdr.get("interval_us") or 10000) / 1e6
        self.vmidx = {v: k for k, v in self.vm.items()}

    def real_to_rec(self, real):
        """an epoch time on the recorder's clock (the clock pair nearest to it)"""
        if not self.clocks:
            return None
        t, r = min(self.clocks, key=lambda c: abs(c[1] - real))
        return t + real - r

    def vmsum(self, d, *prefixes):
        n = 0
        for k, v in d.items():
            if k[0] == "v" and k[1:].isdigit():
                name = self.vm.get(int(k[1:]), "")
                if any(name.startswith(p) for p in prefixes):
                    n += int(v)
        return n

    def disks(self, d):
        """{kind: (reads, sectors read, inflight)} for the sample: hdd (every disk that is not a
        swap device), zram, vswap"""
        out = {"hdd": [0, 0, 0], "zram": [0, 0, 0], "vswap": [0, 0, 0]}
        for i, name in self.disk.items():
            v = d.get("k%d" % i)
            if not v:
                continue
            r, rs, _, _, _, fl = (int(x) for x in v.split(":"))
            kind = "zram" if name.startswith("zram") else "vswap" if name.startswith("ps3vswap") else "hdd"
            if kind == "hdd" and any(re.fullmatch(re.escape(x) + r"p?\d+", name) for x in self.disk.values()):
                continue    # a partition of a recorded disk (sda3, nvme0n1p5): counted in the disk already
            o = out[kind]
            o[0] += r
            o[1] += rs
            o[2] += fl
        return out


def classify(rec, d, dt):
    """the cause of one recorder sample"""
    st = d.get("s")
    if st is None:
        return "unknown"
    mj = int(d.get("mj", 0))
    y = d.get("y")
    sysname = rec.sysnr.get(int(y), "nr%s" % y) if y not in (None, "-1") else None
    wchan = rec.W.get(d.get("w"), "")
    fd = rec.D.get(d.get("d"), "")
    dk = rec.disks(d)
    if mj or (st == "D" and y == "-1"):
        pswpin = rec.vmsum(d, "pswpin")
        if pswpin or dk["zram"][0] or dk["vswap"][0]:
            return "swap_vswap" if dk["vswap"][0] >= dk["zram"][0] and dk["vswap"][0] else "swap_zram"
        if dk["hdd"][0] or dk["hdd"][2]:
            return "disk"
        return "fault"
    asleep_gpu = st in "SD" and ((sysname == "ioctl" and fd.startswith("/dev/dri")) or GPU_RE.search(wchan))
    if st in "RD" and not asleep_gpu:
        if rec.vmsum(d, "cma_alloc_"):
            return "gart"
        if rec.vmsum(d, *DIRECT):
            return "reclaim"
    if asleep_gpu:
        return "gpu"
    if st == "D" and sysname in READS:
        return "io"
    if st == "R":
        c = int(d.get("c", 0)) / 1e6
        return "runq" if dt > 0 and c < 0.25 * dt else "cpu"
    if st in "SI":
        return "wait"
    return "unknown"


def overlap(rec, a, b):
    """[(sample index, seconds of it inside [a, b])]: sample j covers (t[j-1], t[j]]"""
    import bisect
    out = []
    j = bisect.bisect_right(rec.t, a)
    while j < len(rec.t):
        lo = rec.t[j - 1] if j else rec.t[j] - rec.interval
        if lo >= b:
            break
        w = min(b, rec.t[j]) - max(a, lo)
        if w > 0:
            out.append((j, w))
        j += 1
    return out


def interval(rec, a, b):
    """what happened in [a, b] (recorder seconds): seconds by cause and the evidence"""
    by = Counter()
    ev = Counter()
    states, wchans, calls, others = Counter(), Counter(), Counter(), Counter()
    covered = 0.0
    for j, w in overlap(rec, a, b):
        d = rec.s[j]
        dt = rec.t[j] - (rec.t[j - 1] if j else rec.t[j] - rec.interval)
        cause = classify(rec, d, dt)
        by[cause] += w
        covered += w
        f = w / dt if dt > 0 else 1     # counters of a sample partly inside count partly
        for k in ("mj", "Mj", "mn", "Mn"):
            ev[k] += int(d.get(k, 0)) * f
        ev["cpu_ms"] += int(d.get("c", 0)) / 1000 * f
        ev["user_ms"] += int(d.get("mu", 0)) * 1000 / rec.hz * f
        ev["sys_ms"] += int(d.get("ms", 0)) * 1000 / rec.hz * f
        ev["pswpin"] += rec.vmsum(d, "pswpin") * f
        ev["pswpout"] += rec.vmsum(d, "pswpout") * f
        ev["direct"] += rec.vmsum(d, "pgscan_direct") * f
        ev["kswapd"] += rec.vmsum(d, "pgscan_kswapd") * f
        ev["allocstall"] += rec.vmsum(d, "allocstall_") * f
        ev["compact"] += rec.vmsum(d, "compact_stall") * f
        ev["cma"] += rec.vmsum(d, "cma_alloc_") * f
        ev["refault"] += rec.vmsum(d, "workingset_refault_") * f
        dk = rec.disks(d)
        for kind, (r, rs, _) in dk.items():
            ev[kind + "_reads"] += r * f
            ev[kind + "_kb"] += rs / 2 * f
        if "vs" in d:
            vr, dr = (int(x) for x in d["vs"].split(":")[:2])
            ev["vs_vram_reads"] += vr
            ev["vs_disk_reads"] += dr
        st = d.get("s")
        if st:
            states[st] += 1
        if st and st != "R":
            if d.get("w"):
                wchans[rec.W.get(d["w"], d["w"])] += 1
            y = d.get("y")
            if y is not None:
                name = "fault" if y == "-1" else rec.sysnr.get(int(y), "nr" + y)
                if d.get("d"):
                    name += "(" + rec.D.get(d["d"], "?") + ")"
                calls[name] += 1
        for i, name in rec.other.items():
            v = d.get("o%d" % i)
            if v:
                p = v.split(":")
                if p[0] != "S":
                    others[name + ":" + p[0]] += 1
                if int(p[1]):
                    others[name + ":majflt"] += int(p[1])
                others[name + ":ms"] += int(p[2]) * 1000 / rec.hz
    if b - a > covered:
        by["unknown"] += (b - a) - covered
    stacks = [s for s in rec.stacks if a - rec.interval <= s[0] <= b + rec.interval]
    return by, ev, states, wchans, calls, others, stacks


def evidence(ev, states, wchans, calls, others, stacks):
    """the evidence of a stall in one line"""
    p = []
    if ev["mj"] or ev["Mj"]:
        p.append("majflt %d (process %d)" % (round(ev["mj"]), round(ev["Mj"])))
    for k, label in (("pswpin", "pswpin"), ("zram_reads", "zram reads"), ("vswap_reads", "vswap reads"),
                     ("vs_vram_reads", "vswap from VRAM"), ("vs_disk_reads", "vswap from disk"),
                     ("refault", "refaults"), ("allocstall", "allocstall"), ("direct", "direct scan"),
                     ("compact", "compact_stall"), ("cma", "cma allocs"), ("pswpout", "pswpout"),
                     ("kswapd", "kswapd scan")):
        if round(ev[k]):
            p.append("%s %d" % (label, round(ev[k])))
    if round(ev["hdd_kb"]):
        p.append("disk %d reads %d KiB" % (round(ev["hdd_reads"]), round(ev["hdd_kb"])))
    p.append("cpu %.0f ms (main user %.0f sys %.0f)" % (ev["cpu_ms"], ev["user_ms"], ev["sys_ms"]))
    if states:
        p.append("states " + " ".join("%s%d" % kv for kv in states.most_common()))
    if wchans:
        p.append("wchan " + ",".join("%s:%d" % kv for kv in wchans.most_common(3)))
    if calls:
        p.append("in " + ",".join("%s:%d" % kv for kv in calls.most_common(3)))
    if others:
        p.append(" ".join("%s %d" % (k, round(v)) for k, v in sorted(others.items()) if round(v)))
    if stacks:
        p.append("stack " + stacks[0][2].replace("|", "<")[:200])
    return "; ".join(p)


def align(rec, b, ft):
    """offset (recorder seconds - client seconds) and its spread, from the frames both know;
    falls back on bench.txt's t0 and the recorder's clock pairs"""
    ends = report.frame_ends(ft, b)
    diffs = sorted(r - c for r, c in zip(rec.frames, ends) if r is not None)
    if diffs:
        m = diffs[len(diffs) // 2]
        return m, diffs[-1 - len(diffs) // 20] - diffs[len(diffs) // 20], "frames"
    t0 = report.num(b.get("t0"))
    if t0 is not None and rec.clocks:
        return rec.real_to_rec(t0), None, "t0"
    return None, None, None


def analyze(d):
    """the stall analysis of run directory d, or None without a record"""
    d = Path(d)
    path = d / "stalls.rec"
    if not path.exists():
        return None
    rec = Rec(path)
    b = report.kv(d / "bench.txt")
    ft_path = d / "hud" / "frametime_(ms)"
    ft = report.floats(ft_path) if ft_path.exists() else []
    off, spread, how = align(rec, b, ft)
    res = {"rec": rec, "frames": len(ft), "rec_frames": len(rec.frames), "align": how, "offset": off,
           "spread": spread, "stalls": [], "window": Counter(), "load": None, "warmup": None}
    t0 = report.num(b.get("t0"))
    if t0 is not None and rec.clocks and off is not None:
        res["t0_offset"] = rec.real_to_rec(t0) - off    # the two alignments against each other
    segs = report.window(b)
    res["measured"] = bool(ft and segs and off is not None)
    if res["measured"]:
        ends = report.frame_ends(ft, b)
        for k, (e, v) in enumerate(zip(ends, ft)):
            if v <= STALL_MS or not any(s < e <= t for s, t in segs):
                continue
            # the recorder's own time of the frame's end when it saw it, else the client's
            end = rec.frames[k] if k < len(rec.frames) and rec.frames[k] is not None else e + off
            by, ev, states, wchans, calls, others, stacks = interval(rec, end - v / 1000, end)
            cause = max(by, key=by.get) if by else "unknown"
            # the causes add up to the frame's length (the samples cover it, up to rounding)
            tot = sum(by.values()) or 1
            by = {c: x / tot * v / 1000 for c, x in by.items()}
            res["stalls"].append({"at": end - off - v / 1000, "ms": v, "cause": cause, "by": by,
                                  "evidence": evidence(ev, states, wchans, calls, others, stacks)})
            for c, x in by.items():
                res["window"][c] += x
    if ft and off is not None and (wseg := report.warm_segment(b)):
        # the warm-up's stall frames (the first minute after the load), their causes summed
        res["warm"] = Counter()
        ends = report.frame_ends(ft, b)
        for k, (e, v) in enumerate(zip(ends, ft)):
            if v <= STALL_MS or not wseg[0] < e <= wseg[1]:
                continue
            end = rec.frames[k] if k < len(rec.frames) and rec.frames[k] is not None else e + off
            by = interval(rec, end - v / 1000, end)[0]
            tot = sum(by.values()) or 1
            for c, x in by.items():
                res["warm"][c] += x / tot * v / 1000
    le = report.num(b.get("load_end"))
    if le is not None and off is not None and rec.t:
        # the load: the client's start (or the recorder's, if later) until the load ended
        by, ev, states, wchans, calls, others, stacks = interval(rec, max(off, rec.t[0]), le + off)
        res["load"] = (le, by, evidence(ev, states, wchans, calls, others, stacks))
        # the warm-up: the load's end until the window's start (all of it, as the load)
        se = report.startup_end(b)
        if se is not None and se > le:
            by, ev, states, wchans, calls, others, stacks = interval(rec, le + off, se + off)
            res["warmup"] = (le, se, by, evidence(ev, states, wchans, calls, others, stacks))
    return res


def summary(d):
    """report.py's stall_*_s columns for run directory d ({} without a record)"""
    try:
        res = analyze(d)
    except (OSError, ValueError, KeyError, IndexError):
        return {}
    if not res:
        return {}
    out = {}
    if res["load"]:
        out["load_disk_s"] = round(res["load"][1].get("disk", 0), 1)
    if res.get("warmup"):
        out["warmup_disk_s"] = round(res["warmup"][2].get("disk", 0), 1)
    if "warm" in res:
        out["warm_disk_s"] = round(res["warm"].get("disk", 0), 1)
    if not res["measured"]:
        return out   # no frame times or no window: no stalls to split
    out.update({col: round(sum(res["window"].get(c, 0) for c in causes), 1) for col, causes in COLUMNS.items()})
    if res["stalls"]:
        out["stall_why"] = max(res["window"], key=res["window"].get)
    return out


def table(by, total=None):
    total = total if total is not None else sum(by.values())
    rows = []
    for c in CAUSES:
        if by.get(c, 0) >= 0.005:
            rows.append("    %-11s %7.2f s  %5.1f%%" % (c, by[c], 100 * by[c] / total if total else 0))
    return "\n".join(rows) if rows else "    (nothing)"


def show(d):
    res = analyze(d)
    print("== %s" % d)
    if res is None:
        print("  no stalls.rec (BENCH_STALLS=1 records one)")
        return
    rec = res["rec"]
    e, cost = rec.end, rec.cost
    if e:
        run_s = int(e.get("t", 0)) / 1e6 or 1
        own = (int(e.get("self_user_us", 0)) + int(e.get("self_sys_us", 0))) / 1e6
        print("  recorder: %s samples every %.0f ms, own CPU %.2f s (%.2f%% of one thread), late %s, "
              "largest gap %.0f ms, %s stacks" % (e.get("samples"), rec.interval * 1000, own,
                                                  100 * own / run_s, e.get("late"),
                                                  int(e.get("maxgap_us", 0)) / 1000, e.get("stacks")))
    if cost:
        print("  recorder cost per sample (us): " + " ".join("%s %s" % kv for kv in cost.items()))
    if res["offset"] is None:
        print("  no alignment: no HUD frame times and no t0 in bench.txt")
        return
    sp = " spread %.3f s" % res["spread"] if res["spread"] is not None else ""
    t0o = " (t0 says %+.3f s)" % res["t0_offset"] if "t0_offset" in res else ""
    print("  aligned by %s: %d HUD frames, %d seen by the recorder%s%s" % (
        res["align"], res["frames"], res["rec_frames"], sp, t0o))
    st = res["stalls"]
    print("  window: %d stalls, %.1f s" % (len(st), sum(x["ms"] for x in st) / 1000))
    print(table(res["window"]))
    if res.get("warm"):
        print("  warm-up (the first %g s after the load), stall frames: %.1f s" % (
            report.num(report.kv(Path(d) / "bench.txt").get("warmup")) or 0, sum(res["warm"].values())))
        print(table(res["warm"]))
    if res["load"]:
        le, by, ev = res["load"]
        print("  load (0 - %.1f s):" % le)
        print(table(by))
        print("    evidence: " + ev)
    if res.get("warmup"):
        a, z, by, ev = res["warmup"]
        print("  warm-up, all of it (%.1f - %.1f s):" % (a, z))
        print(table(by))
        print("    evidence: " + ev)
    if st:
        print("  longest stalls (at: seconds from the client's start):")
        for x in sorted(st, key=lambda x: -x["ms"])[:10]:
            mix = " ".join("%s %.0f" % (c, s * 1000) for c, s in sorted(x["by"].items(), key=lambda kv: -kv[1])
                           if s * 1000 >= 5)
            print("    %7.2f s %6.0f ms  %-10s [%s]\n        %s" % (x["at"], x["ms"], x["cause"], mix,
                                                                  x["evidence"]))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for a in sys.argv[1:]:
        show(a)
