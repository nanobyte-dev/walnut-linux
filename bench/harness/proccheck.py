#!/usr/bin/python3 -I
"""proccheck.py ALLOWLIST SELF_PID SNAPSHOT...: is anything unexpected running next to a benchmark?
(docs/games/SUITE.md "Process check"). The snapshots are `ps -eo pid,ppid,rss,pcpu,stat,comm,args`
(client.sh's processes-{start,mid,end}.txt). Prints the names of the unexpected processes, comma
separated, on the first line (empty: all clear), then one line per process with the reason. Exits 0
either way; 2 when no snapshot can be read.
Rules (allowed-processes.txt): kernel threads, SELF_PID's ancestors and descendants (the harness, the
game and its helpers) are always fine; a name on a "!" line (outside those) is unexpected whatever it uses; any other
name that is not allowed is unexpected above 2% CPU or 20 MB RSS (THRESHOLDS below)."""
import fnmatch
import re
import sys

CPU_MAX = 2.0       # % (ps pcpu: CPU time over the process's lifetime)
RSS_MAX_KB = 20 * 1024


def load_allowlist(path):
    allow, deny = [], []
    for line in open(path, errors="replace"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("!"):
            deny.append(line[1:].strip())
            continue
        name, _, rx = line.partition(" ~ ")
        allow.append((name.strip(), re.compile(rx.strip()) if rx.strip() else None))
    return allow, deny


def parse(path):
    rows = []
    for n, line in enumerate(open(path, errors="replace")):
        if n == 0 and line.lstrip().startswith("PID"):
            continue
        f = line.split(None, 6)
        if len(f) < 6:
            continue
        try:
            rows.append(dict(pid=int(f[0]), ppid=int(f[1]), rss=int(f[2]), cpu=float(f[3]), stat=f[4],
                             comm=f[5], args=f[6].strip() if len(f) > 6 else f[5]))
        except ValueError:
            continue
    return rows


def exempt_pids(rows, self_pid):
    by = {r["pid"]: r for r in rows}
    ok = set()
    p = self_pid
    while p in by and p not in ok:      # ancestors: the shell, ssh session, ps3-bench.sh, systemd
        ok.add(p)
        p = by[p]["ppid"]
    kids = {}
    for r in rows:
        kids.setdefault(r["ppid"], []).append(r["pid"])
    todo = [self_pid]
    while todo:                          # descendants: the game, helpers, this very ps
        q = todo.pop()
        for c in kids.get(q, []):
            if c not in ok:
                ok.add(c)
                todo.append(c)
    return ok


def check(rows, self_pid, allow, deny):
    ok = exempt_pids(rows, self_pid)
    out = []
    for r in rows:
        c = r["comm"]
        if r["pid"] in (1, 2) or r["ppid"] == 2 or r["pid"] == 0:
            continue                      # init, kthreadd and the kernel threads
        if r["pid"] in ok:
            continue
        if any(fnmatch.fnmatchcase(c, d) for d in deny):
            out.append((r, "not allowed beside a benchmark"))
            continue
        if any(fnmatch.fnmatchcase(c, n) and (rx is None or rx.search(r["args"])) for n, rx in allow):
            continue
        if r["cpu"] > CPU_MAX or r["rss"] > RSS_MAX_KB:
            out.append((r, "%.1f%% CPU, %d MB RSS, not on the list" % (r["cpu"], r["rss"] // 1024)))
    return out


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    allow, deny = load_allowlist(argv[0])
    self_pid = int(argv[1])
    seen, details, readable = [], [], 0
    for snap in argv[2:]:
        try:
            rows = parse(snap)
        except OSError:
            continue
        if not rows:
            continue
        readable += 1
        tag = snap.rsplit("processes-", 1)[-1].rsplit(".", 1)[0]
        for r, why in check(rows, self_pid, allow, deny):
            if r["comm"] not in seen:
                seen.append(r["comm"])
            details.append("%s: %s (pid %d, ppid %d) %s: %s" % (tag, r["comm"], r["pid"], r["ppid"], why, r["args"][:100]))
    if not readable:
        return 2
    print(",".join(seen))
    for d in details:
        print(d)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
