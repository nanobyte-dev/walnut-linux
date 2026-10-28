#!/usr/bin/env python3
# to-fixture.py push.bin NAME SUBMIT[,SUBMIT...]: the pushes of those submits as one C array
import struct, sys
data = open(sys.argv[1], 'rb').read()
name, subs = sys.argv[2], [int(x) for x in sys.argv[3].split(',')]
out, p = [], 0
while p < len(data):
    _, sub, idx, n = struct.unpack('>4I', data[p:p + 16]); p += 16
    w = struct.unpack(f'>{n}I', data[p:p + 4 * n]); p += 4 * n
    if sub in subs:
        out += w
print(f'static const u32 {name}[{len(out)}] = {{')
for i in range(0, len(out), 8):
    print('\t' + ', '.join(f'0x{v:08x}' for v in out[i:i + 8]) + ',')
print('};')
