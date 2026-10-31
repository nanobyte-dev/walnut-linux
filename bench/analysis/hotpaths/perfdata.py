#!/usr/bin/env python3
"""Minimal perf.data (PERFILE2) reader: samples with call chains, mmaps, comms.
Handles big-endian files written on the PS3. Writes a compact pickle per run:
  {'samples': [(pid, tid, time, period, (ip...))], 'mmaps': {pid: [(start, end, pgoff, name, buildid)]},
   'comm': {tid: name}, 'targets': ...}
"""
import struct, sys, pickle, collections

SAMPLE_BITS = ['IP', 'TID', 'TIME', 'ADDR', 'READ', 'CALLCHAIN', 'ID', 'CPU', 'PERIOD', 'STREAM_ID',
               'RAW', 'BRANCH_STACK', 'REGS_USER', 'STACK_USER', 'WEIGHT', 'DATA_SRC', 'IDENTIFIER']


def read(path):
    with open(path, 'rb') as f:
        buf = f.read()
    magic = buf[:8]
    if magic == b'PERFILE2':
        e = '<'
    elif magic == b'2ELIFREP':
        e = '>'
    else:
        raise SystemExit('bad magic %r' % magic)
    size, attr_size, a_off, a_size, d_off, d_size = struct.unpack_from(e + 'QQQQQQ', buf, 8)
    attrs = []
    for off in range(a_off, a_off + a_size, attr_size):
        typ, asz, config, period, stype, rfmt, flags = struct.unpack_from(e + 'IIQQQQQ', buf, off)
        attrs.append((typ, config, stype, rfmt, flags))
    stype = attrs[0][2]
    flags = attrs[0][4]
    has = {n: bool(stype & (1 << i)) for i, n in enumerate(SAMPLE_BITS)}
    if has['READ'] or has['BRANCH_STACK'] or has['REGS_USER'] or has['STACK_USER'] or has['RAW']:
        raise SystemExit('unsupported sample_type %x' % stype)
    samples = []
    mmaps = collections.defaultdict(list)
    comm = {}
    pos, end = d_off, d_off + d_size
    hdr = struct.Struct(e + 'IHH')
    while pos + 8 <= end:
        typ, misc, sz = hdr.unpack_from(buf, pos)
        if sz == 0:
            break
        body = pos + 8
        if typ == 9:  # SAMPLE
            p = body
            if has['IDENTIFIER']:
                p += 8
            if has['IP']:
                p += 8
            pid = tid = 0
            if has['TID']:
                pid, tid = struct.unpack_from(e + 'II', buf, p); p += 8
            t = 0
            if has['TIME']:
                t, = struct.unpack_from(e + 'Q', buf, p); p += 8
            if has['ADDR']:
                p += 8
            if has['ID']:
                p += 8
            if has['STREAM_ID']:
                p += 8
            if has['CPU']:
                p += 8
            per = 1
            if has['PERIOD']:
                per, = struct.unpack_from(e + 'Q', buf, p); p += 8
            ips = ()
            if has['CALLCHAIN']:
                nr, = struct.unpack_from(e + 'Q', buf, p); p += 8
                ips = struct.unpack_from(e + '%dQ' % nr, buf, p)
            samples.append((pid, tid, t, per, ips))
        elif typ == 1:  # MMAP
            pid, tid, addr, ln, pgoff = struct.unpack_from(e + 'IIQQQ', buf, body)
            name = buf[body + 32:pos + sz].split(b'\0', 1)[0].decode('utf-8', 'replace')
            mmaps[pid].append((addr, addr + ln, pgoff, name, None))
        elif typ == 10:  # MMAP2
            pid, tid, addr, ln, pgoff = struct.unpack_from(e + 'IIQQQ', buf, body)
            p = body + 32
            bid = None
            if misc & (1 << 14):
                bsz = buf[p]
                bid = buf[p + 4:p + 4 + bsz].hex()
            p += 24 + 8  # maj/min/ino/gen or build id, then prot, flags
            name = buf[p:pos + sz].split(b'\0', 1)[0].decode('utf-8', 'replace')
            mmaps[pid].append((addr, addr + ln, pgoff, name, bid))
        elif typ == 3:  # COMM
            pid, tid = struct.unpack_from(e + 'II', buf, body)
            name = buf[body + 8:pos + sz].split(b'\0', 1)[0].decode('utf-8', 'replace')
            comm[tid] = name
        elif typ == 81 or typ == 83:
            raise SystemExit('compressed perf.data not supported')
        pos += sz
    return {'endian': e, 'sample_type': stype, 'samples': samples, 'mmaps': dict(mmaps), 'comm': comm}


if __name__ == '__main__':
    d = read(sys.argv[1])
    print('endian', d['endian'], 'stype %x' % d['sample_type'], 'samples', len(d['samples']),
          'mmap pids', {k: len(v) for k, v in d['mmaps'].items()})
    if len(sys.argv) > 2:
        with open(sys.argv[2], 'wb') as f:
            pickle.dump(d, f, protocol=4)
