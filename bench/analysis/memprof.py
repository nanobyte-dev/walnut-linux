#!/usr/bin/env python3
"""memprof.py RUN_DIR...: what the game's memory is, from a MEM=1 run (docs/PROFILING.md): the
snapshots client.sh takes at the window's start, middle and end in RUN_DIR/mem/ (LABEL.smaps,
.rollup, .status, .meminfo, .gpu (ps3rsx's buffers), .malloc (ps3-mallocstat.so), .info). Prints,
per snapshot, every class's Size/Rss/Pss/Swap/Anonymous/Private_Dirty (MiB), the mappings with the
most swap, glibc's malloc numbers and the game's GPU buffers.
  memprof.py --smaps FILE [EXE]   one smaps file classified (any process: cat /proc/PID/smaps)
Classes of mappings:
  game    the executable and what lives in its directory (or /usr/lib/<name>): its own libraries
  mesa    Mesa and the GL stack: libgallium, *_dri.so, GL/EGL/GLX/gbm/glapi, glvnd, libdrm*
  libs    every other shared library (code, data, relro)
  heap    [heap] (the main arena, brk) and glibc's other malloc arenas (64 MiB-aligned anonymous
          mappings followed by their unused reserve)
  anon    other anonymous memory: malloc's own mmaps (blocks >= its mmap threshold), the game's and
          the driver's mmaps, JIT code
  stack   [stack] and thread stacks (anonymous mappings right above a guard page)
  gpu     GPU buffers mapped by the CPU (/dev/dri/*): ps3rsx maps them VM_PFNMAP, so their Rss is 0
          and only Size says how much is mapped; the buffers themselves are in the .gpu census
  shm     shared memory: /dev/shm, SysV (X's MIT-SHM), memfd
  files   other mapped files (game data, fonts, locale archive, caches)
  other   [vdso], [vvar] and the rest
summary(d) gives report.py its columns at the middle snapshot (else the nearest one): mem_<class>_rss
and mem_<class>_swap (MiB) for every class, mem_rss, mem_swap, mem_anon (totals, MiB), mem_topswap
(the 5 mappings with the most swap, name=MiB), mem_grow (Rss+Swap at the end minus at the start,
MiB), mem_malloc_used / mem_malloc_free (MiB in use / free but kept by malloc, all arenas),
mem_arenas, mem_gpu_vram / mem_gpu_gart (MiB of the game's buffers, ps3rsx census), mem_smaps_ms
(how long reading smaps took: the game's mmap lock is held meanwhile)."""
import re
import sys
from pathlib import Path

CLASSES = ("game", "mesa", "libs", "heap", "anon", "stack", "gpu", "shm", "files", "other")
FIELDS = ("Size", "Rss", "Pss", "Swap", "Anonymous", "Private_Dirty")
HEAD = re.compile(r"^([0-9a-f]+)-([0-9a-f]+)\s+(\S{4})\s+([0-9a-f]+)\s+(\S+)\s+(\d+)\s*(.*)$")
MESA = re.compile(r"(^|/)(libgallium[^/]*|[^/]*_dri\.so[^/]*|libGLX_mesa[^/]*|libEGL_mesa[^/]*|libglapi[^/]*|"
                  r"libgbm[^/]*|libGL\.so[^/]*|libGLX\.so[^/]*|libGLdispatch[^/]*|libEGL\.so[^/]*|"
                  r"libOpenGL[^/]*|libGLESv[^/]*|libdrm[^/]*|libxatracker[^/]*)$")
LIB = re.compile(r"(\.so(\.[0-9.]+)?|/ld64\.so\.\d|/ld-linux[^/]*)( \(deleted\))?$")
ARENA = 64 << 20   # glibc's HEAP_MAX_SIZE on 64-bit: non-main arenas are aligned to it
GUARD = 64 << 10   # a thread stack's guard: one page (4 KiB on the PS3), at most this
STACK_MAX = 16 << 20   # and the stack itself (glibc's default: RLIMIT_STACK, 8 MiB)


def parse_smaps(text):
    """the mappings of an smaps file: dicts with start, end, perms, path and the kB fields"""
    maps = []
    cur = None
    for line in text.splitlines():
        m = HEAD.match(line)
        if m and not line.endswith(" kB"):
            cur = {"start": int(m[1], 16), "end": int(m[2], 16), "perms": m[3], "path": m[7].strip()}
            maps.append(cur)
            continue
        if cur is None:
            continue
        k, _, v = line.partition(":")
        v = v.strip()
        if v.endswith(" kB"):
            try:
                cur[k] = int(v[:-3])
            except ValueError:
                pass
    return maps


def game_dirs(exe):
    if not exe:
        return []
    d, base = exe.rsplit("/", 1)
    out = ["/usr/lib/" + base, "/usr/lib/games/" + base, "/usr/share/games/" + base]
    if d not in ("/usr/bin", "/bin", "/usr/games", "/usr/local/bin", "/usr/local/games", "/usr/sbin"):
        out.append(d)
    return out


def classify(maps, exe="", sps=()):
    """sets m['cls'] on every mapping; sps: the threads' stack pointers (LABEL.tasks), whose
    anonymous mappings are stacks whatever their neighbours"""
    dirs = game_dirs(exe)
    on_stack = set()
    for sp in sps:
        for i, m in enumerate(maps):
            if m["start"] <= sp < m["end"] and not m["path"]:
                on_stack.add(i)
                if i and not maps[i - 1]["path"] and maps[i - 1]["perms"].startswith("---") \
                        and maps[i - 1]["end"] == m["start"]:
                    on_stack.add(i - 1)
    for i, m in enumerate(maps):
        p = m["path"]
        prev = maps[i - 1] if i else None
        nxt = maps[i + 1] if i + 1 < len(maps) else None
        if i in on_stack:
            c = "stack"
        elif p.startswith("[anon: glibc: malloc"):         # CONFIG_ANON_VMA_NAME + glibc.mem.decorate_maps
            c = "heap" if "arena" in p else "anon"
        elif p.startswith("[anon: glibc: pthread stack") or p == "[stack]":
            c = "stack"
        elif p == "[heap]":
            c = "heap"
        elif p.startswith("[anon"):
            c = "anon"
        elif not p:
            c = "anon"
            if (m["perms"].startswith("rw") and prev is not None and not prev["path"] and prev["end"] == m["start"]
                    and prev["perms"].startswith("---") and prev["end"] - prev["start"] <= GUARD
                    and m["end"] - m["start"] <= STACK_MAX):
                c = "stack"                                 # a thread stack above its guard
            elif (m["start"] % ARENA == 0 and m["end"] - m["start"] == ARENA) or (
                    nxt is not None and not nxt["path"] and nxt["start"] == m["end"] and nxt["perms"].startswith("---")
                    and nxt["end"] % ARENA == 0 and nxt["end"] - ARENA >= m["start"]):
                # a malloc arena: 64 MiB-aligned, its unused reserve (---p) up to the next 64 MiB; the
                # kernel may have merged a neighbouring anonymous mapping into its start
                c = "heap"
            elif m["perms"].startswith("---") and m["end"] - m["start"] <= GUARD and nxt is not None and not nxt["path"] \
                    and nxt["start"] == m["end"] and nxt["perms"].startswith("rw") and nxt["end"] - nxt["start"] <= STACK_MAX:
                c = "stack"                                 # the guard page itself
            elif m["perms"].startswith("---") and prev is not None and prev.get("cls") == "heap" and prev["end"] == m["start"]:
                c = "heap"                                  # an arena's reserve
        elif p.startswith("/dev/dri/") or (p.startswith("anon_inode:") and "drm" in p) or p.startswith("/dev/ps3"):
            c = "gpu"
        elif p.startswith("/dev/shm/") or p.startswith("/SYSV") or p.startswith("/memfd:") or p.startswith("/dev/zero"):
            c = "shm"
        elif exe and (p == exe or p.split(" (deleted)")[0] == exe or any(p.startswith(g.rstrip("/") + "/") for g in dirs)):
            c = "game"
        elif MESA.search(p.split(" (deleted)")[0]):
            c = "mesa"
        elif LIB.search(p):
            c = "libs"
        elif p.startswith("/"):
            c = "files"
        else:
            c = "other"
        m["cls"] = c
    return maps


def name_of(m):
    p = m["path"]
    if p.startswith("/"):
        return p.rsplit("/", 1)[-1]
    if p:
        return p
    return "%s@%x(%dM)" % (m["cls"], m["start"], (m["end"] - m["start"]) >> 20)


def totals(maps):
    t = {c: dict.fromkeys(FIELDS, 0) for c in CLASSES}
    for m in maps:
        for f in FIELDS:
            t[m["cls"]][f] += m.get(f, 0)
    return t


def stack_pointers(path):
    """the stack pointers of the threads blocked in a system call: LABEL.tasks lines are
    "tid<TAB>comm<TAB>/proc/PID/task/TID/syscall" (number, 6 arguments, sp, pc; "running" or -1 else)"""
    out = []
    if path.exists():
        for line in path.read_text(errors="replace").splitlines():
            f = line.split("\t")[-1].split()
            if len(f) >= 9 and f[-2].startswith("0x"):
                out.append(int(f[-2], 16))
    return out


def kv_tokens(text):
    out = {}
    for tok in text.split():
        k, _, v = tok.partition("=")
        if _:
            out[k] = v
    return out


def snapshot(d, label):
    """one snapshot of run directory d: classes, top swap, malloc, gpu, status"""
    mem = Path(d) / "mem"
    sm = mem / f"{label}.smaps"
    if not sm.exists():
        return None
    info = kv_tokens((mem / f"{label}.info").read_text()) if (mem / f"{label}.info").exists() else {}
    exe = info.get("exe", "")
    sps = stack_pointers(mem / f"{label}.tasks")
    maps = classify(parse_smaps(sm.read_text(errors="replace")), exe, sps)
    s = {"label": label, "info": info, "maps": maps, "tot": totals(maps)}
    st = {}
    if (mem / f"{label}.status").exists():
        for line in (mem / f"{label}.status").read_text(errors="replace").splitlines():
            k, _, v = line.partition(":")
            v = v.strip()
            if v.endswith(" kB"):
                st[k] = int(v[:-3])
            elif k == "Threads":
                st[k] = int(v)
    s["status"] = st
    ml = mem / f"{label}.malloc"
    if ml.exists():
        text = ml.read_text(errors="replace")
        first = text.splitlines()[0] if text else ""
        mi = {k: int(v) for k, v in kv_tokens(first).items() if v.isdigit()} if first.startswith("mallinfo2") else {}
        mi["arenas"] = len(re.findall(r'<heap nr="\d+"', text))
        s["malloc"] = mi
    gp = mem / f"{label}.gpu"
    if gp.exists():
        comm = (info.get("comm") or "")[:15]
        g = {}
        for line in gp.read_text(errors="replace").splitlines():
            if m := re.match(r"^(.{1,16}?)\s+(\d+) handles, VRAM\s+(\d+) KiB, GART\s+(\d+) KiB", line):
                if comm and m[1].strip()[:15] == comm:
                    g["handles"] = g.get("handles", 0) + int(m[2])
                    g["vram"] = g.get("vram", 0) + int(m[3])
                    g["gart"] = g.get("gart", 0) + int(m[4])
            elif m := re.match(r"^GART backed by memory: (\d+) KiB", line):
                g["gart_backed"] = int(m[1])
        s["gpu"] = g
    return s


LABELS = ("start", "mid", "end")


def mb(kb):
    return round(kb / 1024, 1)


def summary(d):
    """report.py's mem_* columns for run directory d"""
    snaps = {lb: snapshot(d, lb) for lb in LABELS}
    snaps = {k: v for k, v in snaps.items() if v}
    if not snaps:
        return {}
    s = snaps.get("mid") or snaps.get("start") or snaps.get("end")
    r = {}
    t = s["tot"]
    for c in CLASSES:
        r[f"mem_{c}_rss"] = mb(t[c]["Rss"])
        r[f"mem_{c}_swap"] = mb(t[c]["Swap"])
    r["mem_rss"] = mb(sum(t[c]["Rss"] for c in CLASSES))
    r["mem_swap"] = mb(sum(t[c]["Swap"] for c in CLASSES))
    r["mem_anon"] = mb(sum(t[c]["Anonymous"] for c in CLASSES))
    top = sorted((m for m in s["maps"] if m.get("Swap", 0) > 0), key=lambda m: -m["Swap"])[:5]
    r["mem_topswap"] = ",".join("%s=%.1f" % (name_of(m), m["Swap"] / 1024) for m in top) or "-"
    if "start" in snaps and "end" in snaps:
        def used(x):
            return sum(x["tot"][c]["Rss"] + x["tot"][c]["Swap"] for c in CLASSES)
        r["mem_grow"] = mb(used(snaps["end"]) - used(snaps["start"]))
    mi = s.get("malloc")
    if mi and "uordblks" in mi:
        r["mem_malloc_used"] = round((mi["uordblks"] + mi.get("hblkhd", 0)) / 1048576, 1)
        r["mem_malloc_free"] = round(mi.get("fordblks", 0) / 1048576, 1)
        r["mem_arenas"] = mi.get("arenas")
    g = s.get("gpu")
    if g and "vram" in g:
        r["mem_gpu_vram"] = mb(g["vram"])
        r["mem_gpu_gart"] = mb(g["gart"])
    try:
        r["mem_smaps_ms"] = max(int(x["info"]["smaps_ms"]) for x in snaps.values() if x["info"].get("smaps_ms"))
    except ValueError:
        pass
    return r


def show_maps(maps, title, top=8):
    t = totals(maps)
    print("   %s" % title)
    print("     %-6s %8s %8s %8s %8s %8s %8s  (MiB)" % (("class",) + FIELDS))
    for c in CLASSES:
        if any(t[c].values()):
            print("     %-6s %s" % (c, " ".join("%8.1f" % (t[c][f] / 1024) for f in FIELDS)))
    print("     %-6s %s" % ("total", " ".join("%8.1f" % (sum(t[c][f] for c in CLASSES) / 1024) for f in FIELDS)))
    sw = sorted((m for m in maps if m.get("Swap", 0) > 0), key=lambda m: -m["Swap"])[:top]
    if sw:
        print("     most swap: " + ", ".join("%s %s %.1f" % (m["cls"], name_of(m), m["Swap"] / 1024) for m in sw))
    rs = sorted(maps, key=lambda m: -m.get("Rss", 0))[:top]
    print("     most Rss:  " + ", ".join("%s %s %.1f" % (m["cls"], name_of(m), m.get("Rss", 0) / 1024) for m in rs))


def show(d):
    print("== %s" % d)
    any_ = False
    for lb in LABELS:
        s = snapshot(d, lb)
        if not s:
            continue
        any_ = True
        i = s["info"]
        st = s["status"]
        show_maps(s["maps"], "%s at %s s (smaps read in %s ms): VmRSS %.1f, VmSwap %.1f, RssAnon %.1f, RssFile %.1f, "
                  "RssShmem %.1f MiB, %s threads" % (lb, i.get("t", "?"), i.get("smaps_ms", "?"),
                                                    st.get("VmRSS", 0) / 1024, st.get("VmSwap", 0) / 1024,
                                                    st.get("RssAnon", 0) / 1024, st.get("RssFile", 0) / 1024,
                                                    st.get("RssShmem", 0) / 1024, st.get("Threads", "?")))
        mi = s.get("malloc")
        if mi and "uordblks" in mi:
            print("     malloc: in use %.1f MiB (%.1f in mmapped blocks), free but kept %.1f MiB, %d arenas, "
                  "top pad %.1f MiB" % ((mi["uordblks"] + mi.get("hblkhd", 0)) / 1048576, mi.get("hblkhd", 0) / 1048576,
                                         mi.get("fordblks", 0) / 1048576, mi.get("arenas", 0), mi.get("keepcost", 0) / 1048576))
        g = s.get("gpu")
        if g:
            print("     ps3rsx: the game's buffers VRAM %.1f MiB, GART %.1f MiB (%s handles); GART backed by "
                  "memory %.1f MiB (all clients)" % (g.get("vram", 0) / 1024, g.get("gart", 0) / 1024,
                                                     g.get("handles", "?"), g.get("gart_backed", 0) / 1024))
    if not any_:
        print("   no mem/ snapshots")


def main(argv):
    if argv and argv[0] == "--smaps":
        if len(argv) < 2:
            sys.exit(__doc__)
        maps = classify(parse_smaps(Path(argv[1]).read_text(errors="replace")), argv[2] if len(argv) > 2 else "")
        show_maps(maps, argv[1], top=12)
        return
    if not argv:
        sys.exit(__doc__)
    for d in argv:
        show(d)


if __name__ == "__main__":
    main(sys.argv[1:])
