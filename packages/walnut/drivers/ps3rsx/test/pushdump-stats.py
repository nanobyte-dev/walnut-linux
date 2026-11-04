#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""The numbers the address checks' next slices need from a split push dump (pushdump-split.py's
output directory):

  index    indexed fetches (VB_INDEX_BATCH ranges, merged per submit) by where the index buffer
           lives: GART, CPU-visible VRAM, GPU-only VRAM (the CPU can't read it); count and bytes
  fp       fragment program binds (0x8e4) by the same placement, distinct programs, and for every
           program the dump could read: its length up to END (inline constants included), the
           longest, and whether it uses CAL or RET
  no-bo    fetches from a buffer missing from the submit's own list

With FRAMES (the harness's frame count for the run) every count is also given per frame.
Run with python3 -I: pushdump-stats.py SPLIT_DIR [FRAMES]
"""
import collections
import os
import struct
import sys


def fp_scan(data):
    """(bytes to the END instruction's end, uses CAL/RET), or (None, ...) without an END. The RSX
    keeps program words with their halfwords swapped; each instruction is 16 bytes, followed by
    16 bytes of constant when one of its sources is a constant (type 2)."""
    n = len(data) // 4
    w = [((v >> 16) | (v << 16)) & 0xffffffff for v in struct.unpack(">%dI" % n, data[:4 * n])]
    i, calret = 0, False
    while i + 4 <= n:
        op = (w[i] >> 24) & 0x3f
        if (w[i + 2] >> 31) & 1 and op in (1, 5):
            calret = True
        const = any(w[i + k] & 3 == 2 for k in (1, 2, 3))
        step = 8 if const else 4
        if w[i] & 1:
            return 4 * min(i + step, n), calret
        i += step
    return None, calret


def main(argv):
    if len(argv) not in (2, 3):
        sys.stderr.write(__doc__)
        return 2
    d = argv[1]
    frames = int(argv[2]) if len(argv) == 3 else 0
    kinds = {}          # (seq, dom, offset) -> kind, from bos.tsv
    for line in open(os.path.join(d, "bos.tsv")):
        seq, ch, h, kind, dom, off, size, rd, wr = line.rstrip("\n").split("\t")
        kinds.setdefault((dom, int(off, 16), int(size, 16)), kind)

    def place(dom, addr, status):
        if dom == "gart":
            return "gart"
        if status == "unreadable":
            return "vram-gpu"
        for (kd, off, size), kind in kinds.items():
            if kd == dom and off <= addr < off + size:
                return kind
        return "vram"

    count = collections.Counter()
    nbytes = collections.Counter()
    progs, lengths, calret, noend = set(), [], 0, 0
    for line in open(os.path.join(d, "contents.tsv")):
        seq, ch, what, dom, addr, want, status, got, fn = line.rstrip("\n").split("\t")
        addr, want = int(addr, 16), int(want, 16)
        if status == "no-bo":
            count[("no-bo", what)] += 1
            continue
        p = place(dom, addr, status)
        count[(what, p)] += 1
        if what == "index":
            nbytes[(what, p)] += want
        else:
            progs.add((dom, addr))
            if fn != "-":
                ln, cr = fp_scan(open(os.path.join(d, fn), "rb").read())
                calret += cr
                if ln is None:
                    noend += 1
                else:
                    lengths.append(ln)

    def per(v):
        return " (%.1f per frame)" % (v / frames) if frames else ""

    for (what, p), c in sorted(count.items()):
        b = nbytes.get((what, p))
        print("%-6s %-9s %8d%s%s" % (what, p, c, per(c),
                                     "  %d bytes%s" % (b, per(b)) if b is not None else ""))
    print("fp     distinct programs %d" % len(progs))
    if lengths:
        lengths.sort()
        print("fp     length to END: %d read, median %d, longest %d bytes; no END in the bytes read: "
              "%d; CAL/RET: %d" % (len(lengths), lengths[len(lengths) // 2], lengths[-1], noend,
                                   calret))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
