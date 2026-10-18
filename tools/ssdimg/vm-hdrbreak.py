#!/usr/bin/env python3
"""vm-hdrbreak.py MAP DISK (vm-e2e.sh): zeroes the image's header block 5 (image byte 20480) in the
disk image DISK, where MAP's extents put it, for the refused boot "a header block no longer stamped"."""
import json
import sys

m = json.load(open(sys.argv[1]))
l, p, n = next(e for e in m["extents"] if e[0] <= 40 < e[0] + e[2])
with open(sys.argv[2], "r+b") as f:
    f.seek((p + 40 - l) * 512)
    f.write(bytes(4096))
