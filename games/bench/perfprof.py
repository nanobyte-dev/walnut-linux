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
        --load: the start-up record (perf-load.data, docs/PROFILING.md "Start-up profiling") into
          perf-load.folded (the phase, load or warmup, as the outermost frame), perf-load-leaf.txt
          and perf-load-stacks.tsv (per phase, process, thread and stack: samples, period, every
          frame's symbol and DSO, for loadprof.py); the phases split at bench.txt's load_end on
          perf's clock (mono_t0 + load_end; without mono_t0 the first sample + load_end)
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
import bisect
import os
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


# the packages run.sh's versions file lists for every game (the graphics stack and the harness)
STACK_PKGS = {"mesa-libgallium", "ps3rsx-modules-ps3", "libspeq0", "libfluidsynth3", "libopenal1", "libsdl3-0",
              "ps3-gpu-tools"}


def is_kernel(dso):
    return dso.startswith("[kernel") or dso in ("[guest.kernel.kallsyms]",) or (
        dso.startswith("[") and dso.endswith("]") and dso not in ("[unknown]", "[vdso]", "[vsyscall]",
                                                                      "[heap]", "[stack]", "[anon]"))


def classify(dso, sym, exe, game_dirs=(), game_pkg=None, pkgs=None):
    """the class of one sample by its DSO (perf's name) and symbol"""
    if dso in ("[unknown]", "") or dso.startswith("//anon") or dso == "[anon]":
        return "unk"
    if dso.startswith("[ps3rsx") or dso.startswith("[drm"):   # the RSX driver and the DRM modules
        return "kgpu"
    if is_kernel(dso):
        # ppc64 ELFv1 kernel symbols are the dot names (.drm_ioctl); ps3rsx's are in [kernel.kallsyms]
        return "kgpu" if KGPU_SYM.match((sym or "").lstrip(".")) else "kernel"
    if exe and dso == exe:
        return "game"
    if MESA.search(dso):
        return "mesa"
    if DRM.search(dso):
        return "drm"
    if LIBC.search(dso):
        return "libc"
    if pkgs and game_pkg and pkgs.get(dso) in ((game_pkg,) if isinstance(game_pkg, str) else game_pkg):
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


class Fill:
    """Frames perf left [unknown] in the game process: a library mapped while the record was still
    disabled (ps3-bench.sh before ps3-gpu-tools 0.412 started perf with -D -1 and no tracking event,
    so libGLX_mesa and libgallium, dlopened during the load, got no mmap record). The DSO comes from
    the run's MEM=1 smaps (mem/mid.smaps, else start or end), the symbol from the library and its
    debug file (/usr/lib/debug/.build-id, by the build id in perf-buildids.txt or the library's
    own note) through hotpaths/elfsym.py, as games/bench/hotpaths/analyze.py does on the PC.
    PERFPROF_ROOT: where those files are (default /; on the PC a symbol root such as symroot.sh's).
    Without smaps (a run without MEM=1) the frames stay [unknown]."""

    def __init__(self, run_dir):
        d = Path(run_dir)
        self.root = os.environ.get("PERFPROF_ROOT", "").rstrip("/")
        tg = kv_line(d / "perf.targets")
        self.game = int(tg["game"]) if tg.get("game", "").isdigit() else None
        self.maps = []
        for lab in ("mid", "start", "end"):
            f = d / "mem" / (lab + ".smaps")
            if not f.exists():
                continue
            for line in f.read_text(errors="replace").splitlines():
                m = SMAPS_MAP.match(line)
                if m and "x" in m["perm"]:
                    self.maps.append((int(m["a"], 16), int(m["b"], 16), int(m["off"], 16), m["path"].strip()))
            if self.maps:
                break
        self.maps.sort()
        self.starts = [m[0] for m in self.maps]
        self.bids = {}
        f = d / "perf-buildids.txt"
        if f.exists():
            for line in f.read_text(errors="replace").splitlines():
                p = line.split()
                if len(p) == 2:
                    self.bids[p[1]] = p[0]
        self.tabs = {}
        self.cache = {}
        self.filled = Counter()   # samples whose leaf was filled, by DSO

    def __bool__(self):
        return bool(self.maps) and self.game is not None

    def symtab(self, path):
        if path not in self.tabs:
            tab = None
            try:
                sys.path.insert(0, str(Path(__file__).resolve().parent / "hotpaths"))
                import elfsym
                rt = self.root + path
                bid = self.bids.get(path) or elfsym.build_id(rt)
                dbg = "%s/usr/lib/debug/.build-id/%s/%s.debug" % (self.root, bid[:2], bid[2:]) if bid else None
                tab, el = elfsym.user_symtab(rt, dbg if dbg and os.path.exists(dbg) else None)
                tab.elf = el
            except Exception as ex:  # noqa: BLE001 (a file we can't read: the DSO name only)
                print("perfprof: no symbols for %s (%s)" % (path, ex), file=sys.stderr)
            self.tabs[path] = tab
        return self.tabs[path]

    def frame(self, ip, exact):
        """(sym, dso) for an address of the game process, None when no mapping holds it"""
        key = (ip, exact)
        if key in self.cache:
            return self.cache[key]
        r = None
        i = bisect.bisect_right(self.starts, ip) - 1
        if i >= 0 and ip < self.maps[i][1]:
            a, b, off, path = self.maps[i]
            tab = self.symtab(path) if path.startswith("/") else None
            sym = None
            if tab:   # a return address points after the call: look up the call
                sym = tab.lookup(tab.elf.file_off_to_vaddr((ip if exact else ip - 4) - a + off))
            r = (sym or "[unknown]", path)
        self.cache[key] = r
        return r

    def fix(self, pid, frames):
        """frames [(sym, dso, ip)], leaf first: the [unknown] ones of the game resolved where possible"""
        if pid != self.game or not any(f[1] == "[unknown]" for f in frames):
            return frames
        ks = [is_kernel(f[1]) or f[1].startswith("[ps3rsx") for f in frames]
        out = []
        for i, (sym, dso, ip) in enumerate(frames):
            if dso == "[unknown]" and ip:
                # the sampled address (the leaf, or the user address of a kernel sample) is exact,
                # the others are return addresses
                r = self.frame(int(ip, 16), i == 0 or (not ks[i] and ks[i - 1]))
                if r:
                    sym, dso = r
            out.append((sym, dso, ip))
        if frames[0][1] == "[unknown]" and out[0][1] != "[unknown]":
            self.filled[out[0][1]] += 1
        # ppc64 stores the link register as the second user entry; perf drops it by the leaf's CFI,
        # which it could not do for a leaf without a map: drop it when it repeats the next entry or
        # lands in the leaf's own function (the rule of hotpaths/analyze.py)
        if not ks[0] and frames[0][1] == "[unknown]" and len(out) > 2:
            l0, l1, l2 = out[0], out[1], out[2]
            if l1[2] == l2[2] or (l1[:2] == l0[:2] and l0[0] != "[unknown]"):
                del out[1]
        return out


SMAPS_MAP = re.compile(r"^(?P<a>[0-9a-f]+)-(?P<b>[0-9a-f]+) (?P<perm>\S+) (?P<off>[0-9a-f]+) \S+ \d+\s+(?P<path>/\S.*)$")


def collapse(lines, out_dir, sysmode=False, prefix="perf", split=None):
    """perf script lines -> perf.folded and perf-leaf.txt (perf-sys-leaf.txt with sysmode);
    prefix perf-load (the start-up record): <prefix>.folded, <prefix>-leaf.txt and, with split =
    (mono_t0 or None, load_end), the phases and <prefix>-stacks.tsv"""
    fill = None if sysmode or prefix != "perf" else Fill(out_dir)
    if fill is not None and not fill:
        fill = None
    boundary = None
    if split is not None and split[0] is not None and split[1] is not None:
        boundary = split[0] + split[1]
    rich = Counter()       # (phase, pid, tid, comm, frames) -> samples
    rich_p = Counter()     # the same -> period
    stacks = Counter()
    leaf = Counter()       # (pid, comm, dso, sym) -> samples
    period = Counter()     # (pid, comm, dso, sym) -> period sum
    comms = {}
    first = last = None
    n = 0
    cur = None             # [comm, pid, period, frames, tid, time]

    def flush():
        nonlocal n, boundary
        if cur is None:
            return
        comm, pid, per, frames, tid, t = cur
        n += 1
        if fill is not None:
            frames = fill.fix(pid, frames)
        if frames:
            sym, dso = frames[0][:2]
        else:
            sym, dso = "[unknown]", "[unknown]"
        key = (pid, comms.get(pid, comm), dso, sym)
        leaf[key] += 1
        period[key] += per
        if not sysmode:
            chain = [frame_name(f[0], f[1]) for f in reversed(frames)] or ["[unknown]"]
            root = [comm.replace(" ", "_").replace(";", ":")]
            if split is not None:
                if boundary is None and split[1] is not None:
                    boundary = t + split[1]   # no mono_t0: the first sample is the gate's opening
                phase = "load" if boundary is None or t < boundary else "warmup"
                root.insert(0, phase)
                fk = tuple((f[0] or "[unknown]", f[1] or "[unknown]") for f in reversed(frames))
                k2 = (phase, pid, tid, comm, fk)
                rich[k2] += 1
                rich_p[k2] += per
            stacks[";".join(root + chain)] += 1

    for line in lines:
        if line[:1] == "\t":      # a call chain frame: "\t    ip sym (dso)" (the fast path)
            if cur is not None:
                t = line.strip()
                i, j = t.find(" "), t.rfind(" (")
                if i > 0 and j >= i and t.endswith(")"):
                    cur[3].append((t[i + 1:j], t[j + 2:-1], t[:i]))
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
        cur = [m["comm"], pid, int(m["period"] or 1), [], int(m["tid"]), t]
        # without call chains the sampled ip, symbol and DSO follow on the same line
        rest = line.lstrip()[m.end():].strip()
        if rest and (fm := FRAME.match("\t" + rest)):
            cur[3].append((fm["sym"], fm["dso"], fm["ip"]))
    flush()
    out_dir = Path(out_dir)
    if not sysmode:
        with (out_dir / (prefix + ".folded")).open("w") as f:
            for s, c in sorted(stacks.items(), key=lambda x: -x[1]):
                f.write("%s %d\n" % (s, c))
    if split is not None:
        # loadprof.py's input: frames outermost first, each "symbol\x1fDSO", tab separated
        with (out_dir / (prefix + "-stacks.tsv")).open("w") as f:
            f.write("# boundary=%s load_end=%s\n" % ("%.6f" % boundary if boundary is not None else "",
                                                      split[1] if split[1] is not None else ""))
            f.write("# phase\tpid\ttid\tcomm\tsamples\tperiod\tframes...\n")
            for k, c in sorted(rich.items(), key=lambda x: -x[1]):
                phase, pid, tid, comm, fk = k
                f.write("%s\t%d\t%d\t%s\t%d\t%d\t%s\n" % (phase, pid, tid, comm.replace("\t", " "), c, rich_p[k],
                                                          "\t".join("%s\x1f%s" % (a.replace("\t", " "), b) for a, b in fk)))
    name = "perf-sys-leaf.txt" if sysmode else prefix + "-leaf.txt"
    with (out_dir / name).open("w") as f:
        f.write("# samples=%d first=%.6f last=%.6f\n" % (n, first or 0, last or 0))
        if fill is not None and fill.filled:
            f.write("# filled_from_smaps=%s\n" % ",".join("%s:%d" % (k.rsplit("/", 1)[-1], v)
                                                          for k, v in fill.filled.most_common()))
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
        if line.startswith("# filled_from_smaps="):
            head.update(kv_line_text(line[2:]))
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
    # the game's packages: the executable's, and the recipe command's that run.sh's versions file
    # names first (OpenArena: /usr/games/openarena from openarena runs ioquake3, its cgame and ui
    # libraries are openarena's)
    gpkgs = {pkgs.get(exe)} - {None}
    v = d / "versions"
    if v.exists():
        for line in v.read_text(errors="replace").splitlines():
            name = line.split("\t")[0].split(":")[0]
            if name and "\t" in line and name not in STACK_PKGS:
                gpkgs.add(name)
    return tg, exe, dirs, tuple(sorted(gpkgs)), pkgs


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
    if t["head"].get("filled_from_smaps"):
        print("   leaves perf had no map for, filled from the MEM=1 smaps: " + t["head"]["filled_from_smaps"])
    if t["cls"]["unk"] > 0.03 * n:
        print("   !! %.1f%% of the samples are in no DSO: perf missed maps (ps3-gpu-tools 0.412 or later records"
              " the load's mmaps; with MEM=1 perf-post.sh fills them from the smaps)" % (100 * t["cls"]["unk"] / n))
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
        sysmode, load = "--sys" in args, "--load" in args
        args = [a for a in args if a not in ("--sys", "--load")]
        if len(args) != 1:
            sys.exit(__doc__)
        if load:
            b = kv_line_text("")
            bp = Path(args[0]) / "bench.txt"
            if bp.exists():
                for line in bp.read_text(errors="replace").splitlines():
                    k, _, v = line.partition("=")
                    b[k] = v
            def num(x):
                try:
                    return float(x)
                except (TypeError, ValueError):
                    return None
            n = collapse(sys.stdin, args[0], prefix="perf-load", split=(num(b.get("mono_t0")), num(b.get("load_end"))))
            print("perfprof: %d samples" % n, file=sys.stderr)
            return
        n = collapse(sys.stdin, args[0], sysmode)
        print("perfprof: %d samples" % n, file=sys.stderr)
        return
    if not argv:
        sys.exit(__doc__)
    for d in argv:
        show(d)


if __name__ == "__main__":
    main(sys.argv[1:])
