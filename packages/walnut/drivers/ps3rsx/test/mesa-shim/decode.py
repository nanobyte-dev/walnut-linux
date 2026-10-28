#!/usr/bin/env python3
# decode.py PUSH.BIN [SUBMIT]: wrap.c's push dump as methods, named from Mesa's nv30 headers
# (build/work/mesa, make mesa-work; or MESA_SRC=<mesa>/src)
import os, re, struct, sys
M = os.environ.get('MESA_SRC', os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../../../../build/work/mesa/src'))
def defs(path, prefixes):
    """method offset -> name; value and field defines (NAME_FIELD_VALUE under a method) left out"""
    found = []
    for line in open(path):
        m = re.match(r'#define\s+(\w+)(?:\(i0\))?\s+(?:\(?(0x[0-9a-fA-F]+)(?:\s*\+\s*0x([0-9a-fA-F]+)\*\(i0\))?\)?)\s*$', line)
        if not m: continue
        name, base = m.group(1), int(m.group(2), 16)
        if not any(name.startswith(p) for p in prefixes) or '__' in name: continue
        found.append((name, base, int(m.group(3), 16) if m.group(3) else 0))
    tail = lambda n: re.sub(r'^NV\w\w_(3D|M2MF|SIFM|SURFACE_SWZ|SURFACE_2D)_', '', n)
    names = {tail(n) for n, _, _ in found}
    d = {}
    for name, base, stride in found:
        if base < 0x100 or base > 0x1ffc or base & 3: continue
        if any(tail(name).startswith(o + '_') for o in names): continue
        for i in range(16 if stride else 1):
            d.setdefault(base + stride * i, f'{name}({i})' if stride else name)
    return d
d3 = defs(M + '/gallium/drivers/nouveau/nv30/nv30-40_3d.xml.h', ['NV30_3D_', 'NV40_3D_', 'NV35_3D_', 'NV25_3D_'])
d2 = defs(M + '/gallium/drivers/nouveau/nv30/nv01_2d.xml.h', ['NV03_SIFM_', 'NV04_SURFACE_SWZ_', 'NV04_SURFACE_2D_', 'NV05_SIFM_'])
dm = defs(M + '/gallium/drivers/nouveau/nv_m2mf.xml.h', ['NV03_M2MF_', 'NV04_M2MF_'])
SUB = {7: ('3D', d3), 2: ('M2MF', dm), 3: ('SF2D', {k: v for k, v in d2.items() if 'SURFACE_2D' in v}),
       4: ('SSWZ', {k: v for k, v in d2.items() if 'SWZ' in v}), 5: ('SIFM', {k: v for k, v in d2.items() if 'SIFM' in v}), 6: ('K6', dm)}
def name(sc, m):
    if m == 0: return 'BIND'
    if m < 0x100: return f'HOST_{m:x}'
    n, d = SUB.get(sc, (f'sc{sc}', {}))
    return f'{n}.{d.get(m, hex(m))}'
data = open(sys.argv[1], 'rb').read()
only = int(sys.argv[2]) if len(sys.argv) > 2 else None
p = 0
while p < len(data):
    mag, sub, idx, n = struct.unpack('>4I', data[p:p + 16]); p += 16
    w = struct.unpack(f'>{n}I', data[p:p + 4 * n]); p += 4 * n
    if only is not None and sub != only: continue
    print(f'== submit {sub} push {idx} ({n} words)')
    i = 0
    while i < n:
        h = w[i]
        if h == 0: i += 1; continue
        cnt = (h >> 18) & 0x7ff; sc = (h >> 13) & 7; m = h & 0x1ffc; ni = (h & 0xe0030003) == 0x40000000
        vals = w[i + 1:i + 1 + cnt]
        if cnt > 12 and (ni or m in (0x1818, 0xb80)):
            print(f'  {name(sc, m)}{" NI" if ni else ""} x{cnt}: ' + ' '.join(f'{v:08x}' for v in vals[:4]) + ' ...')
        else:
            for k, v in enumerate(vals):
                mm = m if ni else m + 4 * k
                print(f'  [{sc}] {mm:04x} {name(sc, mm):40s} {v:08x}')
        i += 1 + cnt
