#!/usr/bin/env python3
import sys, subprocess, array, os
d = sys.argv[1]; fr = int(sys.argv[2]) if len(sys.argv) > 2 else 50
exe = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'src/sauer_client')
names = open(d + '/glc.names').read().split('\n')
marks = {l.split()[0]: (int(l.split()[1]), int(l.split()[2])) for l in open(d + '/glc.marks') if l.strip()}
fl = [int(x) for x in open(d + '/glc.frames')]
a = array.array('Q'); a.frombytes(open(d + '/glc.log', 'rb').read())
f0 = marks['start'][1] + fr
seg = a[fl[f0 - 1]:fl[f0]]
base = [int(l.split('-')[0], 16) for l in open(d + '/glc.maps') if l.rstrip().endswith('sauer_client')][0]
rets = sorted({v & ((1 << 48) - 1) for v in seg})
out = subprocess.run(['addr2line', '-a', '-f', '-C', '-e', exe] + [hex(r - base) for r in rets], capture_output=True, text=True).stdout.split('\n')
loc = {}
for k in range(0, len(out) - 2, 3):
    loc[int(out[k], 16) + base] = out[k + 1].split('(')[0] + ':' + out[k + 2].replace('/src/', '').split(' ')[0].split('/')[-1]
prev = None; n = 0
def flush():
    if prev: print(f'{n:4d} {prev}')
for v in seg:
    s = f'{names[v>>48]} {loc.get(v & ((1<<48)-1), "?")}'
    if s == prev: n += 1
    else:
        flush(); prev = s; n = 1
flush()
