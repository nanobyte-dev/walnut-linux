# ps3_game_prefetch.py: ps3-game-prefetch's code (the program beside it imports it, so its byte code
# comes from the cache postinst makes with py3compile; compiled at every start it would cost
# the game watcher about 5 MB more memory).
# ps3-game-prefetch record LIST [--file PATH]... PID... | fetch LIST (ps3-desktop; ps3-game-guard
# runs it): the desktop's files back into the page cache when game mode ends, in one pass over the
# disk. Game mode pushes the frozen desktop's file pages out (and the game evicts the rest); left
# to themselves the thawed programs fault them back one at a time, random reads from the PS3's
# USB hard disk: about 20 s before the desktop answers again.
#   record  before the desktop is frozen: the files the processes PID... have mapped (their
#           programs, libraries, fonts, icon caches, locale data), plus each --file (read, not
#           mapped: the wallpaper), and of each the pages in memory now (mincore), as ranges.
#           LIST: one file per line, "<physical offset>\t<ranges>\t<path>", sorted by where the
#           file starts on the disk (FIEMAP; the inode number where it has none), so the fetch
#           reads in disk order.
#   fetch   when game mode ends: POSIX_FADV_WILLNEED for every range (the reads go to the disk
#           queued together, its scheduler orders them), then each range read, which waits for
#           them: done when all of it is in memory again.
# Each prints one line of numbers for the journal (pages are counted in MB).
# The game prefetch (game-*: further down) is the same program.
import errno, os, signal, stat, sys, time

PAGE = os.sysconf("SC_PAGE_SIZE")
GAP = 32  # pages: closer ranges are read as one (cheaper than a seek on a hard disk)
PROT_READ, MAP_SHARED = 1, 1
FS_IOC_FIEMAP = 0xC020660B
_C = None


def C():
    """(ctypes, libc, MAP_FAILED), loaded when first needed: the game watcher, which lives while a
    game loads, doesn't need them and stays small"""
    global _C
    if _C is None:
        import ctypes
        libc = ctypes.CDLL(None, use_errno=True)
        libc.mmap.restype = ctypes.c_void_p
        libc.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_long]
        libc.munmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        libc.mincore.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p]
        _C = (ctypes, libc, ctypes.c_void_p(-1).value)
    return _C


def say(msg):
    print("ps3-game-prefetch: " + msg, file=sys.stderr, flush=True)


def resident(fd, size):
    """the file's pages in memory: list of (first page, page count)"""
    if size <= 0:
        return []
    ctypes, libc, MAP_FAILED = C()
    addr = libc.mmap(None, size, PROT_READ, MAP_SHARED, fd, 0)
    if addr in (None, MAP_FAILED):
        return []
    try:
        n = (size + PAGE - 1) // PAGE
        vec = ctypes.create_string_buffer(n)
        if libc.mincore(addr, size, vec) != 0:
            return []
        runs, start, last = [], None, None
        for i, b in enumerate(vec.raw):
            if b & 1:
                if start is None:
                    start = i
                elif i - last > GAP:
                    runs.append((start, last - start + 1))
                    start = i
                last = i
        if start is not None:
            runs.append((start, last - start + 1))
        return runs
    finally:
        libc.munmap(addr, size)


def physical(fd, ino):
    """where the file starts on the disk (its first extent), for the order"""
    import fcntl, struct
    req = struct.pack("=QQIIII", 0, 0xFFFFFFFFFFFFFFFF, 0, 0, 1, 0) + bytes(56)
    try:
        out = fcntl.ioctl(fd, FS_IOC_FIEMAP, req)
        if struct.unpack_from("=I", out, 20)[0] >= 1:
            return struct.unpack_from("=Q", out, 32 + 8)[0]
    except OSError:
        pass
    return ino << 20


def record(lst, args):
    files, pids = [], []
    while args:
        a = args.pop(0)
        if a == "--file" and args:
            files.append(args.pop(0))
        elif a.isdigit():
            pids.append(a)
    seen = set()
    for p in pids:
        try:
            with open("/proc/%s/maps" % p) as f:
                for line in f:
                    parts = line.split(None, 5)
                    if len(parts) == 6 and parts[4] != "0" and parts[5].startswith("/"):
                        path = parts[5].rstrip("\n")
                        if not path.endswith(" (deleted)") and not path.startswith(("/dev/", "/memfd:", "/SYSV")):
                            files.append(path)
        except OSError:  # gone, or not ours (X runs as root)
            pass
    out, total = [], 0
    for path in files:
        if path in seen:
            continue
        seen.add(path)
        try:
            fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
        except OSError:
            continue
        try:
            st = os.fstat(fd)
            if not (st.st_mode & 0o170000 == 0o100000):
                continue
            runs = resident(fd, st.st_size)
            if runs:
                total += sum(n for _, n in runs)
                out.append((physical(fd, st.st_ino), ",".join("%d+%d" % r for r in runs), path))
        finally:
            os.close(fd)
    out.sort()
    tmp = lst + ".new"
    with open(tmp, "w") as f:
        for phys, runs, path in out:
            f.write("%d\t%s\t%s\n" % (phys, runs, path))
    os.rename(tmp, lst)
    say("recorded %d files, %.1f MB in memory" % (len(out), total * PAGE / 1048576))


def fetch(lst):
    t0 = time.monotonic()
    todo = []
    with open(lst) as f:
        for line in f:
            try:
                _, runs, path = line.rstrip("\n").split("\t", 2)
            except ValueError:
                continue
            todo.append((path, [tuple(map(int, r.split("+"))) for r in runs.split(",")]))
    fds, total, cached = [], 0, 0
    for path, runs in todo:  # what is still in memory, then all of it asked for at once
        try:
            fd = os.open(path, os.O_RDONLY)
        except OSError:
            continue
        size = os.fstat(fd).st_size
        have = set()
        for s, n in resident(fd, size):
            have.update(range(s, s + n))
        for s, n in runs:
            total += n
            cached += sum(1 for i in range(s, s + n) if i in have)
            os.posix_fadvise(fd, s * PAGE, n * PAGE, os.POSIX_FADV_WILLNEED)
        fds.append((fd, runs))
    t1 = time.monotonic()
    for fd, runs in fds:  # waits for the reads
        for s, n in runs:
            off, end = s * PAGE, (s + n) * PAGE
            while off < end:
                b = os.pread(fd, min(1 << 20, end - off), off)
                if not b:
                    break
                off += len(b)
        os.close(fd)
    mb = PAGE / 1048576
    say("fetched %d files, %.1f MB (%.1f MB were still in memory): asked in %.1f s, all in after %.1f s"
        % (len(fds), total * mb, cached * mb, t1 - t0, time.monotonic() - t0))


# ---- a game's own files at its start (docs/STALLS.md "Game prefetch") -------------------------
# The PS3's root file system is a 2.5" hard disk on USB 2.0: 10-20 ms a random read. A game loading
# faults its program, libraries and data in page by page (and reads its packs and textures in small
# pieces), each one such a read. The game-* commands learn what a game used and read it back in
# disk order at its next start, in one pass of large reads at low I/O priority.
#   game-watch STATE   ps3-game-guard's helper once game mode is on: each game registered in
#           STATE/games is claimed (STATE/gp/<id>.seen, its record), its list fetched (game-fetch,
#           a process of its own) and the game looked at for its first SAMPLE_S seconds; the
#           watcher exits when no game is left to look at (the guard starts another for a game
#           that comes later), so nothing of it stays in memory while the game plays on
#   game-learn STATE [ID...]   each record (all of them: game mode leaving) learnt into its list
#   game-run [--mode M] [--key CMD] [--dir D] -- CMD...   CMD with its list fetched alongside and
#           learnt after it exits (the bench harness, or by hand without game mode); with
#           --mode fetch it becomes CMD (exec), the fetch a detached process of its own
#   game-fetch [--dir D] [--pid PID] KEY...   the first KEY with a list: its ranges, as many as the
#           budget allows (the earliest used first), read in the order of their place on the disk;
#           stops when PID (the game) is gone
#   game-show KEY | game-list | game-forget KEY   the lists (D/<key>.list): look, drop one
# Learning: the game's processes (the registered pids and their children) are looked at for their
# first SAMPLE_S seconds (every 1 s for 20 s, every 3 s until 2 min, then every 10 s): the files
# they have mapped or open, their working directories, which one is the game (the largest RSS).
# Then: those files, every file under the game's data directories (/usr/share/games/<name>,
# /usr/share/<name>, ~/.<name>, ~/.local/share/<name>, the game's working directory...: files read
# and closed again, such as Sauerbraten's maps and textures, or used later on) and the files of the
# old list, each with the pages in memory now (mincore) as ranges, each range with its place on the
# disk (FIEMAP). The new list is the union with the old one (same size and mtime: else the new
# ranges only); a file without a page in memory for 3 sessions in a row is dropped. Key: the game's
# program (the largest process's executable, its script for an interpreter), which also stands for
# its package version: the list remembers its size and mtime, and an upgraded program makes it
# stale (not fetched, learnt anew). The command the game was started by (ps3-game's, game-run's
# --key) is a symlink to it, so a launcher script and a detected game share one list.
# Fetching, at the start: budget = min(PS3_GAME_PREFETCH_MB, default 64 MB; MemFree and
# MemAvailable less PS3_GAME_PREFETCH_RESERVE_MB, default 48) of pages not in memory, filled in the
# order the game first used its files; read sorted by disk address, 256 KiB at a time, at I/O
# priority PS3_GAME_PREFETCH_IO (be7, default; be0-7; idle) and nice 10, one read in flight and no
# readahead of ours, so a read of the game waits for one of ours at most. Stops early when MemFree
# falls under the reserve, on direct reclaim or swapping (/proc/vmstat), after 60 s, when the game
# is gone. Switch: PS3_GAME_PREFETCH_GAME (the game's environment): on: fetch and learn; off is the default (2026-10-18: no gain on the PS3, Luanti slower, see docs/STALLS.md);
# fetch: fetch only (bench runs against one fixed list); learn: learn only; off. Lists in
# PS3_GAME_PREFETCH_DIR (default $XDG_CACHE_HOME/ps3-game-prefetch, ~/.cache/ps3-game-prefetch).
# PS3_GAME_PREFETCH_SAMPLE_S, PS3_GAME_PREFETCH_LEARN_S: the looking and the learning's time limits.
# Only os, sys, time, signal at the start: ctypes, fcntl, struct come in with the first mincore or
# FIEMAP (the fetch, the learning), not into the watcher.

CHUNK = 64            # pages a read (256 KiB)
MISSES = 3            # sessions without a page in memory before a file leaves the list
WALK_FILES = 30000    # files looked at under a game's data directories, at most
SAMPLE_S = 180        # seconds a game is looked at: its loading and first minutes
FIEMAP_EXTENT_LAST, FIEMAP_EXTENT_UNKNOWN = 0x1, 0x2
MEMINFO, VMSTAT = "/proc/meminfo", "/proc/vmstat"
_ONE = bytes(b & 1 for b in range(256))
# not data directories of a game: shared by everything (a font or icon used is listed by itself)
SHARE_SKIP = {"locale", "icons", "mime", "X11", "fonts", "themes", "glib-2.0", "zoneinfo", "i18n", "doc",
              "man", "info", "dbus-1", "applications", "pixmaps", "alsa", "drirc.d", "glvnd", "egl",
              "vulkan", "terminfo", "misc", "perl", "perl5", "python3", "ca-certificates", "fontconfig",
              "gtk-3.0", "gtk-2.0", "lintian", "bash-completion", "zsh", "sounds", "mesa", "xml",
              "sgml", "polkit-1", "systemd", "dict", "common-licenses", "menu", "desktop-directories"}
NOT_DATA = ("/usr/lib/", "/lib/", "/lib64/", "/usr/lib64/", "/usr/bin/", "/bin/", "/usr/sbin/", "/sbin/",
            "/etc/", "/usr/libexec/", "/usr/include/", "/var/lib/", "/var/cache/", "/var/log/", "/proc/",
            "/sys/", "/dev/", "/run/", "/tmp/", "/memfd:", "/SYSV")
SKIP_SEEN = ("/proc/", "/sys/", "/dev/", "/run/", "/memfd:", "/SYSV")
WRAPPERS = {"env", "stdbuf", "nice", "ionice", "taskset", "chrt", "setsid", "timeout", "nohup"}
WRAPPER_VALUES = {"env": ("-u", "-C", "-S", "--unset", "--chdir"), "nice": ("-n", "--adjustment"),
                  "ionice": ("-c", "-n", "-p", "--class", "--classdata"), "stdbuf": ("-i", "-o", "-e"),
                  "timeout": ("-s", "-k", "--signal", "--kill-after")}
INTERPRETERS = {"python", "perl", "ruby", "lua", "sh", "bash", "dash", "java", "mono", "node", "tclsh", "wish"}
IOPRIO_SET = {"x86_64": 251, "ppc64": 273, "ppc64le": 273, "ppc": 273, "aarch64": 30, "i686": 289,
              "armv7l": 314}
STOP = []


def knob(env, name, default):
    v = (env or {}).get(name) or os.environ.get(name)
    return v if v else default


def fknob(env, name, default):
    try:
        return float(knob(env, name, str(default)))
    except ValueError:
        return float(default)


def list_dir(env=None):
    d = knob(env, "PS3_GAME_PREFETCH_DIR", "")
    if d:
        return d
    cache = knob(env, "XDG_CACHE_HOME", "") or os.path.join(knob(env, "HOME", "/tmp"), ".cache")
    return os.path.join(cache, "ps3-game-prefetch")


def key_path(d, key):
    return os.path.join(d, key.replace("%", "%25").replace("/", "%") + ".list")


def command_of(argv):
    """the program of a command line, past env, stdbuf, nice and the like"""
    i = 0
    while i < len(argv):
        b = os.path.basename(argv[i])
        if b not in WRAPPERS:
            return argv[i]
        i += 1
        while i < len(argv) and (argv[i].startswith("-") or (b == "env" and "=" in argv[i])):
            if argv[i] in WRAPPER_VALUES.get(b, ()):
                i += 1   # an option with its value apart (nice -n 10, ionice -c 3, env -u NAME)
            i += 1
        if b in ("taskset", "timeout", "chrt") and i < len(argv):
            i += 1       # their first operand is a mask, a duration or a priority
    return None


def resolve(cmd, path=None):
    """a command's key: its absolute path with the symlinks resolved"""
    if not cmd:
        return None
    if "/" not in cmd:
        import shutil
        cmd = shutil.which(cmd, path=path or os.environ.get("PATH") or os.defpath)
        if not cmd:
            return None
    return os.path.realpath(cmd)


def interpreter(exe):
    b = os.path.basename(exe)
    return b.rstrip("0123456789.") in INTERPRETERS or b.startswith("luajit")


def runs_of(vec, gap=GAP, base=0):
    """mincore's vector as ranges [(first page, count)], holes under gap pages filled"""
    v, runs = vec.translate(_ONE), []
    i = v.find(1)
    while i >= 0:
        j = v.find(0, i)
        j = len(v) if j < 0 else j
        s, e = i + base, j + base
        if runs and s - (runs[-1][0] + runs[-1][1]) < gap:
            runs[-1] = (runs[-1][0], e - runs[-1][0])
        else:
            runs.append((s, e - s))
        i = v.find(1, j)
    return runs


def union(a, b, gap=GAP):
    """two range lists as one (sorted, overlaps and holes under gap pages joined)"""
    out = []
    for s, n in sorted(list(a) + list(b)):
        if out and s - (out[-1][0] + out[-1][1]) < gap:
            out[-1] = (out[-1][0], max(out[-1][0] + out[-1][1], s + n) - out[-1][0])
        else:
            out.append((s, n))
    return out


class Mapped:
    """a regular file open and mapped, for mincore"""

    def __init__(self, path):
        self.fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
        self.addr = None
        try:
            self.st = os.fstat(self.fd)
            self.size = self.st.st_size
            self.pages = (self.size + PAGE - 1) // PAGE
            if not stat.S_ISREG(self.st.st_mode):
                raise OSError(errno.EINVAL, "not a regular file")
            if self.size > 0:
                _, libc, failed = C()
                a = libc.mmap(None, self.size, PROT_READ, MAP_SHARED, self.fd, 0)
                if a not in (None, failed):
                    self.addr = a
        except BaseException:
            os.close(self.fd)
            raise

    def vec(self, start=0, n=None):
        n = self.pages - start if n is None else min(n, self.pages - start)
        if self.addr is None or n <= 0:
            return b""
        ctypes, libc, _ = C()
        buf = ctypes.create_string_buffer(n)
        if libc.mincore(self.addr + start * PAGE, n * PAGE, buf) != 0:
            return b""
        return buf.raw

    def resident(self, gap=GAP):
        return runs_of(self.vec(), gap)

    def missing(self, start, n):
        """pages of [start, start + n) not in memory"""
        v = self.vec(start, n)
        return v.translate(_ONE).count(0) if v else n

    def close(self):
        if self.addr is not None:
            C()[1].munmap(self.addr, self.size)
            self.addr = None
        if self.fd >= 0:
            os.close(self.fd)
            self.fd = -1


def extents(fd, batch=128):
    """the file's extents [(logical, physical or None, length)] in bytes (FIEMAP); None without
    FIEMAP (not a disk file system: nothing to read from a disk)"""
    import fcntl, struct
    out, start = [], 0
    while True:
        buf = bytearray(struct.pack("=QQIIII", start, 0xFFFFFFFFFFFFFFFF - start, 0, 0, batch, 0)
                        + bytes(56 * batch))
        try:
            fcntl.ioctl(fd, FS_IOC_FIEMAP, buf, True)
        except OSError:
            return None
        n = struct.unpack_from("=I", buf, 20)[0]
        last = n < batch
        for i in range(n):
            lg, ph, ln = struct.unpack_from("=QQQ", buf, 32 + 56 * i)
            fl = struct.unpack_from("=I", buf, 32 + 56 * i + 40)[0]
            out.append((lg, ph if not fl & FIEMAP_EXTENT_UNKNOWN else None, ln))
            last = last or bool(fl & FIEMAP_EXTENT_LAST)
        if last or not n:
            return out
        start = out[-1][0] + out[-1][2]


def phys_at(ext, off, fallback):
    """the disk address of the file's byte off: in its extent, else the end of the closest known
    extent before it (the first known one, else fallback)"""
    best = None
    for lg, ph, ln in ext:
        if lg > off:
            break
        if ph is not None:
            best = ph + (off - lg if off < lg + ln else ln)
    if best is None:
        best = next((ph for _, ph, _ in ext if ph is not None), fallback)
    return best


class Entry:
    __slots__ = ("path", "t", "sessions", "misses", "size", "mtime", "ranges")

    def __init__(self, path, t, sessions, misses, size, mtime, ranges):
        self.path, self.t, self.sessions, self.misses = path, t, sessions, misses
        self.size, self.mtime, self.ranges = size, mtime, ranges   # ranges: [(page, count, phys)]

    def pages(self):
        return sum(n for _, n, _ in self.ranges)


class GameList:
    def __init__(self, key=None):
        self.key, self.head, self.entries = key, {}, {}

    @classmethod
    def load(cls, path):
        g = cls()
        with open(path) as f:
            for line in f:
                line = line.rstrip("\n")
                if line.startswith("# ") and "=" in line:
                    k, _, v = line[2:].partition("=")
                    g.head[k] = v
                    continue
                if not line or line.startswith("#"):
                    continue
                try:
                    t, ses, mis, size, mt, rs, path = line.split("\t", 6)
                    ranges = []
                    for r in rs.split(","):
                        a, _, ph = r.partition("@")
                        s, _, n = a.partition("+")
                        ranges.append((int(s), int(n), int(ph)))
                    g.entries[path] = Entry(path, float(t), int(ses), int(mis), int(size), int(mt), ranges)
                except ValueError:
                    continue
        g.key = g.head.get("key")
        return g

    def save(self, path):
        tmp = path + ".new"
        with open(tmp, "w") as f:
            f.write("# ps3-game-prefetch game list\n")
            for k, v in self.head.items():
                f.write("# %s=%s\n" % (k, v))
            for e in sorted(self.entries.values(), key=lambda e: (prio(e), e.path)):
                f.write("%g\t%d\t%d\t%d\t%d\t%s\t%s\n" % (e.t, e.sessions, e.misses, e.size, e.mtime,
                        ",".join("%d+%d@%d" % r for r in e.ranges), e.path))
        os.rename(tmp, path)

    def pages(self):
        return sum(e.pages() for e in self.entries.values())


def prio(e):
    """the order the budget is filled in: files by when the game first had them (the ones found
    only at the end last), then the ones used in more sessions"""
    return (e.t if e.t >= 0 else 1e9, -e.sessions)


def find_list(d, keys):
    """(path, list) of the first key that has one"""
    for k in keys:
        if not k:
            continue
        p = key_path(d, k)
        try:
            return p, GameList.load(p)
        except OSError:
            continue
    return None, None


def ident(path):
    """the program's identity (size:mtime ns), for a list going stale with an upgrade"""
    try:
        st = os.stat(path)
        return "%d:%d" % (st.st_size, st.st_mtime_ns)
    except (OSError, TypeError):
        return ""


def proc_argv(pid):
    try:
        with open("/proc/%d/cmdline" % pid, "rb") as f:
            return [a.decode(errors="replace") for a in f.read().split(b"\0")[:-1]]
    except OSError:
        return []


def proc_env(pid):
    try:
        with open("/proc/%d/environ" % pid, "rb") as f:
            env = {}
            for kv in f.read().split(b"\0"):
                k, eq, v = kv.decode(errors="replace").partition("=")
                if eq:
                    env[k] = v
            return env
    except OSError:
        return {}


def proc_alive(pid):
    """the process runs (not a zombie)"""
    try:
        with open("/proc/%d/stat" % pid) as f:
            return f.read().rsplit(")", 1)[1].split()[0] not in ("Z", "X")
    except (OSError, IndexError):
        return False


class Sampler:
    """what a game's processes have mapped and open, looked at now and then"""

    def __init__(self, pids, t0=None):
        self.roots = {int(p) for p in pids}
        self.t0 = time.monotonic() if t0 is None else t0
        self.seen = {}        # path -> seconds after the start when first seen
        self.by_pid = {}      # pid -> [its paths, its working directory, its executable]
        self.pids = set(self.roots)
        self.main = None      # (rss pages, exe, argv, cwd, pid) of the largest process seen
        self.n = 0
        self.next = self.t0
        self.cost = 0.0

    def alive(self):
        return any(proc_alive(p) for p in self.pids | self.roots)

    def family(self):
        kids = {}
        try:
            names = os.listdir("/proc")
        except OSError:
            return set(self.roots)
        for e in names:
            if not e.isdigit():
                continue
            try:
                with open("/proc/%s/stat" % e) as f:
                    ppid = int(f.read().rsplit(")", 1)[1].split()[1])
            except (OSError, ValueError, IndexError):
                continue
            kids.setdefault(ppid, []).append(int(e))
        out, todo = set(), list(self.roots | self.pids)
        while todo:
            p = todo.pop()
            if p not in out:
                out.add(p)
                todo += kids.get(p, [])
        return {p for p in out if os.path.isdir("/proc/%d" % p)}

    def due(self, now=None):
        return (time.monotonic() if now is None else now) >= self.next

    def sample(self, now=None):
        c0 = time.monotonic()
        now = c0 if now is None else now
        t = now - self.t0
        if self.n < 5 or self.n % 5 == 0:   # children: at the first looks, then every fifth
            self.pids = self.family()
        for p in list(self.pids):
            if not self.one(p, t):
                self.pids.discard(p)
        self.n += 1
        self.next = now + (1 if t < 20 else 3 if t < 120 else 10)
        self.cost += time.monotonic() - c0

    def one(self, p, t):
        base = "/proc/%d" % p
        paths = []
        try:
            with open(base + "/maps") as f:
                for line in f:
                    parts = line.split(None, 5)
                    if len(parts) == 6 and parts[4] != "0" and parts[5].startswith("/"):
                        paths.append(parts[5].rstrip("\n"))
        except OSError:
            return False
        try:
            for fd in os.listdir(base + "/fd"):
                try:
                    link = os.readlink(base + "/fd/" + fd)
                except OSError:
                    continue
                if link.startswith("/"):
                    paths.append(link)
        except OSError:
            pass
        cwd = exe = None
        try:
            cwd = os.readlink(base + "/cwd")
            exe = os.readlink(base + "/exe")
        except OSError:
            pass
        mine = self.by_pid.get(p)
        if mine is None or mine[2] != exe:   # a program of its own now (exec: a launcher became the game)
            mine = self.by_pid[p] = [set(), cwd, exe]
        mine[1] = cwd or mine[1]
        for x in paths:
            if x.endswith(" (deleted)") or x.startswith(SKIP_SEEN) or "\t" in x:
                continue
            mine[0].add(x)
            if x not in self.seen:
                self.seen[x] = round(t, 1)
        try:
            with open(base + "/statm") as f:
                rss = int(f.read().split()[1])
            if exe and (self.main is None or rss > self.main[0] or self.main[4] == p):
                self.main = (rss, exe, proc_argv(p), cwd, p)
        except (OSError, ValueError, IndexError):
            pass
        return True

    def main_key(self):
        """the game's program: the largest process's executable (for an interpreter, its script)"""
        if not self.main:
            return None
        _, exe, argv, cwd, _ = self.main
        exe = exe[:-len(" (deleted)")] if exe.endswith(" (deleted)") else exe
        if interpreter(exe):
            for a in argv[1:]:
                if a.startswith("-"):
                    continue
                a = os.path.join(cwd or "/", a)
                return os.path.realpath(a) if os.path.isfile(a) else None
            return None
        return exe

    def save(self, path, head):
        """the record: head (key: value) and what was seen, for game-learn"""
        lines = ["#%s\t%s" % kv for kv in head.items()]
        lines.append("#looks\t%d\t%.3f" % (self.n, self.cost))
        if self.main:
            rss, exe, argv, cwd, pid = self.main
            lines.append("#main\t%d\t%s\t%s\t%d\t%s" % (rss, exe, cwd or "", pid, "\x1f".join(argv)))
            mine = self.by_pid.get(pid)
            if mine:
                lines += ["M\t" + x for x in sorted(mine[0])]
        lines += ["S\t%g\t%s" % (t, x) for x, t in self.seen.items()]
        tmp = path + ".new"
        with open(tmp, "w") as f:
            f.write("\n".join(lines) + "\n")
        os.rename(tmp, path)

    @classmethod
    def load(cls, path):
        """(sampler, head) from a record"""
        s, head, mine = cls([]), {}, set()
        with open(path) as f:
            for line in f:
                k, _, v = line.rstrip("\n").partition("\t")
                if k == "S":
                    t, _, x = v.partition("\t")
                    s.seen[x] = float(t)
                elif k == "M":
                    mine.add(v)
                elif k == "#looks":
                    n, _, c = v.partition("\t")
                    s.n, s.cost = int(n), float(c)
                elif k == "#main":
                    f5 = v.split("\t", 4)
                    s.main = (int(f5[0]), f5[1], f5[4].split("\x1f") if f5[4] else [], f5[2] or None, int(f5[3]))
                elif k.startswith("#"):
                    head[k[1:]] = v
        if s.main:
            s.by_pid[s.main[4]] = [mine, s.main[3], s.main[1]]
        return s, head


def data_root(path, home):
    """(directory, recursive) the file belongs to as game data, or None"""
    for pre in ("/usr/share/games/", "/usr/local/share/games/", "/usr/lib/games/", "/usr/games/",
                "/usr/local/games/", "/opt/"):
        if path.startswith(pre):
            name = path[len(pre):].split("/", 1)
            return (pre + name[0], True) if len(name) == 2 and name[0] else None
    for pre in ("/usr/share/", "/usr/local/share/"):
        if path.startswith(pre):
            name = path[len(pre):].split("/", 1)
            ok = len(name) == 2 and name[0] and name[0] not in SHARE_SKIP
            return (pre + name[0], True) if ok else None
    if home and home != "/" and path.startswith(home + "/"):
        rest = path[len(home) + 1:].split("/")
        if rest[0] == ".local" and len(rest) >= 4 and rest[1] == "share":
            return home + "/.local/share/" + rest[2], True
        if rest[0] in (".config", ".cache", ".local"):
            if len(rest) < 3 or rest[1] == "ps3-game-prefetch":
                return None
            return home + "/" + rest[0] + "/" + rest[1], True
        if rest[0].startswith(".") and len(rest) >= 2:
            return home + "/" + rest[0], True
        return (os.path.dirname(path), False) if len(rest) >= 2 else None
    if path.startswith(NOT_DATA):
        return None
    d = os.path.dirname(path)
    return (d, False) if d not in ("", "/") else None


def roots_of(sampler, home, names=()):
    """the data directories: of the game's own process (the largest; a launcher script's or a
    shell's files say nothing about the game's data), else of all its processes"""
    rec, flat = set(), set()
    procs = list(sampler.by_pid.values())
    if sampler.main and sampler.main[4] in sampler.by_pid:
        procs = [sampler.by_pid[sampler.main[4]]]
    paths = [x for ps, _, _ in procs for x in ps] + [c.rstrip("/") + "/-" for _, c, _ in procs if c]
    for p in paths:
        r = data_root(p, home)
        if r:
            (rec if r[1] else flat).add(r[0])
    for n in names:   # the program's name as a data directory (/usr/share/games/<name>)
        for pre in ("/usr/share/games/", "/usr/share/", "/usr/local/share/games/"):
            if n and n not in SHARE_SKIP and os.path.isdir(pre + n):
                rec.add(pre + n)
    # a directory inside another one is walked with it
    rec = {r for r in rec if not any(r != o and r.startswith(o + "/") for o in rec)}
    flat = {f for f in flat if not any(f == o or f.startswith(o + "/") for o in rec)}
    return sorted(rec), sorted(flat)


def walk(rec, flat, limit, deadline):
    """the regular files under rec (recursively) and in flat; True as well if cut short"""
    out, stack = [], list(rec)
    while stack:
        d = stack.pop()
        try:
            it = os.scandir(d)
        except OSError:
            continue
        with it:
            for e in it:
                try:
                    if e.is_dir(follow_symlinks=False):
                        stack.append(e.path)
                    elif e.is_file(follow_symlinks=False):
                        out.append(e.path)
                except OSError:
                    continue
                if len(out) >= limit or time.monotonic() > deadline:
                    return out, True
    for d in flat:
        try:
            with os.scandir(d) as it:
                out += [e.path for e in it if e.is_file(follow_symlinks=False)]
        except OSError:
            continue
        if len(out) >= limit or time.monotonic() > deadline:
            return out, True
    return out, False


def learn(d, keys, sampler, deadline_s=15.0, fetched=False):
    """the game's files and their pages in memory now into its list (see the top). keys: the
    command it was started by (or none); the list goes under the program's key, the command's is a
    symlink to it. The list's path, or None"""
    c0 = time.monotonic()
    deadline = c0 + deadline_s
    main = sampler.main_key()
    keys = [k for k in keys if k]
    _, old = find_list(d, ([main] if main else []) + keys)
    if main:
        primary = main
    elif old and old.key:   # the game not seen: the list its command links to
        primary = old.key
    elif keys:
        primary = keys[0]
    else:
        say("game-learn: no key (the game was gone before it was seen)")
        return None
    exe = main or (old.head.get("exe") if old else None) or primary
    eid = ident(exe)
    stale = False
    if old and (old.head.get("exe") != exe or old.head.get("exe_id") != eid):
        old, stale = None, True   # another program now, or this one upgraded
    home = os.environ.get("HOME", "")
    rec, flat = roots_of(sampler, home, {os.path.basename(k) for k in [primary] + keys})
    files, cut = walk(rec, flat, WALK_FILES, deadline)
    cand = dict.fromkeys(files, -1.0)
    cand.update(sampler.seen)
    if old:
        for p, e in old.entries.items():
            if p not in cand or (cand[p] < 0 <= e.t):
                cand[p] = e.t
    own = os.path.realpath(d) + "/"
    new, now_pages, late = {}, 0, False
    for p, t in cand.items():
        if time.monotonic() > deadline:
            late = True
            break
        if p.startswith(own):
            continue
        try:
            m = Mapped(p)
        except OSError:
            continue
        try:
            runs = m.resident()
            if not runs:
                continue
            ext = extents(m.fd)
            if ext is None:
                continue
            o = old.entries.get(p) if old else None
            same = o is not None and o.size == m.size and o.mtime == m.st.st_mtime_ns
            now_pages += sum(n for _, n in runs)
            if same:
                runs = union([(s, n) for s, n, _ in o.ranges], runs)
            fb = m.st.st_ino << 20
            ranges = [(s, n, phys_at(ext, s * PAGE, fb)) for s, n in runs]
            ts = [x for x in (t, o.t if same else -1) if x >= 0]
            new[p] = Entry(p, min(ts) if ts else -1.0, (o.sessions if same else 0) + 1, 0, m.size,
                           m.st.st_mtime_ns, ranges)
        finally:
            m.close()
    dropped = kept = 0
    if old:
        for p, o in old.entries.items():
            if p in new:
                continue
            try:
                st = os.stat(p)
                if st.st_size != o.size or st.st_mtime_ns != o.mtime:
                    raise OSError
            except OSError:
                dropped += 1
                continue
            if late or o.misses + 1 < MISSES:   # (cut short: not looked at, not a miss)
                o.misses += 0 if late else 1
                new[p] = o
                kept += 1
            else:
                dropped += 1
    g = GameList(primary)
    g.head = {"key": primary, "exe": exe, "exe_id": eid, "page": str(PAGE),
              "sessions": str(int(old.head.get("sessions", "0")) + 1 if old else 1),
              "learnt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
              "fetched": "1" if fetched else "0"}
    g.entries = new
    os.makedirs(d, exist_ok=True)
    path = key_path(d, primary)
    g.save(path)
    for k in keys:   # the commands the game was started by: links to the program's list
        if k == primary:
            continue
        lp = key_path(d, k)
        try:
            if os.path.islink(lp) and os.readlink(lp) == os.path.basename(path):
                continue
            tmp = lp + ".new"
            if os.path.lexists(tmp):
                os.unlink(tmp)
            os.symlink(os.path.basename(path), tmp)
            os.rename(tmp, lp)
        except OSError:
            pass
    mb = PAGE / 1048576
    say("game-learn %s: %d files, %.1f MB listed (%.1f MB in memory now), %d kept though not in memory, "
        "%d dropped%s%s; %d looks at the game (%.2f s), %d roots, learnt in %.1f s"
        % (primary, len(new), g.pages() * mb, now_pages * mb, kept, dropped, ", stale list replaced" if stale else "",
           ", cut short" if cut or late else "", sampler.n, sampler.cost, len(rec) + len(flat), time.monotonic() - c0))
    return path


def meminfo():
    out = {}
    try:
        with open(MEMINFO) as f:
            for line in f:
                k, _, v = line.partition(":")
                out[k] = int(v.split()[0]) * 1024
    except (OSError, ValueError, IndexError):
        pass
    return out


def vmstat():
    """(pages swapped in and out, direct reclaims and compaction stalls) so far"""
    sw = rc = 0
    try:
        with open(VMSTAT) as f:
            for line in f:
                k, _, v = line.partition(" ")
                if k in ("pswpin", "pswpout"):
                    sw += int(v)
                elif k.startswith("allocstall") or k == "compact_stall":
                    rc += int(v)
    except (OSError, ValueError):
        pass
    return sw, rc


def ioprio(spec):
    """this process's I/O priority: be<N> (best effort, level N), rt<N> or idle"""
    nr = IOPRIO_SET.get(os.uname().machine)
    if spec == "idle":
        val = 3 << 13
    elif len(spec or "") == 3 and spec[:2] in ("be", "rt") and spec[2] in "01234567":
        val = ((2 if spec[:2] == "be" else 1) << 13) | int(spec[2])
    else:
        return False
    return bool(nr) and C()[1].syscall(nr, 1, 0, val) == 0


def select(g, budget, deadline=None):
    """the ranges to read: [(phys, entry, first page, count)], as many as hold budget pages not in
    memory now, in prio order (each file opened and its ranges looked at with mincore, nothing
    read), then sorted by phys; and (pages not in memory, pages in memory, files looked at)"""
    sel, miss_n, have_n, looked = [], 0, 0, 0
    for e in sorted(g.entries.values(), key=lambda e: (prio(e), e.path)):
        if miss_n >= budget or (deadline and time.monotonic() > deadline):
            break
        try:
            m = Mapped(e.path)
        except OSError:
            continue
        looked += 1
        try:
            if m.size != e.size or m.st.st_mtime_ns != e.mtime:
                continue
            for s, c, ph in e.ranges:
                c = max(0, min(c, m.pages - s))
                miss = m.missing(s, c) if c else 0
                have_n += c - miss
                if not miss:
                    continue
                if miss_n + miss > budget:   # the last one cut to the budget (roughly: holes in it count)
                    c = max(1, c * (budget - miss_n) // miss)
                    miss = budget - miss_n
                sel.append((ph, e, s, c))
                miss_n += miss
                if miss_n >= budget:
                    break
        finally:
            m.close()
    sel.sort(key=lambda x: (x[0], x[1].path, x[2]))
    return sel, miss_n, have_n, looked


def game_fetch(d, keys, env=None, max_s=60.0, alive=None):
    """the list's ranges back into memory (see the top); a dict of numbers, or None without a list"""
    c0 = time.monotonic()
    path, g = find_list(d, keys)
    name = next((k for k in keys if k), "?")
    if not g:
        say("game-fetch %s: nothing learnt yet" % name)
        return None
    if g.head.get("exe_id") != ident(g.head.get("exe")) or g.head.get("page") != str(PAGE):
        say("game-fetch %s: the list is stale (%s changed): not read" % (name, g.head.get("exe")))
        return None
    mb = 1048576
    cap = int(fknob(env, "PS3_GAME_PREFETCH_MB", 64) * mb) // PAGE
    reserve = int(fknob(env, "PS3_GAME_PREFETCH_RESERVE_MB", 48) * mb)
    mi = meminfo()
    room = min(mi.get("MemFree", 0), mi.get("MemAvailable", 0)) - reserve
    budget = max(0, min(cap, room // PAGE))
    listed = g.pages()
    if budget <= 0:
        say("game-fetch %s: no room (MemFree %d MB, MemAvailable %d MB, reserve %d MB)"
            % (name, mi.get("MemFree", 0) // mb, mi.get("MemAvailable", 0) // mb, reserve // mb))
        return {"read": 0, "stop": "room"}
    io = knob(env, "PS3_GAME_PREFETCH_IO", "be7")
    prio_ok = ioprio(io)
    try:
        os.nice(10)
    except OSError:
        pass
    c1 = time.monotonic()
    sel, sel_miss, sel_have, looked = select(g, budget, c0 + max_s / 2)
    t_sel = time.monotonic() - c1
    sw0, rc0 = vmstat()
    files, order, bad, used = {}, [], set(), set()
    buf = bytearray(CHUNK * PAGE)
    read = cached = nread = 0
    stop = None
    for _, e, s, n in sel:
        if STOP or (alive and not alive()):
            stop = "game gone"
            break
        if e.path in bad:
            continue
        m = files.get(e.path)
        if m is None:
            try:
                m = Mapped(e.path)
            except OSError:
                bad.add(e.path)
                continue
            if m.size != e.size or m.st.st_mtime_ns != e.mtime:
                m.close()
                bad.add(e.path)
                continue
            try:   # no readahead of our own: what is read is what the list says (and the budget counts)
                os.posix_fadvise(m.fd, 0, 0, os.POSIX_FADV_RANDOM)
            except OSError:
                pass
            files[e.path] = m
            order.append(e.path)
            if len(order) > 64:   # the files read lately stay open (the ranges come in disk order)
                files.pop(order.pop(0)).close()
        used.add(e.path)
        for c in range(s, min(s + n, m.pages), CHUNK):
            k = min(CHUNK, s + n - c, m.pages - c)
            miss = m.missing(c, k)
            if not miss:
                cached += k
                continue
            free = meminfo().get("MemFree", 0)
            if free < reserve:
                stop = "memory (MemFree %d MB)" % (free // mb)
                break
            if nread % 4 == 0:
                sw, rc = vmstat()
                if rc > rc0:
                    stop = "direct reclaim"
                    break
                if sw - sw0 > 16:
                    stop = "swapping"
                    break
            try:
                os.preadv(m.fd, [memoryview(buf)[:k * PAGE]], c * PAGE)
            except OSError:
                bad.add(e.path)
                break
            nread += 1
            read += miss
            cached += k - miss
            if STOP or (alive and not alive()):
                stop = "game gone"
                break
            if time.monotonic() - c0 > max_s:
                stop = "time"
                break
        if stop:
            break
    for m in files.values():
        m.close()
    pm = PAGE / mb
    say("game-fetch %s: read %.1f MB from %d files in %.1f s (%.1f MB of the list in memory already, %d files "
        "looked at in %.1f s; budget %.1f MB, %.1f MB listed; %d reads, io %s%s)%s"
        % (name, read * pm, len(used), time.monotonic() - c0, sel_have * pm, looked, t_sel, budget * pm,
           listed * pm, nread, io, "" if prio_ok else " not set", ", stopped: " + stop if stop else ""))
    return {"read": read, "cached": cached, "selected": sel_miss, "in_memory": sel_have, "files": len(used),
            "stop": stop}


def pid_watch(pid):
    """(a function: is the process still there, the pidfd it uses or None)"""
    try:
        pfd = os.pidfd_open(pid)
    except (AttributeError, OSError):
        return (lambda: proc_alive(pid)), None
    import select

    def alive():
        return not select.select([pfd], [], [], 0)[0]
    return alive, pfd


def default_signals():
    """SIGPIPE and SIGXFSZ as a program expects them (Python ignores them, and an exec or spawn
    would pass that on to the game)"""
    for s in (signal.SIGPIPE, signal.SIGXFSZ):
        signal.signal(s, signal.SIG_DFL)


SELF = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ps3-game-prefetch")   # the program


def spawn_fetch(d, keys, env, pid):
    """game-fetch as a process of its own (its I/O priority, nice level, and its exit not ours)"""
    e = dict(os.environ)
    for k, v in (env or {}).items():
        if k.startswith("PS3_GAME_PREFETCH_"):
            e[k] = v
    argv = [sys.executable, "-I", SELF, "game-fetch", "--dir", d, "--pid", str(pid), "--"]
    try:
        return os.posix_spawn(sys.executable, argv + [k for k in keys if k], e)
    except OSError as x:
        say("game-fetch: %s" % x)
        return None


def mode_of(env):
    m = knob(env, "PS3_GAME_PREFETCH_GAME", "off")
    return m if m in ("on", "off", "learn", "fetch") else "on"


def reap():
    """the children that are done (fetches), without waiting"""
    while True:
        try:
            pid, _ = os.waitpid(-1, os.WNOHANG)
        except ChildProcessError:
            return
        if not pid:
            return


def game_run(args):
    """game-run [--mode M] [--key CMD] [--dir D] -- CMD...: CMD with its list fetched alongside and
    learnt after; CMD's exit status. Without learning (fetch, off) nothing of this stays: the fetch
    is a process of its own (detached, in the same process group) and this one becomes CMD (exec),
    so the game has the pid, parent and memory it has without it"""
    mode, key, d = mode_of(None), None, None
    while args and args[0] != "--":
        a = args.pop(0)
        if a in ("--mode", "--key", "--dir") and args:
            v = args.pop(0)
            if a == "--mode":
                mode = v
            elif a == "--key":
                key = v
            else:
                d = v
        else:
            raise SystemExit("game-run: unknown option %s" % a)
    cmd = args[1:]
    if not cmd or mode not in ("on", "fetch", "learn", "off"):
        raise SystemExit("usage: ps3-game-prefetch game-run [--mode on|fetch|learn|off] [--key CMD] [--dir D] -- CMD...")
    d = d or list_dir()
    key = resolve(key or command_of(cmd))
    if mode in ("fetch", "off"):
        if mode == "fetch" and key:
            fetch_detached(d, [key])
        default_signals()
        os.execvp(cmd[0], cmd)
    fetch = spawn_fetch(d, [key], None, os.getpid()) if mode == "on" else None
    try:
        child = os.posix_spawnp(cmd[0], cmd, os.environ, setsigdef=(signal.SIGPIPE, signal.SIGXFSZ))
    except OSError as e:
        say("game-run: %s: %s" % (cmd[0], e))
        return 127

    def fwd(sig, _):   # the harness stops the game: passed on, learnt after it is gone
        try:
            os.kill(child, sig)
        except OSError:
            pass
    for s in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(s, fwd)
    learning = Sampler([child])
    window = fknob(None, "PS3_GAME_PREFETCH_SAMPLE_S", SAMPLE_S)
    status = None
    while status is None:
        pid, st = os.waitpid(child, os.WNOHANG)
        if pid:
            status = st
            break
        if fetch:
            try:
                if os.waitpid(fetch, os.WNOHANG)[0]:
                    fetch = None
            except ChildProcessError:
                fetch = None
        if time.monotonic() - learning.t0 > window:   # looked at long enough: wait for the end
            _, status = os.waitpid(child, 0)
            break
        if learning.due():
            learning.sample()
        time.sleep(max(0.05, min(1.0, learning.next - time.monotonic())))
    if fetch:
        try:
            os.kill(fetch, signal.SIGTERM)
            os.waitpid(fetch, 0)
        except OSError:
            pass
    try:
        learn(d, [key], learning, fknob(None, "PS3_GAME_PREFETCH_LEARN_S", 8), mode == "on")
    except OSError as e:
        say("game-learn %s: %s" % (key, e))
    rc = os.waitstatus_to_exitcode(status)
    return 128 - rc if rc < 0 else rc


def fetch_detached(d, keys):
    """game-fetch in a grandchild that stops once this process (about to become the game) is gone"""
    game = os.getpid()
    pid = os.fork()
    if pid:
        os.waitpid(pid, 0)   # the middle child exits at once: nothing left for the game to reap
        return
    try:
        if os.fork():
            os._exit(0)
        alive, pfd = pid_watch(game)
        for fd in [int(x) for x in os.listdir("/proc/self/fd")]:   # the game's descriptors are not ours
            if fd > 2 and fd != pfd:
                try:
                    os.close(fd)
                except OSError:
                    pass
        signal.signal(signal.SIGTERM, lambda s, _: STOP.append(s))
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        game_fetch(d, keys, None, alive=alive)
    except BaseException as e:   # never back into the caller's code: that is the game's exec
        say("game-fetch: %s" % e)
    finally:
        os._exit(0)


def record_path(S, gid):
    return os.path.join(S, "gp", gid + ".seen")


def game_watch(S):
    """ps3-game-guard's helper (see the top): claims the registered games, starts their fetches,
    looks at each for its first SAMPLE_S seconds, keeps their records; exits when none is left"""
    signal.signal(signal.SIGTERM, lambda s, _: STOP.append(s))
    signal.signal(signal.SIGINT, lambda s, _: STOP.append(s))
    gdir = os.path.join(S, "games")
    os.makedirs(os.path.join(S, "gp"), exist_ok=True)
    games, done = {}, set()   # gid -> [sampler, head, record path, until, ino]; (gid, ino) let go
    window = fknob(None, "PS3_GAME_PREFETCH_SAMPLE_S", SAMPLE_S)
    first = True
    while not STOP:
        now = time.monotonic()
        try:
            ids = {x: os.stat(os.path.join(gdir, x)).st_ino for x in os.listdir(gdir) if not x.endswith(".new")}
        except OSError:
            ids = {}
        for gid, ino in ids.items():
            if (gid in games and games[gid][4] == ino) or (gid, ino) in done:
                continue
            rp = record_path(S, gid)
            try:
                with open(os.path.join(gdir, gid)) as f:
                    pids = [int(x.split()[0]) for x in f if x.split() and x.split()[0].isdigit()]
            except OSError:
                pids = []
            pids = [p for p in pids if proc_alive(p)]
            if not pids:
                continue
            if gid in games or os.path.exists(rp):   # again (F11 back into fullscreen): looked at
                try:                                # some more, not fetched again
                    s, head = (games[gid][0], games[gid][1]) if gid in games else Sampler.load(rp)
                except (OSError, ValueError, IndexError):
                    s, head = None, None
                if s is not None and head.get("mode") == "off":
                    done.add((gid, ino))
                    continue
                if s is not None:
                    s.roots |= set(pids)
                    s.pids |= set(pids)
                    games[gid] = [s, head, rp, now + window, ino]
                    continue
            env = proc_env(pids[0])
            mode = mode_of(env)
            keys = []
            if not gid.startswith("auto-"):   # ps3-game's launcher: sh .../ps3-game CMD...
                argv = proc_argv(pids[0])
                i = next((i for i, a in enumerate(argv) if os.path.basename(a) == "ps3-game"), None)
                if i is not None:
                    keys.append(resolve(command_of(argv[i + 1:]), env.get("PATH")))
            s = Sampler(pids)
            head = {"gid": gid, "mode": mode, "dir": list_dir(env), "learn_s": knob(env, "PS3_GAME_PREFETCH_LEARN_S", "10")}
            if mode == "off":
                say("game %s: PS3_GAME_PREFETCH_GAME=off" % gid)
                head["keys"] = ""
                s.save(rp, head)   # claimed: the guard starts no watcher for it
                done.add((gid, ino))
                continue
            s.sample()
            if gid.startswith("auto-"):   # detected: the game runs (ps3-game's starts after the join)
                keys.append(s.main_key())
            keys = list(dict.fromkeys(k for k in keys if k))
            head["keys"] = "\x1f".join(keys)
            head["fetched"] = "1" if mode in ("on", "fetch") and keys else "0"
            s.save(rp, head)
            games[gid] = [s, head, rp, now + window, ino]
            if head["fetched"] == "1":
                spawn_fetch(head["dir"], keys, env, pids[0])
        young = False
        for gid, g in list(games.items()):
            s, head, rp, until, ino = g
            if ids.get(gid) != ino or not s.alive() or now > until:
                del games[gid]
                done.add((gid, ino))
                continue
            if s.due(now):
                s.sample(now)
                if gid in ids:
                    try:
                        s.save(rp, head)
                    except OSError:
                        pass
            young = young or now - s.t0 < 5
        reap()
        if not games and not first:
            break
        first = False
        time.sleep(0.25 if young else 1.0)
    for s, head, rp, _, _ in games.values():
        try:
            s.save(rp, head)
        except OSError:
            pass
    reap()


def game_learn(S, gids):
    """the records (of gids, else all) learnt into their lists, then removed"""
    gp = os.path.join(S, "gp")
    try:
        names = sorted(os.listdir(gp))
    except OSError:
        return 0
    mine = []
    for n in names:
        p = os.path.join(gp, n)
        if n.endswith(".seen") and (not gids or n[:-5] in gids):
            q = p[:-5] + ".%d.learning" % os.getpid()
        elif n.endswith(".learning") and n.split(".")[-2].isdigit() and not proc_alive(int(n.split(".")[-2])):
            q = p.rsplit(".", 2)[0] + ".%d.learning" % os.getpid()   # a learner that died meanwhile
        else:
            continue
        try:
            os.rename(p, q)
        except OSError:
            continue   # another learner has it
        mine.append(q)
    for q in mine:
        try:
            s, head = Sampler.load(q)
            if head.get("mode") in ("on", "learn"):
                keys = [k for k in head.get("keys", "").split("\x1f") if k]
                learn(head.get("dir") or list_dir(), keys, s, fknob(head, "learn_s", 10), head.get("fetched") == "1")
        except (OSError, ValueError, IndexError) as e:
            say("game-learn %s: %s" % (os.path.basename(q), e))
        finally:
            try:
                os.unlink(q)
            except OSError:
                pass
    return 0


def game_show(d, key):
    path, g = find_list(d, [resolve(key) or key, key])
    if not g:
        print("no list for %s in %s" % (key, d))
        return 1
    mb = PAGE / 1048576
    print("%s (%s)" % (path, ", ".join("%s=%s" % kv for kv in g.head.items())))
    print("%d files, %.1f MB" % (len(g.entries), g.pages() * mb))
    for e in sorted(g.entries.values(), key=lambda e: (prio(e), e.path)):
        print("%7.1f s  %6.2f MB  %3d ranges  seen %d  missed %d  %s"
              % (e.t, e.pages() * mb, len(e.ranges), e.sessions, e.misses, e.path))
    return 0


def game_list(d):
    try:
        names = sorted(os.listdir(d))
    except OSError:
        names = []
    for n in names:
        if not n.endswith(".list"):
            continue
        p = os.path.join(d, n)
        key = n[:-5].replace("%", "/").replace("/25", "%")
        if os.path.islink(p):
            print("%s -> %s" % (key, os.readlink(p)[:-5].replace("%", "/").replace("/25", "%")))
            continue
        try:
            g = GameList.load(p)
            print("%s: %d files, %.1f MB, %s sessions, learnt %s"
                  % (key, len(g.entries), g.pages() * PAGE / 1048576, g.head.get("sessions"), g.head.get("learnt")))
        except OSError:
            pass
    return 0


def game_forget(d, key):
    k = resolve(key) or key
    p = key_path(d, k)
    target = os.path.join(d, os.readlink(p)) if os.path.islink(p) else None
    n = 0
    for x in (p, target):
        if x and os.path.lexists(x):
            os.unlink(x)
            n += 1
    for name in os.listdir(d) if os.path.isdir(d) else []:   # links left pointing at nothing
        q = os.path.join(d, name)
        if os.path.islink(q) and not os.path.exists(q):
            os.unlink(q)
    print("%s: %s" % (key, "forgotten" if n else "no list"))
    return 0 if n else 1


def opts(args, *names):
    """--name VALUE options at the front of args (removed), and a "--" after them"""
    out = {}
    while len(args) >= 2 and args[0] in names:
        out[args[0]] = args[1]
        del args[:2]
    if args and args[0] == "--":
        del args[0]
    return out


USAGE = ("usage: ps3-game-prefetch record LIST [--file PATH]... PID... | fetch LIST |\n"
         "  game-watch STATE | game-learn STATE [ID...] |\n"
         "  game-run [--mode on|fetch|learn|off] [--key CMD] [--dir D] -- CMD... |\n"
         "  game-fetch [--dir D] [--pid PID] KEY... | game-show [--dir D] KEY | game-list [--dir D] |\n"
         "  game-forget [--dir D] KEY")


def main(argv):
    cmd, args = (argv[1] if len(argv) > 1 else ""), argv[2:]
    if cmd == "record" and args:
        record(args[0], args[1:])
    elif cmd == "fetch" and len(args) == 1:
        fetch(args[0])
    elif cmd == "game-run":
        return game_run(args)
    elif cmd == "game-watch" and len(args) == 1:
        game_watch(args[0])
    elif cmd == "game-learn" and args:
        return game_learn(args[0], set(args[1:]))
    elif cmd == "game-fetch":
        o = opts(args, "--dir", "--pid")
        signal.signal(signal.SIGTERM, lambda s, _: STOP.append(s))
        alive = pid_watch(int(o["--pid"]))[0] if o.get("--pid", "").isdigit() else None
        game_fetch(o.get("--dir") or list_dir(), [resolve(k) or k for k in args], alive=alive)
    elif cmd in ("game-show", "game-list", "game-forget"):
        d = opts(args, "--dir").get("--dir") or list_dir()
        if cmd == "game-list":
            return game_list(d)
        if len(args) != 1:
            print(USAGE, file=sys.stderr)
            return 2
        return (game_show if cmd == "game-show" else game_forget)(d, args[0])
    else:
        print(USAGE, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
