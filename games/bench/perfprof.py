#!/usr/bin/env python3
"""perfprof.py: where the game's CPU goes, from a PERF=1 run (docs/PROFILING.md).
  perf script -F comm,pid,tid,time,period,ip,sym,dso | perfprof.py collapse [--sys] RUN_DIR
        reads perf script's samples (with call chains) and writes into RUN_DIR
          perf.folded     one line per distinct stack: "thread;outer;...;leaf COUNT" (flamegraph.pl,
                          inferno, speedscope); kernel frames end in _[k], frames without a symbol
                          are [their DSO]
          perf-leaf.txt   samples by process, DSO and symbol of the sampled instruction (TSV), the
                          numbers report.py's perf_* columns come from
        --sys: the system-wide record (perf-sys.data, no call graphs) into perf-sys-leaf.txt
  perfprof.py RUN_DIR...   the table per run: the game's samples by class and its top symbols
summary(d) gives report.py its columns (percent of the game process's samples, all its threads):
  perf_game   the game's own code: its executable, the libraries in its package or directory
  perf_mesa   Mesa: libgallium, the DRI and GL/EGL/GLX/gbm libraries, glvnd's dispatch
  perf_drm    libdrm and libdrm_nouveau (the pushbuf, fences, buffer objects)
  perf_kgpu   the kernel for the GPU: ps3rsx (the [ps3rsx] module) and drm_/dma_fence/dma_resv/
              ttm_/nouveau functions in the kernel image
  perf_kernel the rest of the kernel (system calls, page faults, reclaim and zram run by the game)
  perf_libc   libc, the dynamic linker, libm (memcpy/memset, malloc, string functions)
  perf_other  every other library (SDL, OpenAL, ALSA, X, LuaJIT, zlib, ...)
  perf_unk    samples perf could not place in any DSO ([unknown]: a map it never read)
  perf_xorg, perf_kswapd   samples of Xorg and kswapd0 in the same window, as percent of the
              game's samples (perf records them alongside: X's work for the game, background reclaim)
  perf_n      the game's samples; perf_self perf's own CPU in the window, % of one thread"""
import re
import sys
from collections import Counter
from pathlib import Path

CLASSES = ("game", "mesa", "drm", "kgpu", "kernel", "libc", "other", "unk")
MESA = re.compile(r"(^|/)(libgallium[^/]*|[^/]*_dri\.so[^/]*|libGLX_mesa[^/]*|libEGL_mesa[^/]*|libglapi[^/]*|"
                  r"libgbm[^/]*|libGL\.so[^/]*|libGLX\.so[^/]*|libGLdispatch[^/]*|libEGL\.so[^/]*|"
                  r"libOpenGL[^/]*|libGLESv[^/]*|libxatracker[^/]*)$")
DRM = re.compile(r"(^|/)libdrm[^/]*$")
LIBC = re.compile(r"(^|/)(libc\.so[^/]*|libc-[^/]*|ld64\.so[^/]*|ld-linux[^/]*|libm\.so[^/]*|libpthread[^/]*|"
                  r"libdl\.so[^/]*|librt\.so[^/]*)$")
KGPU_SYM = re.compile(r"^(ps3rsx|drm_|__drm|dma_fence|dma_resv|ttm_|nouveau|gem_|ps3gpu|ps3vswap)")


def is_kernel(dso):
    return dso.startswith("[kernel") or dso in ("[guest.kernel.kallsyms]",) or (
        dso.startswith("[") and dso.endswith("]") and dso not in ("[unknown]", "[vdso]", "[vsyscall]",
                                                                      "[heap]", "[stack]", "[anon]"))


def classify(dso, sym, exe, game_dirs=(), game_pkg=None, pkgs=None):
    """the class of one sample by its DSO (perf's name) and symbol"""
    if dso in ("[unknown]", "") or dso.startswith("//anon") or dso == "[anon]":
        return "unk"
    if dso == "[ps3rsx]" or dso.startswith("[ps3rsx"):
        return "kgpu"
    if is_kernel(dso):
        return "kgpu" if KGPU_SYM.match(sym or "") else "kernel"
    if exe and dso == exe:
        return "game"
    if MESA.search(dso):
        return "mesa"
    if DRM.search(dso):
        return "drm"
    if LIBC.search(dso):
        return "libc"
    if pkgs and game_pkg and pkgs.get(dso) == game_pkg:
        return "game"
    if any(dso.startswith(g.rstrip("/") + "/") for g in game_dirs):
        return "game"
    return "other"


# --- collapse ---------------------------------------------------------------------------------
HEAD = re.compile(r"^(?P<comm>\S.*?)\s+(?P<pid>-?\d+)/(?P<tid>-?\d+)\s+(?:\[\d+\]\s+)?(?P<time>[\d.]+):\s+(?P<period>\d+)?")
FRAME = re.compile(r"^\s+(?P<ip>[0-9a-f]+)\s+(?P<sym>.*?)\s+\((?P<dso>[^()]*(?:\([^()]*\))?[^()]*)\)\s*$")


def frame_name(sym, dso):
    k = is_kernel(dso) or dso.startswith("[ps3rsx")
    if not sym or sym == "[unknown]":
        name = "[%s]" % (dso.rsplit("/", 1)[-1] if dso else "unknown")
        name = name.replace("[[", "[").replace("]]", "]")
    else:
        name = sym.replace(";", ":").replace(" ", "_")
    return name + ("_[k]" if k else "")


def collapse(lines, out_dir, sysmode=False):
    """perf script lines -> perf.folded and perf-leaf.txt (perf-sys-leaf.txt with sysmode)"""
    stacks = Counter()
    leaf = Counter()       # (pid, comm, dso, sym) -> samples
    period = Counter()     # (pid, comm, dso, sym) -> period sum
    comms = {}
    first = last = None
    n = 0
    cur = None             # [comm, pid, period, frames]

    def flush():
        nonlocal n
        if cur is None:
            return
        comm, pid, per, frames = cur
        n += 1
        if frames:
            sym, dso = frames[0]
        else:
            sym, dso = "[unknown]", "[unknown]"
        key = (pid, comms.get(pid, comm), dso, sym)
        leaf[key] += 1
        period[key] += per
        if not sysmode:
            chain = [frame_name(s, d) for s, d in reversed(frames)] or ["[unknown]"]
            stacks[";".join([comm.replace(" ", "_").replace(";", ":")] + chain)] += 1

    for line in lines:
        if line[:1] == "\t":      # a call chain frame: "\t    ip sym (dso)" (the fast path)
            if cur is not None:
                t = line.strip()
                i, j = t.find(" "), t.rfind(" (")
                if i > 0 and j >= i and t.endswith(")"):
                    cur[3].append((t[i + 1:j], t[j + 2:-1]))
            continue
        line = line.rstrip("\n")
        if not line.strip():
            continue
        # a sample's first line (comm right-aligned when there are no call chains)
        m = HEAD.match(line.lstrip())
        if not m:
            continue
        flush()
        pid = int(m["pid"])
        if m["tid"] == m["pid"]:
            comms[pid] = m["comm"]
        t = float(m["time"])
        first = t if first is None else first
        last = t
        cur = [m["comm"], pid, int(m["period"] or 1), []]
        # without call chains the sampled ip, symbol and DSO follow on the same line
        rest = line.lstrip()[m.end():].strip()
        if rest and (fm := FRAME.match("\t" + rest)):
            cur[3].append((fm["sym"], fm["dso"]))
    flush()
    out_dir = Path(out_dir)
    if not sysmode:
        with (out_dir / "perf.folded").open("w") as f:
            for s, c in sorted(stacks.items(), key=lambda x: -x[1]):
                f.write("%s %d\n" % (s, c))
    name = "perf-sys-leaf.txt" if sysmode else "perf-leaf.txt"
    with (out_dir / name).open("w") as f:
        f.write("# samples=%d first=%.6f last=%.6f\n" % (n, first or 0, last or 0))
        f.write("# samples\tperiod\tpid\tcomm\tdso\tsym\n")
        for k, c in sorted(leaf.items(), key=lambda x: -x[1]):
            pid, comm, dso, sym = k
            f.write("%d\t%d\t%d\t%s\t%s\t%s\n" % (c, period[k], pid, comm, dso, sym))
    return n


# --- reading a run ----------------------------------------------------------------------------
def kv_line(path):
    return kv_line_text(path.read_text(errors="replace")) if path.exists() else {}


def read_leaf(path):
    head, rows = {}, []
    if not path.exists():
        return head, rows
    for line in path.read_text(errors="replace").splitlines():
        if line.startswith("# samples="):
            head = kv_line_text(line[2:])
            continue
        if line.startswith("#") or not line.strip():
            continue
        f = line.split("\t")
        if len(f) < 6:
            continue
        rows.append((int(f[0]), int(f[1]), int(f[2]), f[3], f[4], f[5]))
    return head, rows


def kv_line_text(text):
    out = {}
    for tok in text.split():
        k, _, v = tok.partition("=")
        out[k] = v
    return out


def pkgs_of(d):
    """perf-pkgs.txt (perf-post.sh: dpkg -S of every DSO with samples): path -> package"""
    out = {}
    p = d / "perf-pkgs.txt"
    if p.exists():
        for line in p.read_text(errors="replace").splitlines():
            path, _, pkg = line.partition("\t")
            if pkg:
                out[path] = pkg
    return out


def context(d):
    d = Path(d)
    tg = kv_line(d / "perf.targets")
    exe = (d / "perf.exe").read_text().strip() if (d / "perf.exe").exists() else ""
    pkgs = pkgs_of(d)
    dirs = [exe.rsplit("/", 1)[0]] if exe and exe.count("/") > 2 else []
    # a game under /usr/games: its libraries in /usr/lib/<name>, /usr/lib/games/<name>
    if exe:
        base = exe.rsplit("/", 1)[-1]
        dirs += ["/usr/lib/" + base, "/usr/lib/games/" + base, "/usr/share/games/" + base]
    return tg, exe, dirs, pkgs.get(exe), pkgs


def table(d):
    """per class: game samples and percent; plus the other targets' samples, the top symbols"""
    d = Path(d)
    head, rows = read_leaf(d / "perf-leaf.txt")
    if not rows:
        return None
    tg, exe, dirs, gpkg, pkgs = context(d)
    game = int(tg["game"]) if tg.get("game", "").isdigit() else None
    if game is None:  # no targets file: the pid with the most samples
        c = Counter()
        for s, _, pid, *_ in rows:
            c[pid] += s
        game = c.most_common(1)[0][0]
    cls = Counter()
    syms = Counter()
    dsos = Counter()
    other = Counter()      # the other targets' samples, by name (perf.targets: xorg, kswapd0)
    names = {int(v): k for k, v in tg.items() if k in ("xorg", "kswapd0") and v.isdigit()}
    for s, _, pid, comm, dso, sym in rows:
        if pid == game:
            k = classify(dso, sym, exe, dirs, gpkg, pkgs)
            cls[k] += s
            syms[(k, dso.rsplit("/", 1)[-1], sym)] += s
            dsos[(k, dso)] += s
        else:
            other[names.get(pid, comm)] += s
    n = sum(cls.values())
    return {"n": n, "cls": cls, "syms": syms, "dsos": dsos, "other": other, "exe": exe,
            "game": game, "head": head, "tg": tg}


def summary(d):
    """report.py's perf_* columns for run directory d (needs perf-leaf.txt: perf-post.sh)"""
    t = table(d)
    if not t or not t["n"]:
        return {}
    n = t["n"]
    r = {"perf_n": n}
    for c in CLASSES:
        r["perf_" + c] = round(100 * t["cls"][c] / n, 1)
    for col, key in (("xorg", "xorg"), ("kswapd", "kswapd0")):
        if t["tg"].get(key):
            r["perf_" + col] = round(100 * t["other"].get(key, 0) / n, 1)
    # perf's own CPU over the window (client.sh): % of one thread
    b = {}
    bp = Path(d) / "bench.txt"
    if bp.exists():
        for line in bp.read_text(errors="replace").splitlines():
            k, _, v = line.partition("=")
            b[k] = v
    try:
        w = [float(x) for x in b.get("perf_win", "").split()]
        on = sum(w[i + 1] - w[i] for i in range(0, len(w) - 1, 2))
        if on > 0 and b.get("perf_cpu"):
            r["perf_self"] = round(100 * float(b["perf_cpu"]) / on, 2)
    except ValueError:
        pass
    s = sys_summary(d)
    r.update(s)
    return r


SYS = (("idle", re.compile(r"^swapper")), ("xorg", re.compile(r"^Xorg$")), ("kswapd", re.compile(r"^kswapd")),
       ("kworker", re.compile(r"^kworker")), ("vswap", re.compile(r"ps3vswap|zram")))


def sys_table(d):
    head, rows = read_leaf(Path(d) / "perf-sys-leaf.txt")
    if not rows:
        return None
    tg = kv_line(Path(d) / "perf.targets")
    game = int(tg["game"]) if tg.get("game", "").isdigit() else None
    c = Counter()
    for s, _, pid, comm, dso, sym in rows:
        if pid == game:
            c["game"] += s
            continue
        for name, rx in SYS:
            if rx.search(comm):
                c[name] += s
                break
        else:
            c["other"] += s
    return c


def sys_summary(d):
    c = sys_table(d)
    if not c:
        return {}
    n = sum(c.values())
    return {"perf_sys_" + k: round(100 * c[k] / n, 1) for k in ("game", "idle", "xorg", "kswapd", "kworker",
                                                                 "vswap", "other")}


def show(d, top=25):
    t = table(d)
    print("== %s" % d)
    if not t:
        print("   no perf-leaf.txt (run perf-post.sh on it)")
        return
    n = t["n"] or 1
    print("   game pid %s, %s, %d samples (all its threads) in the window" % (t["game"], t["exe"] or "?", t["n"]))
    print("   " + "  ".join("%s %.1f%%" % (c, 100 * t["cls"][c] / n) for c in CLASSES))
    if t["other"]:
        print("   recorded alongside (samples, % of the game's): " +
              ", ".join("%s %d (%.1f%%)" % (k, v, 100 * v / n) for k, v in t["other"].most_common(6)))
    print("   top DSOs:")
    for (k, dso), s in t["dsos"].most_common(10):
        print("     %5.1f%%  %-6s %s" % (100 * s / n, k, dso))
    print("   top symbols (self):")
    for (k, dso, sym), s in t["syms"].most_common(top):
        print("     %5.1f%%  %-6s %-28s %s" % (100 * s / n, k, dso[:28], sym))
    c = sys_table(d)
    if c:
        m = sum(c.values()) or 1
        print("   system-wide (perf-sys.data, % of both CPUs' samples): " +
              "  ".join("%s %.1f%%" % (k, 100 * v / m) for k, v in c.most_common()))


def main(argv):
    if argv and argv[0] == "collapse":
        args = argv[1:]
        sysmode = "--sys" in args
        args = [a for a in args if a != "--sys"]
        if len(args) != 1:
            sys.exit(__doc__)
        n = collapse(sys.stdin, args[0], sysmode)
        print("perfprof: %d samples" % n, file=sys.stderr)
        return
    if not argv:
        sys.exit(__doc__)
    for d in argv:
        show(d)


if __name__ == "__main__":
    main(sys.argv[1:])
