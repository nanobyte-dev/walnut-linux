#!/usr/bin/env python3
"""Symbolise a PERF=1 run's perf.data on the PC (ppc64 big-endian, ELFv1) into call stacks
(docs/HOTPATHS.md). usage: analyze.py RUN_DIR OUT.pkl
Needs the symbol root symroot.sh makes (HOTPATHS_SYM, default build/tmp/hotpaths-sym) and the Mesa
sysroot (build/mesa/sysroot) for libc. Uses perf's MMAP2 records plus the MEM=1 smaps: perf misses
the mmaps made while the record is disabled (-D -1), so Mesa loaded during the game's load is
[unknown] in the PS3's own perf report. Kernel and ps3rsx frames from System.map and the module.
The pickle: {'game': Counter((thread, frames)), 'xorg': ..., 'times': [...]}, a frame being
(class, dso, symbol), outermost first; class one of game mesa drm kgpu kernel libcmem libc other unk."""
import sys, os, re, glob, pickle, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import perfdata, elfsym

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.abspath(os.path.join(HERE, '..', '..', '..', 'build'))
SYM = os.environ.get('HOTPATHS_SYM') or os.path.join(BUILD, 'tmp', 'hotpaths-sym')
KVER = os.environ.get('HOTPATHS_KVER', '6.12.111+ps3.178')
CTX = 0xfffffffffffff001  # >= PERF_CONTEXT_MAX (-4095)

MESA_RE = re.compile(r'libgallium|_dri\.so|dri_gbm\.so|libGLX_mesa|libEGL_mesa|libgbm\.so|libglapi|'
                     r'/libGL\.so|/libGLX\.so|/libGLdispatch|/libEGL\.so|/libOpenGL|/libGLESv')
KGPU_RE = re.compile(r'^(drm_|__drm|dma_fence|dma_resv|ttm_|nouveau|ps3rsx)')
MEMFN_RE = re.compile(r'^(__)?(mem(cpy|set|move|cmp|cmpeq|chr)|bcopy|bzero)|^__mem(cpy|set|move|cmp)')

_symcache = {}


def find_runtime(path, bid):
    cands = [SYM + path, BUILD + '/mesa/sysroot' + path]
    for c in cands:
        if os.path.exists(c):
            return c
    return None


def debug_for(bid):
    if not bid:
        return None
    p = '%s/usr/lib/debug/.build-id/%s/%s.debug' % (SYM, bid[:2], bid[2:])
    return p if os.path.exists(p) else None


def user_tab(path, bid):
    key = (path, bid)
    if key not in _symcache:
        rt = find_runtime(path, bid)
        tab = None
        if rt:
            try:
                tab, el = elfsym.user_symtab(rt, debug_for(bid))
                tab.elf = el
            except Exception as ex:  # noqa
                print('  !! symtab', path, ex, file=sys.stderr)
                tab = None
        _symcache[key] = tab
    return _symcache[key]


_ktab = None
_mods = {}


def kernel_tab():
    global _ktab
    if _ktab is None:
        _ktab = elfsym.system_map('%s/boot/System.map-%s' % (SYM, KVER))
    return _ktab


def module_tab(name):
    if name not in _mods:
        cands = glob.glob('%s/lib/modules/%s/**/%s.ko' % (SYM, KVER, name), recursive=True) + \
            glob.glob('%s/usr/lib/modules/%s/**/%s.ko' % (SYM, KVER, name), recursive=True)
        _mods[name] = elfsym.module_symtab(cands[0]) if cands else None
    return _mods[name]


EXE = ['']


def dso_class(path):
    if EXE[0] and path == EXE[0]:
        return 'game'
    if path.startswith('[kernel') or path.endswith('.ko'):
        return 'kernel'
    if MESA_RE.search(path):
        return 'mesa'
    if '/libdrm' in path:
        return 'drm'
    b = os.path.basename(path)
    if re.match(r'(libc\.so|ld64\.so|libm\.so|libmvec)', b):
        return 'libc'
    if path.startswith('/usr/lib/powerpc64-linux-gnu/') or path.startswith('/lib/') or \
            path.startswith('/usr/lib/ps3/') or path.startswith('/usr/libexec/') or path.startswith('['):
        return 'other'
    return 'game'


def read_smaps(run):
    out = []
    for lab in ('mid', 'start', 'end'):
        f = os.path.join(run, 'mem', lab + '.smaps')
        if not os.path.exists(f):
            continue
        for line in open(f):
            m = re.match(r'^([0-9a-f]+)-([0-9a-f]+) (\S+) ([0-9a-f]+) \S+ \d+\s+(/\S.*)$', line)
            if m and 'x' in m.group(3):
                out.append((int(m.group(1), 16), int(m.group(2), 16), int(m.group(4), 16), m.group(5).strip(), None))
        if out:
            break
    return out


class Maps:
    def __init__(self, entries, extra):
        # perf entries win; smaps fill holes
        self.e = sorted(entries, key=lambda x: x[0])
        have = [(a, b) for a, b, *_ in self.e]
        for x in extra:
            if not any(a <= x[0] < b for a, b in have):
                self.e.append(x)
                self.filled = getattr(self, 'filled', set()) | {x[3]}
        self.e.sort(key=lambda x: x[0])
        self.starts = [x[0] for x in self.e]

    def find(self, ip):
        import bisect
        i = bisect.bisect_right(self.starts, ip) - 1
        # later mmaps for the same range may overlap: scan back a little
        for j in range(i, max(-1, i - 8), -1):
            a, b, pgoff, name, bid = self.e[j]
            if a <= ip < b:
                return self.e[j]
        return None


def build_ids(run):
    ids = {}
    f = os.path.join(run, 'perf-buildids.txt')
    if os.path.exists(f):
        for line in open(f):
            p = line.split()
            if len(p) == 2:
                ids[p[1]] = p[0]
    return ids


def main(run, out):
    d = perfdata.read(os.path.join(run, 'perf.data'))
    tg = open(os.path.join(run, 'perf.targets')).read()
    game = int(re.search(r'game=(\d+)', tg).group(1))
    xorg = int(re.search(r'xorg=(\d+)', tg).group(1)) if 'xorg=' in tg else -2
    try:
        EXE[0] = open(os.path.join(run, 'perf.exe')).read().strip()
    except OSError:
        pass
    kmaps = d['mmaps'].get(0xffffffff, [])
    bids = build_ids(run)
    smaps = read_smaps(run)
    pmaps = {}
    for pid in (game, xorg):
        ent = d['mmaps'].get(pid, [])
        ent = [(a, b, pg, n, bid or bids.get(n)) for a, b, pg, n, bid in ent]
        ex = [(a, b, pg, n, bids.get(n)) for a, b, pg, n, _ in smaps] if pid == game else []
        pmaps[pid] = Maps(ent, ex)
    ksyms = kernel_tab()
    kmods = [(a, b, os.path.basename(n)[:-3]) for a, b, pg, n, bid in kmaps if n.endswith('.ko')]
    ksyms_cache = {}

    def ksym(ip):
        r = ksyms_cache.get(ip)
        if r:
            return r
        r = None
        for a, b, m in kmods:
            if a <= ip < b:
                t = module_tab(m)
                s = t.lookup(ip - a) if t else None
                r = ('kgpu' if (m == 'ps3rsx' or m.startswith('drm')) else 'kernel', '[' + m + ']', s or '[%s]' % m)
                break
        if r is None:
            s = ksyms.lookup(ip) or '[k]'
            r = ('kgpu' if KGPU_RE.match(s) else 'kernel', '[kernel]', s)
        ksyms_cache[ip] = r
        return r

    ucache = {}

    def usym(pid, ip):
        key = (pid, ip)
        r = ucache.get(key)
        if r:
            return r
        m = pmaps[pid].find(ip)
        if m is None:
            r = ('unk', '[unknown]', '[unknown]')
        else:
            a, b, pgoff, name, bid = m
            cls = dso_class(name)
            short = os.path.basename(name)
            tab = user_tab(name, bid) if name.startswith('/') else None
            s = None
            if tab:
                va = tab.elf.file_off_to_vaddr(ip - a + pgoff)
                s = tab.lookup(va)
            if cls == 'libc' and s and MEMFN_RE.search(s):
                cls = 'libcmem'
            r = (cls, short, s or '[%s]' % short)
        ucache[key] = r
        return r

    folded = {game: collections.Counter(), xorg: collections.Counter()}
    times = []
    for pid, tid, t, per, ips in d['samples']:
        if pid not in folded:
            continue
        frames = []
        mode = None
        n = 0
        for ip in ips:
            if ip >= CTX:
                mode = 'k' if ip == 0xffffffffffffff80 else ('u' if ip == 0xfffffffffffffe00 else 'x')
                n = 0
                continue
            if ip == 0 or mode == 'x':
                continue
            if n > 0:
                ip -= 4  # return address -> the call
            f = ksym(ip) if mode == 'k' else usym(pid, ip)
            frames.append((mode, n, ip, f))
            n += 1
        # ppc64 LR duplicates: per context, drop frame 1 if it equals frame 2's address or is the leaf's function
        clean = []
        for i, (mode, n, ip, f) in enumerate(frames):
            if n == 1:
                prev = frames[i - 1]
                nxt = frames[i + 1] if i + 1 < len(frames) and frames[i + 1][0] == mode else None
                if (nxt and nxt[2] == ip) or prev[3] == f:
                    continue
            clean.append(f)
        thr = d['comm'].get(tid, ':%d' % tid)
        if pid == game:
            times.append(t)
        folded[pid][(thr, tuple(reversed(clean)))] += 1
    res = {'run': run, 'game_pid': game, 'game': folded[game], 'xorg': folded[xorg],
           'filled': sorted(getattr(pmaps[game], 'filled', ())), 'tmin': min(times) if times else 0,
           'tmax': max(times) if times else 0, 'times': times}
    with open(out, 'wb') as f:
        pickle.dump(res, f, protocol=4)
    tot = sum(folded[game].values())
    print('%s: game %d samples, xorg %d, filled from smaps: %s' % (
        os.path.basename(run), tot, sum(folded[xorg].values()), ', '.join(os.path.basename(x) for x in res['filled'])))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
