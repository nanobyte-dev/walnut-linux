#!/usr/bin/env python3
"""vm-outside.py MAP A B (vm-e2e.sh): every device sector outside the image's mapped part (image
sectors 2048 on, the part /dev/mapper/* covers) is the same in the disk images A and B; prints how
many mapped ranges differ. Exit 1 with the first differing range outside."""
import json
import sys

m = json.load(open(sys.argv[1]))
inside = sorted((p + max(l, 2048) - l, p + n) for l, p, n in m["extents"] if max(l, 2048) < l + n)
a, b = open(sys.argv[2], "rb"), open(sys.argv[3], "rb")
end = a.seek(0, 2) // 512
if b.seek(0, 2) // 512 != end:
    sys.exit("the images differ in size")


def same(x, y):
    a.seek(x * 512)
    b.seek(x * 512)
    while x < y:
        k = min(2048, y - x)
        if a.read(k * 512) != b.read(k * 512):
            sys.exit("sector %d..%d differs outside the image" % (x, x + k))
        x += k


pos, changed = 0, 0
for x, y in inside:
    same(pos, x)
    a.seek(x * 512)
    b.seek(x * 512)
    changed += a.read((y - x) * 512) != b.read((y - x) * 512)
    pos = y
same(pos, end)
print(changed)
