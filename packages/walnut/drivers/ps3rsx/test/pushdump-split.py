#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Split a ps3rsx push dump (debugfs ps3rsx_push_dump of a pushdump test build; the record
format is src/pushdump_scan.h) into the host tools' inputs:

  chan<N>-s<S>.push  every push channel N (serial S: ids are reused) submitted, rejected submits included, in mesa-shim's
                   layout ('PUSH', submit, push index, word count, words): validate-capture's input,
                   with a 'CHAN' record (the channel id) first and its file's buffers as 'BOEV'
                   records (open or close, domain, offset, size) where they came and went
  chan<N>-s<S>.gpu   what reached the GPU from that channel: the accepted pushes and ps3rsx's own ENDs
                   (push index 0xffff), for validate-capture --gpu and --state (with 'CHAN')
  submits.tsv      one line per submit: seq, channel, serial, owner, tgid, comm, verdict, bad push,
                   pushes, buffers
  bos.tsv          each submit's buffers: seq, channel, handle, kind, domain, offset, size, read
                   and write domains
  set.tsv          the files' buffer sets: open/close, seq, owner, tgid, kind, domain, offset, size
  contents.tsv     what the accepted pushes fetched: seq, channel, what, domain, address, length
                   asked, status, length read, file
  contents/        the bytes, one file each
  summary.txt      counts, gaps (records the kernel dropped) and the address-space facts

Run with python3 -I: pushdump-split.py DUMP OUTDIR
"""
import os
import struct
import sys

MAGIC = 0x50344452
TYPES = {1: "START", 2: "OPEN", 3: "CLOSE", 4: "SUBMIT", 5: "RESTORE", 6: "PUSH", 7: "END",
         8: "CONTENT", 9: "GAP"}
KINDS = {0: "vram", 1: "vram-gpu", 2: "gart", 3: "reports"}
DOMS = {1: "vram", 2: "gart"}
WHAT = {1: "fp", 2: "index"}
STATUS = {1: "read", 2: "read-early", 3: "unreadable", 4: "no-bo", 5: "cut"}
VERDICT = {0: "accepted", 1: "rejected", 2: "not-sent"}
PUSH_MAGIC = 0x50555348
CHAN_MAGIC = 0x4348414e
BOEV_MAGIC = 0x424f4556


def words(b, off, n):
    return struct.unpack_from(">%dI" % n, b, off)


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    data = open(argv[1], "rb").read()
    out = argv[2]
    os.makedirs(os.path.join(out, "contents"), exist_ok=True)
    push_files, gpu_files, nsub = {}, {}, {}
    counts = {t: 0 for t in TYPES.values()}
    gaps = dropped = 0
    start = None
    tsv = {name: open(os.path.join(out, name + ".tsv"), "w")
           for name in ("submits", "bos", "set", "contents")}

    owner_of = {}           # (channel, serial) -> its file's owner id (from its submits)
    serial_of = {}          # channel id -> the serial of its current channel
    events = {}             # owner -> [(open, dom, offset, size)] so far, for a new channel file

    def boev(f, ev):
        f.write(struct.pack(">7I", BOEV_MAGIC, ev[0], ev[1], 3, ev[2] >> 32, ev[2] & 0xffffffff,
                            ev[3]))

    def stream(files, kind, key):
        if key not in files:
            files[key] = open(os.path.join(out, "chan%d-s%d.%s" % (key[0], key[1], kind)), "wb")
            files[key].write(struct.pack(">5I", CHAN_MAGIC, 0, 0, 1, key[0]))
            if kind == "push":
                for ev in events.get(owner_of.get(key), []):
                    boev(files[key], ev)
        return files[key]

    def put(f, sub, idx, ws):
        f.write(struct.pack(">4I", PUSH_MAGIC, sub, idx, len(ws)))
        f.write(struct.pack(">%dI" % len(ws), *ws))

    cur = None              # (channel, verdict, seq) of the last SUBMIT, its pushes follow
    off = 0
    while off + 16 <= len(data):
        magic, typ, n, seq = words(data, off, 4)
        if magic != MAGIC or n < 4 or off + 4 * n > len(data):
            sys.stderr.write("bad record at byte %d\n" % off)
            return 1
        name = TYPES.get(typ, "?")
        counts[name] = counts.get(name, 0) + 1
        body = off + 16
        if name == "START":
            start = words(data, body, 6)
        elif name in ("OPEN", "CLOSE"):
            owner, tgid, kind, dom, hi, lo, size = words(data, body, 7)
            if kind != 3 and dom in (1, 2):     # the reports BO is in no set
                ev = (1 if name == "OPEN" else 0, dom - 1, hi << 32 | lo, size)
                events.setdefault(owner, []).append(ev)
                for key, o in owner_of.items():
                    if o == owner and key in push_files:
                        boev(push_files[key], ev)
            tsv["set"].write("%s\t%#x\t%d\t%d\t%s\t%s\t%#x\t%#x\n" % (
                name.lower(), seq, owner, tgid, KINDS.get(kind, kind), DOMS.get(dom, dom),
                hi << 32 | lo, size))
        elif name == "SUBMIT":
            ch, serial, owner, tgid, verdict, bad, npush, nbuf = words(data, body, 8)
            comm = data[body + 32:body + 48].split(b"\0")[0].decode("latin-1")
            key = (ch, serial)
            nsub[key] = nsub.get(key, 0) + 1
            owner_of[key] = owner
            serial_of[ch] = serial
            cur = (key, verdict, nsub[key])
            tsv["submits"].write("%#x\t%d\t%d\t%d\t%d\t%s\t%s\t%s\t%d\t%d\n" % (
                seq, ch, serial, owner, tgid, comm, VERDICT.get(verdict, verdict),
                "-" if bad == 0xffffffff else bad, npush, nbuf))
            for i in range(nbuf):
                h, kind, dom, boff, size, rd, wr = words(data, body + 48 + 28 * i, 7)
                tsv["bos"].write("%#x\t%d\t%d\t%s\t%s\t%#x\t%#x\t%#x\t%#x\n" % (
                    seq, ch, h, KINDS.get(kind, kind), DOMS.get(dom, dom), boff, size, rd, wr))
        elif name == "PUSH":
            ch, idx, k = words(data, body, 3)
            ws = words(data, body + 12, k)
            if cur is None or cur[0][0] != ch:
                sys.stderr.write("push without its submit at byte %d\n" % off)
                return 1
            put(stream(push_files, "push", cur[0]), cur[2], idx, ws)
            if cur[1] == 0:
                put(stream(gpu_files, "gpu", cur[0]), cur[2], idx, ws)
        elif name == "END":
            ch, h, d = words(data, body, 3)
            key = (ch, serial_of.get(ch, 0))
            put(stream(gpu_files, "gpu", key), nsub.get(key, 0), 0xffff, (h, d))
        elif name == "CONTENT":
            ch, what, dom, addr, want, status, got = words(data, body, 7)
            fn = "-"
            if got:
                fn = "contents/%08x-c%d-%s-%s-%08x.bin" % (seq, ch, WHAT.get(what, what),
                                                         DOMS.get(dom, dom), addr)
                with open(os.path.join(out, fn), "wb") as f:
                    f.write(data[body + 28:body + 28 + got])
            tsv["contents"].write("%#x\t%d\t%s\t%s\t%#x\t%#x\t%s\t%#x\t%s\n" % (
                seq, ch, WHAT.get(what, what), DOMS.get(dom, dom), addr, want,
                STATUS.get(status, status), got, fn))
        elif name == "GAP":
            hi, lo = words(data, body, 2)
            gaps += 1
            dropped += hi << 32 | lo
            cur = None      # a submit's pushes may be among the lost
        off += 4 * n
    if off != len(data):
        sys.stderr.write("%d trailing bytes (a cut record)\n" % (len(data) - off))
    for f in list(push_files.values()) + list(gpu_files.values()) + list(tsv.values()):
        f.close()
    with open(os.path.join(out, "summary.txt"), "w") as f:
        if start:
            f.write("version %d, vram reserved %#x, memory_size %#x, gart_size %#x, "
                    "cpu vram limit %#x, ring reserve %#x\n" % start)
        for t, c in counts.items():
            f.write("%s %d\n" % (t.lower(), c))
        f.write("gaps %d, records dropped %d\n" % (gaps, dropped))
        f.write("channels %s\n" % " ".join("%d-s%d" % k for k in sorted(push_files)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
