#!/usr/bin/env python3
"""gprefetch-check.py [DIR]: ps3-game-prefetch's game prefetch (docs/STALLS.md "Game prefetch") on
this host, with no PS3. DIR (default: a new directory under the repository's build/tmp, which is on
a disk file system: FIEMAP has to work) is removed at the end unless KEEP=1.
  1. the pieces: mincore vectors to ranges (holes under GAP filled), range unions, the program
     behind env/stdbuf/nice wrappers, which paths are a game's data directories
  2. real files: resident ranges after POSIX_FADV_DONTNEED and partial reads, FIEMAP extents
     (every byte placed, a range's address inside its extent), phys_at on made-up extents (holes,
     unknown extents)
  3. learn: a made-up session (a Sampler with seen paths and a working directory) learnt twice:
     the union of the ranges, first-seen times kept, sessions counted, a file read and closed found
     under the data directory, a file without pages dropped after 3 sessions, a changed file
     relearnt, another program (or an upgraded one) making the list stale, the command's key a
     symlink to the program's list
  4. the budget: residency-aware selection in first-use order, cut at the budget, sorted by disk
     address; game-fetch reading exactly what is missing; its stops (MemFree under the reserve,
     direct reclaim, swapping) with made-up /proc numbers; a stale list not read
  5. game-run end to end with a fake game (a Python script mapping one pack, reading another and
     a texture file): learn, the pages dropped, fetch (exec: the game keeps its pid), the pages back;
     exit statuses passed on"""
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.dont_write_bytecode = True   # no __pycache__ beside the packaged script
HERE = Path(__file__).resolve().parent
SCRIPT = HERE.parent / "files" / "libexec" / "ps3-game-prefetch"   # the program; its code:
sys.path.insert(0, str(SCRIPT.parent))
import ps3_game_prefetch as gp  # noqa: E402
PAGE = gp.PAGE
FAILS = []


def check(what, cond, got=None):
    print("%s  %s%s" % ("ok  " if cond else "FAIL", what, "" if cond else "  (got %r)" % (got,)))
    if not cond:
        FAILS.append(what)


def drop(*paths):
    for p in paths:
        fd = os.open(p, os.O_RDONLY)
        os.fdatasync(fd)
        os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED)
        os.close(fd)


def resident_pages(p):
    m = gp.Mapped(str(p))
    try:
        return sum(n for _, n in gp.runs_of(m.vec(), 1))
    finally:
        m.close()


def touch(p, start, n):
    """pages [start, start + n) of p read into the page cache"""
    with open(p, "rb") as f:
        f.seek(start * PAGE)
        f.read(n * PAGE)


def mkfile(p, mb):
    p.parent.mkdir(parents=True, exist_ok=True)
    with open(p, "wb") as f:
        f.write(os.urandom(int(mb * 1048576)))
        f.flush()
        os.fsync(f.fileno())
    return p


def pieces():
    vec = bytes([1, 1, 0, 0, 1, 0] + [0] * 40 + [1, 3])   # bit 0 only counts (3: other bits set)
    check("runs_of: holes under GAP filled", gp.runs_of(vec, 32) == [(0, 5), (46, 2)], gp.runs_of(vec, 32))
    check("runs_of: gap 1 keeps every run", gp.runs_of(vec, 1) == [(0, 2), (4, 1), (46, 2)], gp.runs_of(vec, 1))
    check("runs_of: nothing resident", gp.runs_of(bytes(10)) == [], gp.runs_of(bytes(10)))
    u = gp.union([(0, 10), (100, 5)], [(5, 10), (40, 2), (200, 1)], 32)
    check("union: overlaps and close ranges joined", u == [(0, 42), (100, 5), (200, 1)], u)
    check("union: far apart stay apart", gp.union([(0, 1)], [(100, 1)], 32) == [(0, 1), (100, 1)])
    for argv, want in ((["stdbuf", "-oL", "-eL", "/usr/games/cube2", "-w1"], "/usr/games/cube2"),
                       (["env", "A=1", "-u", "B", "nice", "-n", "10", "ionice", "-c", "3", "luanti"], "luanti"),
                       (["taskset", "0x1", "/usr/games/openarena", "+set"], "/usr/games/openarena"),
                       (["/usr/games/supertuxkart"], "/usr/games/supertuxkart"), (["env"], None)):
        check("command_of %s" % " ".join(argv), gp.command_of(argv) == want, gp.command_of(argv))
    h = "/home/u"
    for path, want in (("/usr/share/games/openarena/baseoa/pak0.pk3", ("/usr/share/games/openarena", True)),
                       ("/usr/share/luanti/textures/x.png", ("/usr/share/luanti", True)),
                       ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", None),
                       ("/usr/lib/powerpc64-linux-gnu/libgallium-25.so", None),
                       ("/usr/lib/games/cube2/cube2_client", ("/usr/lib/games/cube2", True)),
                       ("/home/u/.cube2/demo/bench.dmo", ("/home/u/.cube2", True)),
                       ("/home/u/.local/share/supertuxkart/config/x.xml", ("/home/u/.local/share/supertuxkart", True)),
                       ("/home/u/.cache/mesa_shader_cache_db/part0/mesa_cache.db", ("/home/u/.cache/mesa_shader_cache_db", True)),
                       ("/home/u/.cache/ps3-game-prefetch/x.list", None),
                       ("/home/u/notes.txt", None),
                       ("/var/tmp/world/map.sqlite", ("/var/tmp/world", False)),
                       ("/etc/ld.so.cache", None), ("/dev/shm/hud/fps", None)):
        check("data_root %s" % path, gp.data_root(path, h) == want, gp.data_root(path, h))


def real_files(w):
    f = mkfile(w / "real" / "a.bin", 4)
    drop(f)
    check("resident: nothing after DONTNEED", resident_pages(f) == 0, resident_pages(f))
    touch(f, 0, 3)
    touch(f, 100, 2)
    m = gp.Mapped(str(f))
    os.posix_fadvise(m.fd, 0, 0, os.POSIX_FADV_RANDOM)
    runs = m.resident(1)
    check("resident: the pages read (readahead may add some after them)", runs and runs[0][0] == 0 and
          runs[0][1] >= 3 and any(s <= 100 < s + n for s, n in runs), runs)
    check("missing: counted per range", m.missing(100, 2) == 0 and m.missing(500, 10) == 10,
          (m.missing(100, 2), m.missing(500, 10)))
    ext = gp.extents(m.fd)
    check("FIEMAP: extents on this file system", ext is not None and len(ext) >= 1, ext)
    if ext:
        covered = sum(ln for _, _, ln in ext)
        check("FIEMAP: the extents cover the file", covered >= m.size and ext[0][0] == 0, (covered, m.size))
        lg, ph, ln = ext[0]
        check("phys_at: an offset inside the first extent", gp.phys_at(ext, 8192, 0) == ph + 8192, gp.phys_at(ext, 8192, 0))
    m.close()
    fake = [(0, 1000000, 8192), (65536, None, 4096), (131072, 2000000, 4096)]
    check("phys_at: a hole after an extent: that extent's end", gp.phys_at(fake, 20000, 7) == 1000000 + 8192)
    check("phys_at: an unknown extent: the last known before it", gp.phys_at(fake, 65536 + 100, 7) == 1000000 + 8192)
    check("phys_at: the third extent", gp.phys_at(fake, 131072 + 4, 7) == 2000004)
    check("phys_at: nothing known: the fallback", gp.phys_at([(0, None, 4096)], 0, 7) == 7)
    sparse = w / "real" / "sparse.bin"
    with open(sparse, "wb") as fh:
        fh.seek(8 << 20)
        fh.write(b"x" * 4096)
        os.fsync(fh.fileno())
    fd = os.open(sparse, os.O_RDONLY)
    se = gp.extents(fd)
    os.close(fd)
    check("FIEMAP: a sparse file's only extent at 8 MiB", se is not None and len(se) == 1 and se[0][0] == 8 << 20, se)


def fake_sampler(paths, cwd, exe, t0=None):
    s = gp.Sampler([], t0)
    for i, p in enumerate(paths):
        s.seen[str(p)] = float(i)
    s.by_pid[1] = [set(map(str, paths)), str(cwd), exe]
    s.main = (1000, exe, [exe], str(cwd), 1)
    s.n = 3
    return s


def learning(w):
    data = w / "learn" / "share" / "games" / "fakegame"
    lists = w / "learn" / "lists"
    exe = mkfile(w / "learn" / "bin" / "fakegame", 0.1)
    p1, p2, tex, gone = (mkfile(data / n, mb) for n, mb in (("p1.pk3", 2), ("p2.pk3", 2), ("tex.png", 0.5), ("old.dat", 0.25)))
    drop(p1, p2, tex, gone, exe)
    touch(p1, 0, 10)
    touch(tex, 0, 128)   # read and closed: only the walk of the data directory finds it
    touch(gone, 0, 8)
    touch(exe, 0, 4)
    os.environ["HOME"] = str(w / "learn" / "home")
    s = fake_sampler([exe, p1], data, str(exe))
    path = gp.learn(str(lists), ["/usr/games/fakegame-wrapper"], s)
    g = gp.GameList.load(path)
    e = g.entries
    check("learn 1: the mapped pack, the program, the texture found under the data directory",
          {str(p1), str(exe), str(tex), str(gone)} <= set(e), sorted(e))
    check("learn 1: p2 (no page in memory) not listed", str(p2) not in e)
    check("learn 1: the pack's ranges from page 0", e[str(p1)].ranges[0][0] == 0 and e[str(p1)].pages() >= 10,
          e[str(p1)].ranges)
    check("learn 1: first-seen times: the program 0, the walked texture -1", e[str(exe)].t == 0 and e[str(tex)].t == -1,
          (e[str(exe)].t, e[str(tex)].t))
    check("learn 1: list under the program's key", Path(path).name == gp.key_path(str(lists), str(exe)).split("/")[-1])
    lk = Path(gp.key_path(str(lists), "/usr/games/fakegame-wrapper"))
    check("learn 1: the command's key links to it", lk.is_symlink() and os.readlink(lk) == Path(path).name)
    ph = [r[2] for r in e[str(p1)].ranges]
    check("learn 1: each range has a disk address", all(x > 0 for x in ph), ph)

    # session 2: p1 elsewhere, p2 now used, the texture changed, old.dat not in memory
    drop(p1, p2, tex, gone)
    touch(p1, 300, 5)
    touch(p2, 0, 4)
    with open(tex, "r+b") as f:
        f.write(b"changed")
        f.flush()
        os.fsync(f.fileno())
    drop(tex)
    touch(tex, 0, 2)
    s = fake_sampler([exe, p2, p1], data, str(exe))
    s.seen[str(p1)] = 5.0
    gp.learn(str(lists), ["/usr/games/fakegame-wrapper"], s)
    e = gp.GameList.load(path).entries
    r1 = [(a, b) for a, b, _ in e[str(p1)].ranges]
    check("learn 2: p1's ranges the union of both sessions", any(a == 0 for a, _ in r1) and any(a <= 300 < a + b for a, b in r1), r1)
    check("learn 2: p1 seen in 2 sessions, first seen at 1 s (the earlier)", e[str(p1)].sessions == 2 and e[str(p1)].t == 1.0,
          (e[str(p1)].sessions, e[str(p1)].t))
    check("learn 2: p2 new", str(p2) in e and e[str(p2)].sessions == 1)
    check("learn 2: the changed texture relearnt (its new ranges only)", e[str(tex)].sessions == 1 and
          e[str(tex)].pages() < 128, (e[str(tex)].sessions, e[str(tex)].pages()))
    check("learn 2: old.dat kept, one miss", str(gone) in e and e[str(gone)].misses == 1, e.get(str(gone)) and e[str(gone)].misses)
    for _ in range(2):
        drop(gone)
        gp.learn(str(lists), ["/usr/games/fakegame-wrapper"], fake_sampler([exe, p1], data, str(exe)))
    e = gp.GameList.load(path).entries
    check("learn 4: old.dat dropped after 3 sessions without a page in memory", str(gone) not in e, sorted(e))
    gl = gp.GameList.load(path)
    check("learn 4: 4 sessions in the head", gl.head.get("sessions") == "4", gl.head)

    # the program upgraded: the list is stale for the fetch and starts anew
    with open(exe, "ab") as f:
        f.write(b"v2")
    _, g = gp.find_list(str(lists), ["/usr/games/fakegame-wrapper"])
    check("stale: an upgraded program's list isn't read", gp.game_fetch(str(lists), ["/usr/games/fakegame-wrapper"]) is None)
    touch(p1, 0, 2)
    gp.learn(str(lists), ["/usr/games/fakegame-wrapper"], fake_sampler([exe, p1], data, str(exe)))
    gl = gp.GameList.load(path)
    check("stale: learnt anew (1 session)", gl.head.get("sessions") == "1" and
          all(x.sessions == 1 for x in gl.entries.values()), gl.head)
    return lists, exe, (p1, p2, tex)


def budget(w, lists, exe, files):
    p1, p2, tex = files
    a, b = mkfile(w / "budget" / "a.bin", 1), mkfile(w / "budget" / "b.bin", 1)
    st = {p: os.stat(p) for p in (a, b)}
    g = gp.GameList("k")
    g.entries = {str(a): gp.Entry(str(a), 5.0, 1, 0, st[a].st_size, st[a].st_mtime_ns, [(0, 256, 900000)]),
                 str(b): gp.Entry(str(b), 1.0, 1, 0, st[b].st_size, st[b].st_mtime_ns, [(0, 128, 100), (200, 56, 800000)])}
    drop(a, b)
    sel, miss, have, looked = gp.select(g, 300)
    check("select: the budget filled in first-use order (b, then a cut)", miss == 300 and
          [(x[1].path, x[2], x[3]) for x in sel] == [(str(b), 0, 128), (str(b), 200, 56), (str(a), 0, 116)],
          [(x[1].path, x[2], x[3]) for x in sel])
    check("select: sorted by disk address", [x[0] for x in sel] == sorted(x[0] for x in sel))
    touch(b, 0, 128)
    sel, miss, have, looked = gp.select(g, 300)
    check("select: pages in memory cost nothing (b's first range skipped, a whole)", have >= 128 and
          [(x[1].path, x[2]) for x in sel] == [(str(b), 200), (str(a), 0)] or [(x[1].path, x[2]) for x in sel] == [(str(a), 0), (str(b), 200)],
          ([(x[1].path, x[2], x[3]) for x in sel], have))
    # game-fetch with made-up /proc numbers
    gl = gp.GameList("k")
    gl.head = {"key": "k", "exe": str(a), "exe_id": gp.ident(str(a)), "page": str(PAGE)}
    gl.entries = g.entries
    d = w / "budget" / "lists"
    d.mkdir()
    gl.save(gp.key_path(str(d), "k"))
    real_mi, real_vm = gp.meminfo, gp.vmstat
    try:
        gp.meminfo = lambda: {"MemFree": 400 << 20, "MemAvailable": 400 << 20}
        gp.vmstat = lambda: (0, 0)
        drop(a, b)
        r = gp.game_fetch(str(d), ["k"])
        check("game-fetch: reads all of it (256 + 184 pages)", r and r["read"] == 440 and r["stop"] is None, r)
        check("game-fetch: the pages are in memory", resident_pages(a) == 256 and resident_pages(b) >= 184,
              (resident_pages(a), resident_pages(b)))
        r = gp.game_fetch(str(d), ["k"])
        check("game-fetch again: nothing to read", r and r["read"] == 0, r)
        os.environ["PS3_GAME_PREFETCH_MB"] = "0.5"
        drop(a, b)
        r = gp.game_fetch(str(d), ["k"])
        check("game-fetch: PS3_GAME_PREFETCH_MB caps it (128 pages)", r and r["read"] == 128, r)
        del os.environ["PS3_GAME_PREFETCH_MB"]
        calls = [0]

        def falling():   # MemFree falls under the 48 MB reserve after a few chunks
            calls[0] += 1
            return {"MemFree": (300 if calls[0] < 4 else 40) << 20, "MemAvailable": 300 << 20}
        gp.meminfo = falling
        drop(a, b)
        r = gp.game_fetch(str(d), ["k"])
        check("game-fetch: stops when MemFree falls under the reserve", r and r["stop"] and r["stop"].startswith("memory")
              and 0 < r["read"] < 440, r)
        gp.meminfo = lambda: {"MemFree": 40 << 20, "MemAvailable": 300 << 20}
        r = gp.game_fetch(str(d), ["k"])
        check("game-fetch: no room under the reserve: nothing read", r and r["read"] == 0 and r["stop"] == "room", r)
        gp.meminfo = lambda: {"MemFree": 400 << 20, "MemAvailable": 400 << 20}
        n = [0]

        def reclaim():
            n[0] += 1
            return (0, 0 if n[0] < 2 else 5)
        gp.vmstat = reclaim
        drop(a, b)
        r = gp.game_fetch(str(d), ["k"])
        check("game-fetch: stops on direct reclaim", r and r["stop"] == "direct reclaim", r)
        n[0] = 0
        gp.vmstat = lambda: (n.__setitem__(0, n[0] + 1) or (0 if n[0] < 2 else 100, 0))
        drop(a, b)
        r = gp.game_fetch(str(d), ["k"])
        check("game-fetch: stops on swapping", r and r["stop"] == "swapping", r)
        gp.vmstat = lambda: (0, 0)
        drop(a, b)
        r = gp.game_fetch(str(d), ["k"], alive=lambda: False)
        check("game-fetch: stops when the game is gone", r and r["stop"] == "game gone" and r["read"] == 0, r)
    finally:
        gp.meminfo, gp.vmstat = real_mi, real_vm


GAME = r'''
import mmap, os, sys, time
os.chdir(sys.argv[1])
f = open("data/pack1.pk3", "rb"); m = mmap.mmap(f.fileno(), 0, prot=mmap.PROT_READ)
s = sum(m[i] for i in range(0, 2 << 20, 4096))
with open("data/tex.png", "rb") as g: g.read()
h = open("data/pack2.pk3", "rb"); h.seek(1 << 20); h.read(512 << 10)
print("pid", os.getpid(), flush=True)
time.sleep(float(sys.argv[2]))
sys.exit(int(sys.argv[3]))
'''


def end_to_end(w):
    root = w / "e2e"
    data = root / "share" / "games" / "fake" / "data"
    p1, p2, tex = mkfile(data / "pack1.pk3", 4), mkfile(data / "pack2.pk3", 4), mkfile(data / "tex.png", 1)
    game = root / "fakegame.py"
    game.write_text(GAME)
    lists = root / "lists"
    env = dict(os.environ, PS3_GAME_PREFETCH_DIR=str(lists))
    env.pop("PS3_GAME_PREFETCH_GAME", None)
    drop(p1, p2, tex)
    cmd = [str(SCRIPT), "game-run", "--mode", "learn", "--key", "/usr/games/fake", "--",
           sys.executable, "-I", str(game), str(root / "share" / "games" / "fake"), "1.5", "3"]
    p = subprocess.run(cmd, env=env, capture_output=True, text=True)
    check("game-run learn: the game's exit status (3)", p.returncode == 3, (p.returncode, p.stderr[-400:]))
    check("game-run learn: game-learn line", "game-learn" in p.stderr, p.stderr[-400:])
    _, g = gp.find_list(str(lists), ["/usr/games/fake"])
    check("game-run learn: list found through the --key link", g is not None and {str(p1), str(p2), str(tex)} <= set(g.entries),
          sorted(g.entries) if g else None)
    drop(p1, p2, tex)
    before = (resident_pages(p1), resident_pages(p2), resident_pages(tex))
    cmd = [str(SCRIPT), "game-run", "--mode", "fetch", "--key", "/usr/games/fake", "--",
           "sh", "-c", 'echo "pid $$"; sleep 1; exit 5']
    t = time.monotonic()
    pr = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    out, err = pr.communicate()
    check("game-run fetch: exec: the game has game-run's pid, its exit status (5)",
          pr.returncode == 5 and out.strip() == "pid %d" % pr.pid, (pr.returncode, out, pr.pid))
    check("game-run fetch: game-fetch line, nothing learnt", "game-fetch" in err and "game-learn" not in err, err[-400:])
    time.sleep(0.3)
    after = (resident_pages(p1), resident_pages(p2), resident_pages(tex))
    check("game-run fetch: the pages back (pack1 2 MB, pack2 0.5 MB, the texture)", before == (0, 0, 0) and
          after[0] >= 512 and after[1] >= 128 and after[2] >= 256, (before, after))
    check("game-run fetch: quick (%.1f s)" % (time.monotonic() - t), time.monotonic() - t < 5)
    p = subprocess.run([str(SCRIPT), "game-show", "--dir", str(lists), "/usr/games/fake"], capture_output=True, text=True)
    check("game-show", p.returncode == 0 and "pack1.pk3" in p.stdout, p.stdout[-300:])
    p = subprocess.run([str(SCRIPT), "game-list", "--dir", str(lists)], capture_output=True, text=True)
    check("game-list: the list and the link", p.returncode == 0 and "/usr/games/fake ->" in p.stdout, p.stdout)
    p = subprocess.run([str(SCRIPT), "game-forget", "--dir", str(lists), "/usr/games/fake"], capture_output=True, text=True)
    check("game-forget: list and link gone", p.returncode == 0 and not any(lists.iterdir()), list(lists.iterdir()))
    p = subprocess.run([str(SCRIPT), "game-run", "--mode", "fetch", "--", "true"], env=env, capture_output=True, text=True)
    check("game-run without a list: the game runs anyway", p.returncode == 0 and "nothing learnt yet" in p.stderr, p.stderr)


def main():
    base = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    if base is None:
        b = HERE.parents[4] / "build" / "tmp"
        b.mkdir(parents=True, exist_ok=True)
        base = Path(tempfile.mkdtemp(prefix="gprefetch-check-", dir=b))
    base.mkdir(parents=True, exist_ok=True)
    home = os.environ.get("HOME")
    try:
        pieces()
        real_files(base)
        lists, exe, files = learning(base)
        os.environ["HOME"] = home or "/"
        budget(base, lists, exe, files)
        end_to_end(base)
    finally:
        if home is not None:
            os.environ["HOME"] = home
        if os.environ.get("KEEP") != "1":
            shutil.rmtree(base, ignore_errors=True)
    print("\n%d check(s) failed: %s" % (len(FAILS), ", ".join(FAILS)) if FAILS else "\nall checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
