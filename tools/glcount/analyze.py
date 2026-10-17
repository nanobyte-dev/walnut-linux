#!/usr/bin/env python3
import sys, struct, subprocess, collections, array, os
d = sys.argv[1]; top = int(sys.argv[2]) if len(sys.argv) > 2 else 40
exe = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.environ.get('SRC','src') + '/sauer_client')
names = open(d + '/glc.names').read().split('\n')
marks = {l.split()[0]: (int(l.split()[1]), int(l.split()[2])) for l in open(d + '/glc.marks') if l.strip()}
a = array.array('Q'); a.frombytes(open(d + '/glc.log', 'rb').read())
s, f0 = marks['start']; e, f1 = marks['end']
nfr = f1 - f0
base = None
for l in open(d + '/glc.maps'):
    if l.rstrip().endswith('sauer_client'):
        base = int(l.split('-')[0], 16); break
cnt = collections.Counter(); byfn = collections.Counter()
for v in a[s:e]:
    sl = v >> 48; ret = v & ((1 << 48) - 1)
    cnt[(sl, ret)] += 1; byfn[sl] += 1
isdraw = lambda n: n.startswith(('glDraw', 'glMultiDraw'))
tot = sum(byfn.values()); draws = sum(c for sl, c in byfn.items() if isdraw(names[sl]))
print(f'{d}: {nfr} frames, GL calls/frame {tot/nfr:.1f}, draws/frame {draws/nfr:.1f}')
print('-- per function (per frame)')
for sl, c in byfn.most_common(top):
    print(f'  {c/nfr:8.1f} {names[sl]}')
rets = sorted({r for (_, r) in cnt})
loc = {}
if base is not None:
    p = subprocess.run(['addr2line', '-f', '-C', '-i', '-e', exe] + [hex(r - base) for r in rets if r >= base and r < base + 0x4000000], capture_output=True, text=True).stdout.split('\n')
    # -i gives multiple lines; use -a to split
    p = subprocess.run(['addr2line', '-a', '-f', '-C', '-e', exe] + [hex(r - base) for r in rets if r >= base and r < base + 0x4000000], capture_output=True, text=True).stdout.split('\n')
    i = 0; cur = None
    toks = p
    k = 0
    while k + 2 < len(toks):
        if toks[k].startswith('0x'):
            loc[int(toks[k], 16) + base] = (toks[k + 1], toks[k + 2].replace('/src/', '')); k += 3
        else: k += 1
by = collections.Counter(); bydraw = collections.Counter()
for (sl, r), c in cnt.items():
    fn, ln = loc.get(r, ('?', hex(r)))
    by[(fn, ln.rsplit(':', 1)[0] if False else ln)] += c
    if isdraw(names[sl]): bydraw[(fn, ln, names[sl])] += c
print('-- calls by caller (per frame)')
for (fn, ln), c in by.most_common(top): print(f'  {c/nfr:8.1f} {fn} {ln}')
print('-- draws by caller (per frame)')
for (fn, ln, n), c in bydraw.most_common(top): print(f'  {c/nfr:8.1f} {n} {fn} {ln}')
