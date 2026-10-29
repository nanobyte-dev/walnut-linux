#!/usr/bin/env python3
# capture-stats.py PUSH.BIN...: what a wrap.c capture asks of the RSX, to read against the known-safe
# set (docs/RSX.md): every method used (count per subchannel), and at each draw and clear the state
# that has wedged the RSX before: each enabled texture unit's format, size, pitch, offset alignment
# and wrap/filter words, the render target's format, size, pitch and offset alignment, the vertex
# formats; then every M2MF and SIFM copy and surface setup. Names from decode.py (Mesa's headers).
import collections, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from decode import name

TEXFMT = {0x01: 'L8', 0x02: 'A1R5G5B5', 0x03: 'A4R4G4B4', 0x04: 'R5G6B5', 0x05: 'A8R8G8B8', 0x06: 'DXT1',
          0x07: 'DXT3', 0x08: 'DXT5', 0x0b: 'A8L8', 0x10: 'Z24', 0x12: 'Z16', 0x14: 'A16(X16)', 0x15: 'A16L16',
          0x18: 'HILO8', 0x1a: 'RGBA16F', 0x1b: 'RGBA32F'}
RTCOL = {3: 'R5G6B5', 5: 'X8R8G8B8', 8: 'A8R8G8B8', 9: 'B8', 0xb: 'RGBA16F', 0xc: 'RGBA32F', 0xd: 'R32F',
         0xf: 'X8B8G8R8', 0x10: 'A8B8G8R8'}

def texfmt(w):
    f = (w >> 8) & 0xff
    return (f"{TEXFMT.get(f & 0x1f, hex(f & 0x1f))}{' linear' if f & 0x20 else ' swizzled'}"
            f"{' unnorm' if f & 0x40 else ''} {({1: 'VRAM', 2: 'GART'}).get(w & 3, 'dma' + str(w & 3))}{' cube' if w & 4 else ''}{' border' if not w & 8 else ''}"
            f" dims{(w >> 4) & 0xf} levels{(w >> 16) & 0xf}")

def rtfmt(w):
    t = (w >> 8) & 0xf
    return (f"{RTCOL.get(w & 0x1f, hex(w & 0x1f))} zeta{(w >> 5) & 7} "
            f"{'linear' if t == 1 else 'swizzled' if t == 2 else 'type' + str(t)}"
            + (f" log2 {(w >> 16) & 0xff}x{(w >> 24) & 0xff}" if t == 2 else ''))

methods = collections.Counter()
draws, rts, copies, sifm, surf = collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter()
for path in sys.argv[1:]:
    data = open(path, 'rb').read()
    st = collections.defaultdict(dict)	# subchannel -> method -> last data
    p = 0
    while p + 16 <= len(data):
        mag, sub, idx, n = struct.unpack('>4I', data[p:p + 16]); p += 16
        w = struct.unpack(f'>{n}I', data[p:p + 4 * n]); p += 4 * n
        i = 0
        while i < n:
            h = w[i]
            if h == 0: i += 1; continue
            cnt = (h >> 18) & 0x7ff; sc = (h >> 13) & 7; m0 = h & 0x1ffc; ni = (h & 0xe0030003) == 0x40000000
            for k, v in enumerate(w[i + 1:i + 1 + cnt]):
                m = m0 if ni else m0 + 4 * k
                if not (sc == 7 and (0x1818 <= m <= 0x1824 or 0xb80 <= m <= 0xbfc or 0x1f00 <= m <= 0x1f7c)):
                    methods[(sc, m)] += 1
                elif k == 0:
                    methods[(sc, m)] += 1
                s = st[sc]; s[m] = v
                if sc == 7 and ((m == 0x1808 and v) or m == 0x1d94):
                    rt = s.get(0x208, 0); hz = s.get(0x200, 0); vt = s.get(0x204, 0)
                    rts[(('clear' if m == 0x1d94 else 'draw'), rtfmt(rt), f"{hz >> 16}x{vt >> 16}",
                         f"pitch {s.get(0x20c, 0) & 0xffff}", f"offset%128={s.get(0x210, 0) & 127}",
                         f"zeta pitch {s.get(0x22c, 0)} offset%128={s.get(0x214, 0) & 127}" if rt & 0xe0 else '',
                         f"rt_enable {s.get(0x220, 0):x}")] += 1
                    if m == 0x1808:
                        for u in range(16):
                            b = 0x1a00 + 0x20 * u
                            en = s.get(b + 0xc, 0)
                            if not en & 0x80000000: continue
                            sz = s.get(b + 0x18, 0)
                            draws[(u, texfmt(s.get(b + 4, 0)), f"{sz >> 16}x{sz & 0xffff}",
                                   f"pitch {s.get(0x1840 + 4 * u, 0) & 0xffff}",
                                   f"offset%128={s.get(b, 0) & 127}", f"wrap {s.get(b + 8, 0):08x}",
                                   f"enable {en:08x}", f"swz {s.get(b + 0x10, 0):08x}",
                                   f"filter {s.get(b + 0x14, 0):08x}")] += 1
                        vf = tuple(f"{s.get(0x1740 + 4 * a, 0):x}" for a in range(16) if s.get(0x1740 + 4 * a, 0) & 0xf0)
                        draws[('vtxfmt',) + vf] += 1
                if sc == 2 and m == 0x328:
                    copies[(f"dma {s.get(0x184, 0):x}->{s.get(0x188, 0):x}",
                            f"in%128={s.get(0x30c, 0) & 127} out%128={s.get(0x310, 0) & 127}",
                            f"pitch {s.get(0x314, 0)}->{s.get(0x318, 0)}", f"line {s.get(0x31c, 0)} x{s.get(0x320, 0)}",
                            f"format {s.get(0x324, 0):x}")] += 1
                if sc == 5 and m == 0x40c:
                    sifm[(f"surface {s.get(0x198, 0):x}", f"dma {s.get(0x184, 0):x}", f"colour fmt {s.get(0x300, 0):x}",
                          f"op {s.get(0x304, 0):x}", f"clip {s.get(0x308, 0):x}/{s.get(0x30c, 0):x}",
                          f"out {s.get(0x310, 0):x}/{s.get(0x314, 0):x}", f"size {s.get(0x400, 0):x}",
                          f"fmt/pitch {s.get(0x404, 0):x}", f"in%128={s.get(0x408, 0) & 127}")] += 1
                if sc == 4 and m == 0x304:
                    surf[('swz', f"dma {s.get(0x184, 0):x}", f"format {s.get(0x300, 0):x}", f"offset%128={v & 127}")] += 1
                if sc == 3 and m == 0x30c:
                    surf[('sf2d', f"dma {s.get(0x184, 0):x}/{s.get(0x188, 0):x}", f"format {s.get(0x300, 0):x}",
                          f"pitch {s.get(0x304, 0):x}", f"src%128={s.get(0x308, 0) & 127} dst%128={v & 127}")] += 1
            i += 1 + cnt

def show(title, c):
    print(f'== {title} ({sum(c.values())})')
    for k, v in sorted(c.items(), key=lambda kv: -kv[1]):
        print(f'{v:8d}  ' + '  '.join(str(x) for x in k if x != ''))
print('== methods')
for (sc, m), v in sorted(methods.items()):
    print(f'{v:8d}  [{sc}] {m:04x} {name(sc, m)}')
show('render targets at draws and clears', rts)
show('enabled texture units and vertex formats at draws', draws)
show('M2MF copies', copies)
show('SIFM copies', sifm)
show('surfaces', surf)
