#!/usr/bin/env python3
"""attrib.py PPECOST_OUT MAPS [DEBUGDIR...]: ppecost's per-PC counts summed per function.
Functions from `powerpc64-linux-gnu-nm -n --synthetic` of each mapped file (or its .debug by
build-id under DEBUGDIR/.build-id), run in the deb builder image. Prints the top functions by the
crude PPE estimate insns + 40*lhs + 8*mul + 40*div."""
import bisect, collections, os, re, subprocess, sys

out, maps = sys.argv[1], sys.argv[2]
dbgdirs = sys.argv[3:]
segs = []   # (start, end, offset, path)
for l in open(maps):
    p = l.split()
    if len(p) >= 6 and 'x' in p[1]:
        a, b = (int(x, 16) for x in p[0].split('-'))
        segs.append((a, b, int(p[2], 16), p[5]))
segs.sort()
cache = {}
def run(cmd):
    return subprocess.run(["docker", "run", "--rm", "-v", "/:/h:ro", "ps3-deb-builder:30cec2dc4988"] + cmd,
                          capture_output=True, text=True).stdout
def symtab(path, root):
    if path in cache: return cache[path]
    host = root + path if os.path.exists(root + path) else path
    syms = []
    cands = [host]
    bid = re.search(r"Build ID: ([0-9a-f]+)", run(["powerpc64-linux-gnu-readelf", "-n", "/h" + os.path.realpath(host)]) or "")
    if bid:
        for d in dbgdirs:
            f = os.path.join(d, "usr/lib/debug/.build-id", bid[1][:2], bid[1][2:] + ".debug")
            if os.path.exists(f): cands.insert(0, f)
    if len(cands) > 1: dbgfile[path] = cands[0]
    for c in cands:
        for l in run(["powerpc64-linux-gnu-nm", "-n", "--synthetic", "-C", "/h" + os.path.realpath(c)]).splitlines():
            m = re.match(r"([0-9a-f]+) [tTwW] (\S.*)", l)
            if m and m[2].startswith('.'):
                syms.append((int(m[1], 16), m[2][1:]))
        if syms: break
    # load bias: the first PT_LOAD with X: vaddr - offset
    ph = run(["powerpc64-linux-gnu-readelf", "-lW", "/h" + os.path.realpath(host)])
    bias = 0
    for l in ph.splitlines():
        p = l.split()
        if p and p[0] == "LOAD" and "E" in l:
            bias = int(p[2], 16) - int(p[1], 16); break
    syms.sort()
    cache[path] = (syms, bias)
    return cache[path]
root = os.environ.get("ROOT", "")
dbgfile, pend = {}, {}
tot = collections.defaultdict(lambda: [0, 0, 0, 0])
starts = [s[0] for s in segs]
for l in open(out):
    pc, n, lhs, kind = l.split(); pc = int(pc, 16); n = int(n); lhs = int(lhs); kind = int(kind)
    i = bisect.bisect_right(starts, pc) - 1
    name = "?"
    if i >= 0 and pc < segs[i][1]:
        a, b, off, path = segs[i]
        syms, bias = symtab(path, root)
        va = pc - a + off + bias
        j = bisect.bisect_right([s[0] for s in syms], va) - 1 if syms else -1
        name = os.path.basename(path) + ":" + (syms[j][1] if j >= 0 else "?")
        if j < 0 and dbgfile.get(path):
            pend.setdefault(path, {})[va] = None
            name = (path, va)
    t = tot[name]
    t[0] += n; t[1] += lhs; t[2] += n if kind == 1 else 0; t[3] += n if kind == 2 else 0
# functions by DWARF (addr2line) where the symbol table has no code symbols (ELFv1 .debug files)
names = {}
for path, vas in pend.items():
    vl = sorted(vas)
    o = subprocess.run(["docker", "run", "--rm", "-i", "-v", "/:/h:ro", "ps3-deb-builder:30cec2dc4988",
                        "powerpc64-linux-gnu-addr2line", "-f", "-C", "-e", "/h" + os.path.realpath(dbgfile[path])],
                       input="\n".join("%x" % v for v in vl), capture_output=True, text=True).stdout.splitlines()
    for k, v in enumerate(vl):
        names[(path, v)] = os.path.basename(path) + ":" + (o[2 * k] if 2 * k < len(o) else "?")
merged = collections.defaultdict(lambda: [0, 0, 0, 0])
for k, v in tot.items():
    m = merged[names.get(k, k) if isinstance(k, tuple) else k]
    for i in range(4): m[i] += v[i]
tot = merged
rows = sorted(tot.items(), key=lambda kv: -(kv[1][0] + 40 * kv[1][1] + 8 * kv[1][2] + 40 * kv[1][3]))
allc = sum(v[0] + 40 * v[1] + 8 * v[2] + 40 * v[3] for v in tot.values())
print("%-70s %12s %10s %8s %12s %6s" % ("function", "insns", "lhs", "mul", "est.cycles", "%"))
for k, v in rows[:int(os.environ.get("TOP", "25"))]:
    c = v[0] + 40 * v[1] + 8 * v[2] + 40 * v[3]
    print("%-70s %12d %10d %8d %12d %6.1f" % (k[:70], v[0], v[1], v[2], c, 100.0 * c / allc))
print("total est.cycles %d, insns %d, lhs %d" % (allc, sum(v[0] for v in tot.values()), sum(v[1] for v in tot.values())))
