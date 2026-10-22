#!/usr/bin/env python3
"""Host tests of the SSD test drive's tools (tools/run-tests.sh suite ssdimg): stamp.py's format and
stream, ps3-imgmap's extent merging, unit conversion, mount and device checks, refusals, and its
map -> verify -> recheck -> create path on a fake UFS layer: FIBMAP and the super block are faked,
the "device" is a file in the scratch directory holding the stamped image's blocks where the fake
layout puts them (real O_DIRECT reads), dmsetup and blockdev are recording stubs. What the fake
can't show (the real kernel's FIBMAP unit on UFS2) is tools/ssdimg/vm-e2e.sh's job.
Scratch files under build/tmp/ssdimg/unit, removed at the end."""
import importlib.machinery
import importlib.util
import io
import json
import lzma
import os
import shutil
import subprocess
import sys
import unittest
import uuid
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SCRATCH = ROOT / "build" / "tmp" / "ssdimg" / "unit"
sys.path.insert(0, str(HERE))
import stamp  # noqa: E402


def load_imgmap(env):
    """ps3-imgmap as a module, its test seams from ENV (read at import)"""
    old = {k: os.environ.get(k) for k in env}
    os.environ.update(env)
    try:
        loader = importlib.machinery.SourceFileLoader("ps3_imgmap", str(HERE / "ps3-imgmap"))
        spec = importlib.util.spec_from_loader("ps3_imgmap", loader)
        m = importlib.util.module_from_spec(spec)
        loader.exec_module(m)
        return m
    finally:
        for k, v in old.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v


UID = uuid.UUID("12345678-9abc-def0-1122-334455667788")


class StampFormat(unittest.TestCase):
    def test_layout(self):
        b = stamp.block(UID.bytes, 3 * 4096)
        self.assertEqual(len(b), 4096)
        self.assertEqual(b[:8], b"PS3IMGv1")
        self.assertEqual(b[8:24], UID.bytes)
        self.assertEqual(b[24:32], (12288).to_bytes(8, "big"))
        self.assertEqual(int.from_bytes(b[32:36], "big"), zlib.crc32(b[:32]))
        self.assertEqual(b[36:], bytes(4060))
        self.assertEqual(stamp.parse_stamp(b), (True, UID.bytes, 12288, True))

    def test_crc_catches_a_flip(self):
        b = bytearray(stamp.block(UID.bytes, 0))
        b[30] ^= 1
        self.assertFalse(stamp.parse_stamp(bytes(b))[3])

    def test_offset_is_64bit(self):
        o = 900 * 2**30   # beyond 4 GiB and 2^32 blocks
        self.assertEqual(stamp.parse_stamp(stamp.block(UID.bytes, o))[2], o)

    def test_stream_and_chunks(self):
        whole = b"".join(stamp.chunks(UID.bytes, 3 * stamp.MIB))
        self.assertEqual(len(whole), 3 * stamp.MIB)
        s = stamp.Stream(UID.bytes, 3 * stamp.MIB)
        parts = []
        while True:
            x = s.read(100003)   # odd sizes, as storbinary may ask
            if not x:
                break
            parts.append(x)
        self.assertEqual(b"".join(parts), whole)
        self.assertEqual(s.done, 3 * stamp.MIB)

    def test_sizes(self):
        self.assertEqual(stamp.parse_size("16G"), 16 << 30)
        self.assertEqual(stamp.parse_size("16GiB"), 16 << 30)
        self.assertEqual(stamp.parse_size("1.5G"), 3 << 29)
        self.assertEqual(stamp.parse_size("512M"), 512 << 20)
        for bad in ("1.0000001G", "1M", "16", "16T", "x"):
            with self.assertRaises(ValueError, msg=bad):
                stamp.parse_size(bad)

    def test_dry_run_and_check(self):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        f = SCRATCH / "dry.img"
        r = subprocess.run([sys.executable, str(HERE / "stamp.py"), "dry-run", "--mib", "2", "--uuid", str(UID),
                            "--out", str(f)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(f.stat().st_size, 2 << 20)
        r = subprocess.run([sys.executable, str(HERE / "stamp.py"), "check", str(f), "--uuid", str(UID)],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout)
        with open(f, "r+b") as fh:   # a block moved: caught
            fh.seek(8192)
            fh.write(stamp.block(UID.bytes, 4096))
        r = subprocess.run([sys.executable, str(HERE / "stamp.py"), "check", str(f)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 1)
        self.assertIn("1 bad", r.stdout)
        f.unlink()

    def test_both_tools_agree(self):
        m = load_imgmap({})
        for o in (0, 4096, 1 << 20, 5 << 32):
            self.assertEqual(m.stamp(UID.bytes, o) + m.ZERO_TAIL, stamp.block(UID.bytes, o))
        whole = b"".join(stamp.chunks(UID.bytes, 64 * 4096))
        self.assertEqual(m.expected(UID.bytes, 0, len(whole)), whole)
        for a, n in ((0, 36), (1, 35), (512, 3584), (2048, 4096), (4090, 20), (8191, 9000), (100, 5)):
            self.assertEqual(m.expected(UID.bytes, a, n), whole[a:a + n], (a, n))


class Extents(unittest.TestCase):
    m = load_imgmap({})

    def test_merge(self):
        self.assertEqual(self.m.merge([(0, 100, 4), (4, 104, 4), (8, 200, 4), (12, 204, 4), (16, 108, 4)]),
                         [(0, 100, 8), (8, 200, 8), (16, 108, 4)])
        self.assertEqual(self.m.merge([]), [])
        # physically contiguous but a logical gap: not merged
        self.assertEqual(self.m.merge([(0, 10, 2), (4, 12, 2)]), [(0, 10, 2), (4, 12, 2)])

    def test_units(self):
        self.assertEqual(self.m.to_sectors([(0, 100, 8)], 2048), [(0, 400, 32)])
        self.assertEqual(self.m.to_sectors([(8, 3, 1)], 4096), [(64, 24, 8)])
        self.assertEqual(self.m.to_sectors([(1, 1, 1)], 512), [(1, 1, 1)])
        with self.assertRaises(self.m.Refuse):
            self.m.to_sectors([(0, 0, 1)], 1000)

    def test_check_extents(self):
        ok = [(0, 1000, 8), (8, 2000, 8)]
        self.m.check_extents(ok, 16 * 512, 10000)
        for bad, why in (([(0, 1000, 8), (9, 2000, 7)], "gap"), ([(0, 1000, 8), (8, 1004, 8)], "overlap"),
                         ([(0, 10, 16)], "in the super block"), ([(0, 9990, 16)], "past the end"),
                         ([(0, 1000, 8)], "short")):
            with self.assertRaises(self.m.Refuse, msg=why):
                self.m.check_extents(bad, 16 * 512, 10000)

    def test_digest(self):
        a = self.m.digest(4096, str(UID), [(0, 8, 8)])
        self.assertEqual(a, self.m.digest(4096, str(UID), [(0, 8, 8)]))
        self.assertNotEqual(a, self.m.digest(4096, str(UID), [(0, 16, 8)]))
        self.assertNotEqual(a, self.m.digest(8192, str(UID), [(0, 8, 8)]))


class FakeUFS:
    """a file's layout in fragments, as FIBMAP would answer: logical block -> physical fragment"""

    def __init__(self, fsize, bsize, nblocks, gaps=(), hole=None, skew=None):
        self.fsize, self.bsize, self.fpb = fsize, bsize, bsize // fsize
        self.phys, p = {}, 1000 * self.fpb   # data area well past the super block
        for b in range(nblocks):
            if b in gaps:   # an indirect block (or another file) between data blocks
                p += self.fpb * 3
            self.phys[b] = p
            p += self.fpb
        self.hole, self.skew, self.calls = hole, skew, 0

    def fibmap(self, fd, n):
        self.calls += 1
        b, i = divmod(n, self.fpb)
        if b == self.hole:
            return 0
        if self.skew is not None and b == self.skew and i == self.fpb - 1:
            return 7
        return self.phys[b] + i


class Pipeline(unittest.TestCase):
    """map -> verify -> recheck -> create on the fake layer"""

    def setUp(self):
        self.d = SCRATCH / self._testMethodName
        shutil.rmtree(self.d, ignore_errors=True)
        (self.d / "mnt").mkdir(parents=True)
        self.state = self.d / "state"
        self.dev = self.d / "ps3db.img"
        self.whole = self.d / "ps3da"
        self.whole.write_bytes(b"")
        self.sysb = self.d / "sys"
        (self.sysb / "ps3da").mkdir(parents=True)
        (self.sysb / "ps3da" / "ro").write_text("0\n")
        self.bootid = self.d / "boot_id"
        self.bootid.write_text("boot-1\n")
        self.log = self.d / "cmds.log"
        self.file = self.d / "mnt" / "ssdtest.img"
        self.file.write_bytes(b"")
        st = os.stat(self.file)
        tables = self.d / "tables"
        tables.mkdir()
        # dmsetup create keeps the table (logged too), dmsetup table gives it back as the kernel would,
        # with the device as MAJ:MIN
        stub = self.d / "stub"
        stub.write_text('#!/bin/sh\necho "$(basename "$0") $*" >> %s\n'
                        '[ "$1" = --setro ] && echo 1 > %s\n'
                        '[ "$1" = create ] && tee %s/"$2" >> %s\n'
                        '[ "$1" = table ] && sed "s| %s | %d:%d |" %s/"$2"\n'
                        '[ "$1" = remove ] && rm -f %s/"$2"\nexit 0\n' % (
                            self.log, self.sysb / "ps3da" / "ro", tables, self.log, self.dev,
                            os.major(st.st_dev), os.minor(st.st_dev), tables, tables))
        stub.chmod(0o755)
        (self.d / "dmsetup").symlink_to(stub)
        (self.d / "blockdev").symlink_to(stub)
        self.file = self.d / "mnt" / "ssdtest.img"
        self.size = 4 << 20
        self.file.write_bytes(b"".join(stamp.chunks(UID.bytes, self.size)))
        self.fsize = os.statvfs(self.file).f_bsize   # statvfs of the scratch disk stands in for UFS's
        self.bsize = max(self.fsize * 4, 16384)
        self.minfo = self.d / "mountinfo"
        self.mount_line("ro,noatime", "ro,ufstype=ufs2")
        self.m = load_imgmap({"PS3_IMGMAP_TEST_DEVICE": str(self.dev), "PS3_IMGMAP_TEST_WHOLE": str(self.whole),
                              "PS3_IMGMAP_STATE": str(self.state), "PS3_IMGMAP_MOUNTINFO": str(self.minfo),
                              "PS3_IMGMAP_DMSETUP": str(self.d / "dmsetup"),
                              "PS3_IMGMAP_BLOCKDEV": str(self.d / "blockdev"),
                              "PS3_IMGMAP_SYSBLOCK": str(self.sysb), "PS3_IMGMAP_BOOT_ID_FILE": str(self.bootid)})
        st = os.stat(self.file)
        self.fdev = "%d:%d" % (os.major(st.st_dev), os.minor(st.st_dev))
        self.devnum = self.fdev
        self.m.devnum = lambda p: self.devnum if p == str(self.dev) else "0:0"
        self.ufs = FakeUFS(self.fsize, self.bsize, self.size // self.bsize, gaps={3, 40, 41})
        self.m.fibmap = self.ufs.fibmap
        self.m.figetbsz = lambda fd: self.fsize
        self.m.ufs_super = lambda p: (self.bsize, self.fsize, "big-endian")
        self.write_device()

    def tearDown(self):
        shutil.rmtree(self.d, ignore_errors=True)

    def mount_line(self, mopts, sopts, fstype="ufs", source=None):
        st = os.stat(self.file)
        dev = "%d:%d" % (os.major(st.st_dev), os.minor(st.st_dev))
        self.minfo.write_text("22 1 %s / %s %s - %s %s %s\n" % (
            dev, self.d / "mnt", mopts, fstype, source or "/dev/ps3db", sopts))

    def write_device(self):
        """the fake disk: zeros, the image's blocks where the fake layout puts them"""
        data = self.file.read_bytes()
        end = (max(self.ufs.phys.values()) + self.ufs.fpb + 64) * self.fsize
        with open(self.dev, "wb") as f:
            f.truncate(end)
            for b, p in self.ufs.phys.items():
                f.seek(p * self.fsize)
                f.write(data[b * self.bsize:(b + 1) * self.bsize])

    def cli(self, *args):
        out, err = io.StringIO(), io.StringIO()
        so, se = sys.stdout, sys.stderr
        sys.stdout, sys.stderr = out, err
        try:
            rc = self.m.main(list(args))
        finally:
            sys.stdout, sys.stderr = so, se
        return rc, out.getvalue() + err.getvalue()

    def mapfile(self):
        return str(self.state / "ssdtest.map.json")

    def test_full_path(self):
        rc, out = self.cli("map", str(self.file))
        self.assertEqual(rc, 0, out)
        mp = json.loads(Path(self.mapfile()).read_text())
        k = self.fsize // 512
        self.assertEqual(mp["uuid"], str(UID))
        self.assertEqual(len(mp["extents"]), 4, mp["extents"])   # split at the gaps before blocks 3, 40 and 41
        self.assertEqual(mp["extents"][0], [0, 1000 * self.ufs.fpb * k, 3 * self.ufs.fpb * k])
        self.assertEqual(self.ufs.calls, 2 * (self.size // self.bsize))   # first and last fragment per block
        rc, out = self.cli("create", self.mapfile(), "ps3ssd", "--yes")
        self.assertEqual(rc, 2, out)
        self.assertIn("no verify pass", out)
        rc, out = self.cli("verify", self.mapfile())
        self.assertEqual(rc, 0, out)
        self.assertIn("all 1024 blocks right", out)
        rc, out = self.cli("create", self.mapfile(), "ps3ssd", "--yes")
        self.assertEqual(rc, 2, out)
        self.assertIn("no recheck in this boot", out)
        rc, out = self.cli("recheck", self.mapfile())
        self.assertEqual(rc, 0, out)
        rc, out = self.cli("create", self.mapfile(), "ps3ssd", "--yes")
        self.assertEqual(rc, 2, out)   # still mounted (the fake mountinfo has the device)
        self.assertIn("is mounted", out)
        self.minfo.write_text("")
        rc, out = self.cli("create", self.mapfile(), "ps3ssd", "--yes", "--swap-mib", "1")
        self.assertEqual(rc, 0, out)
        log = self.log.read_text().splitlines()
        self.assertEqual(log[0], "blockdev --setro %s" % self.whole)
        self.assertEqual(log[1], "dmsetup create ps3ssd")
        # the tables: image sectors 2048.. (after the header area) up to the swap, then the swap
        lines = [x.split() for x in log if " linear " in x]
        total = self.size // 512
        mapped = []
        for t in lines:
            start, n, _, dev, off = t
            self.assertEqual(dev, str(self.dev))
            mapped += range(int(off), int(off) + int(n))
        image = []   # device sectors of image sectors 2048..total, through the map
        for l, p, n in mp["extents"]:
            image += [p + i for i in range(n) if 2048 <= l + i < total]
        self.assertEqual(mapped, image)   # exactly the image after its header, in order, nothing else
        self.assertIn("dmsetup create ps3ssd-swap", log)
        rc, out = self.cli("verify", self.mapfile())
        self.assertEqual(rc, 2, out)   # in use: verify refused, the pass is kept
        self.assertTrue(Path(self.m.record("verified", mp["extents_sha256"])).exists())
        # a new boot: create needs a new recheck
        self.bootid.write_text("boot-2\n")
        rc, out = self.cli("create", self.mapfile(), "ps3ssd2", "--yes")
        self.assertEqual(rc, 2, out)
        self.assertIn("no recheck in this boot", out)

    def test_verify_finds_a_bad_block(self):
        self.assertEqual(self.cli("map", str(self.file))[0], 0)
        with open(self.dev, "r+b") as f:   # image block 5 (byte 20480) holds block 6's stamp
            mp = json.loads(Path(self.mapfile()).read_text())
            l, p, n = mp["extents"][0]
            f.seek(p * 512 + 5 * 4096)
            f.write(stamp.block(UID.bytes, 6 * 4096))
        rc, out = self.cli("verify", self.mapfile())
        self.assertEqual(rc, 1, out)
        self.assertIn("image byte 20480", out)
        self.assertIn("off by +4096", out)
        self.assertIn("1 of 1024 blocks wrong", out)
        self.assertFalse(Path(self.m.record("verified", mp["extents_sha256"])).exists())

    def test_recheck_catches_a_moved_file(self):
        self.assertEqual(self.cli("map", str(self.file))[0], 0)
        self.assertEqual(self.cli("verify", self.mapfile())[0], 0)
        mp = json.loads(Path(self.mapfile()).read_text())
        self.ufs.phys[10] += 10 ** 4   # GameOS moved one block
        rc, out = self.cli("recheck", self.mapfile())
        self.assertEqual(rc, 1, out)
        self.assertIn("CHANGED", out)
        self.assertFalse(Path(self.m.record("verified", mp["extents_sha256"])).exists())

    def test_refusals(self):
        self.ufs.hole = 7
        rc, out = self.cli("map", str(self.file))
        self.assertEqual(rc, 2)
        self.assertIn("hole at byte %d" % (7 * self.bsize), out)
        self.ufs.hole, self.ufs.skew = None, 2
        rc, out = self.cli("map", str(self.file))
        self.assertEqual(rc, 2)
        self.assertIn("not contiguous", out)
        self.ufs.skew = None
        for mopts, sopts, fstype, src, why in (
                ("rw,noatime", "rw,ufstype=ufs2", "ufs", None, "not mounted read-only"),
                ("ro", "rw,ufstype=ufs2", "ufs", None, "not mounted read-only"),
                ("ro", "ro", "ufs", None, "ufstype=ufs2"),
                ("ro", "ro", "ext4", None, "not GameOS's UFS2")):
            self.mount_line(mopts, sopts, fstype, src)
            rc, out = self.cli("map", str(self.file))
            self.assertEqual(rc, 2, (why, out))
            self.assertIn(why, out)
        self.mount_line("ro,noatime", "ro,ufstype=ufs2")
        self.devnum = "8:3"   # the mount is on another device
        rc, out = self.cli("map", str(self.file))
        self.assertEqual(rc, 2)
        self.assertIn("is not %s" % self.dev, out)
        self.devnum = self.fdev
        self.m.figetbsz = lambda fd: self.fsize * 2   # the unit cross-check
        rc, out = self.cli("map", str(self.file))
        self.assertEqual(rc, 2)
        self.assertIn("unit assumption", out)
        self.m.figetbsz = lambda fd: self.fsize
        with open(self.file, "r+b") as f:   # not a stamped image
            f.write(bytes(4096))
        rc, out = self.cli("map", str(self.file))
        self.assertEqual(rc, 2)
        self.assertIn("not a stamped image", out)

    def test_tail_fragment(self):
        with open(self.file, "ab") as f:
            f.write(stamp.block(UID.bytes, self.size))
        rc, out = self.cli("map", str(self.file))
        self.assertEqual(rc, 2)
        self.assertIn("tail fragment", out)

    def test_edited_map(self):
        self.assertEqual(self.cli("map", str(self.file))[0], 0)
        mp = json.loads(Path(self.mapfile()).read_text())
        mp["extents"][0][1] += 8
        Path(self.mapfile()).write_text(json.dumps(mp))
        rc, out = self.cli("verify", self.mapfile())
        self.assertEqual(rc, 2)
        self.assertIn("doesn't match its extents", out)

    def test_header_check_at_create(self):
        """create re-reads the header area through the map: a stale map whose blocks now hold
        something else is refused even with its records in place"""
        self.assertEqual(self.cli("map", str(self.file))[0], 0)
        self.assertEqual(self.cli("verify", self.mapfile())[0], 0)
        self.assertEqual(self.cli("recheck", self.mapfile())[0], 0)
        self.minfo.write_text("")
        mp = json.loads(Path(self.mapfile()).read_text())
        with open(self.dev, "r+b") as f:
            f.seek(mp["extents"][0][1] * 512 + 4096 * 100)
            f.write(bytes(4096))
        rc, out = self.cli("create", self.mapfile(), "ps3ssd", "--yes")
        self.assertEqual(rc, 2, out)
        self.assertIn("header area", out)
        self.assertFalse(self.log.exists())   # nothing ran: no setro, no dmsetup


def build_boot_tool():
    """ps3-imgmap-boot.c with the host's gcc (the pure parts: check, tables); None without gcc"""
    if not shutil.which("gcc"):
        return None
    out = SCRATCH / "ps3-imgmap-boot"
    if not out.exists():
        SCRATCH.mkdir(parents=True, exist_ok=True)
        subprocess.run(["gcc", "-O2", "-Wall", "-Wextra", "-Werror", "-o", str(out), str(HERE / "ps3-imgmap-boot.c")],
                       check=True)
    return out


class BootMap(Pipeline):
    """export-boot and ps3-imgmap-boot (C): the same dm tables as create, and every refusal of a bad map"""

    def setUp(self):
        super().setUp()
        self.tool = build_boot_tool()
        if not self.tool:
            self.skipTest("no gcc")

    def made(self, swap="1"):
        self.assertEqual(self.cli("map", str(self.file))[0], 0)
        self.assertEqual(self.cli("verify", self.mapfile())[0], 0)
        self.assertEqual(self.cli("recheck", self.mapfile())[0], 0)
        self.minfo.write_text("")
        rc, out = self.cli("create", self.mapfile(), "ps3ssd", "--yes", "--swap-mib", swap)
        self.assertEqual(rc, 0, out)

    def boot(self, *args):
        r = subprocess.run([str(self.tool)] + [str(a) for a in args], capture_output=True, text=True)
        return r.returncode, r.stdout, r.stderr

    def export(self, *extra):
        out = self.d / "boot.map"
        rc, o = self.cli("export-boot", self.mapfile(), "--out", str(out), *extra)
        return rc, o, out

    def test_same_tables_as_create(self):
        self.made()
        rc, o, bm = self.export()
        self.assertEqual(rc, 0, o)
        self.assertIn("swap 1 MiB", o)
        rc, out, err = self.boot("check", bm)
        self.assertEqual(rc, 0, err)
        self.assertIn("root 2 MiB, swap 1 MiB", out)   # 4 MiB image: 1 MiB header, 2 MiB root, 1 MiB swap
        for part, name in (("root", "ps3ssd"), ("swap", "ps3ssd-swap")):
            rc, out, err = self.boot("tables", bm, part)
            self.assertEqual(rc, 0, err)
            self.assertEqual(out, (self.state / (name + ".table")).read_text(), part)
            self.assertIn(" linear %s " % self.dev, out)
        # the digest is ps3-imgmap's: the C side recomputes it (a mismatch would have refused above)
        text = bm.read_text()
        self.assertTrue(text.startswith("ps3-imgmap-boot 1\nimage /ssdtest.img\n"), text[:80])

    def test_bigger_swap_and_split_extents(self):
        """an 8 MiB image, 3 MiB of swap: both tables cross several extents"""
        self.size = 8 << 20
        self.file.write_bytes(b"".join(stamp.chunks(UID.bytes, self.size)))
        self.bsize = self.fsize * 4
        nb = self.size // self.bsize
        self.ufs = type(self.ufs)(self.fsize, self.bsize, nb, gaps={3, nb // 3, nb * 2 // 3 + 5, nb * 2 // 3 + 6, nb - 7})
        self.m.fibmap = self.ufs.fibmap
        self.m.ufs_super = lambda p: (self.bsize, self.fsize, "big-endian")
        self.write_device()
        self.made(swap="3")
        rc, o, bm = self.export()
        self.assertEqual(rc, 0, o)
        for part, name in (("root", "ps3ssd"), ("swap", "ps3ssd-swap")):
            rc, out, err = self.boot("tables", bm, part)
            self.assertEqual(out, (self.state / (name + ".table")).read_text(), part)
            self.assertGreater(len(out.splitlines()), 1, part)

    def test_export_refusals(self):
        self.assertEqual(self.cli("map", str(self.file))[0], 0)
        rc, o, bm = self.export("--swap-mib", "1")
        self.assertEqual(rc, 2)
        self.assertIn("no verify pass", o)
        self.assertEqual(self.cli("verify", self.mapfile())[0], 0)
        rc, o, bm = self.export()
        self.assertEqual(rc, 2)
        self.assertIn("give --swap-mib", o)
        rc, o, bm = self.export("--swap-mib", "1")
        self.assertEqual(rc, 0, o)   # verified, never created: the layout given by hand
        self.assertEqual(self.cli("recheck", self.mapfile())[0], 0)
        self.minfo.write_text("")
        self.assertEqual(self.cli("create", self.mapfile(), "ps3ssd", "--yes", "--swap-mib", "1")[0], 0)
        rc, o, bm = self.export("--swap-mib", "2")
        self.assertEqual(rc, 2)
        self.assertIn("not 2", o)
        rc, o, bm = self.export("--swap-mib", "3")
        self.assertEqual(rc, 2)

    def rewrite(self, bm, edit, fix_digest=False, fix_sha=True):
        """BM's text through EDIT; optionally the extent digest and the closing sha256 made right again"""
        import hashlib
        lines = bm.read_text().splitlines()[:-1]
        lines = edit(lines)
        if fix_digest:
            kv = dict(x.split(" ", 1) for x in lines[1:12])
            n = int(lines[12].split()[1])
            ext = [tuple(int(v) for v in x.split()) for x in lines[13:13 + n]]
            d = self.m.digest(int(kv["file_size"]), kv["uuid"], ext)
            lines = [("extents_sha256 " + d) if x.startswith("extents_sha256 ") else x for x in lines]
        body = "".join(x + "\n" for x in lines)
        out = self.d / "bad.map"
        out.write_text(body + ("sha256 %s\n" % hashlib.sha256(body.encode()).hexdigest() if fix_sha else "sha256 " + "0" * 64 + "\n"))
        return out

    def test_bad_maps_refused(self):
        self.made()
        rc, o, bm = self.export()
        self.assertEqual(rc, 0, o)
        good = bm.read_text()
        cases = [
            ("edited byte", lambda b: b.write_text(good.replace("inode ", "inode 1", 1)), "sha256 line doesn't match"),
            ("truncated", lambda b: b.write_text(good[:len(good) // 2]), "sha256 line"),
            ("not a map", lambda b: b.write_text("hello\n"), "sha256 line"),
        ]
        for what, mk, why in cases:
            mk(bm)
            rc, out, err = self.boot("check", bm)
            self.assertEqual(rc, 2, what)
            self.assertIn(why, err, what)
        bm.write_text(good)

        def ext_edit(i, delta):
            def f(lines):
                n0 = 13 + i
                l, p, n = (int(v) for v in lines[n0].split())
                lines[n0] = "%d %d %d" % (l, p + delta, n)
                return lines
            return f
        for what, edit, fixd, why in (
                ("extent moved, digest stale", ext_edit(1, 8), False, "extent list's sha256"),
                ("extents overlap", lambda L: L[:13] + [L[13]] + ["%s %s %s" % (L[14].split()[0], L[13].split()[1], L[14].split()[2])] + L[15:], True, "overlap"),
                ("inside the super block", lambda L: L[:13] + ["0 16 %s" % L[13].split()[2]] + L[14:], True, "outside the data area"),
                ("swap too big", lambda L: [("swap_sectors %d" % (8 << 20)) if x.startswith("swap_sectors") else x for x in L], False, "leaves less than 2 MiB"),
                ("bad image path", lambda L: [("image /../x.img") if x.startswith("image ") else x for x in L], False, "image path"),
                ("unknown field order", lambda L: [L[0], L[2], L[1]] + L[3:], False, "expected"),
                ("bad uuid", lambda L: [("uuid " + "Z" * 36) if x.startswith("uuid ") else x for x in L], False, "bad uuid"),
                ("lines after", lambda L: L + ["0 0 0"], False, "lines after the extents"),
        ):
            b = self.rewrite(bm, edit, fix_digest=fixd)
            rc, out, err = self.boot("check", b)
            self.assertEqual(rc, 2, (what, err))
            self.assertIn(why, err, what)
        b = self.rewrite(bm, lambda L: L, fix_sha=False)
        self.assertEqual(self.boot("check", b)[0], 2)
        b = self.rewrite(bm, lambda L: L)
        self.assertEqual(self.boot("check", b)[0], 0)   # the rewriter itself is faithful

    def test_device_ops_refused_off_the_ps3(self):
        """recheck and create name /dev/ps3db themselves: a map for another device is refused before any I/O"""
        self.made()
        rc, o, bm = self.export()
        rc, out, err = self.boot("recheck", bm, self.d / "mnt", "--run", self.d / "run")
        self.assertEqual(rc, 2)
        self.assertIn("the boot uses /dev/ps3db", err)
        rc, out, err = self.boot("create", bm, "--run", self.d / "run")
        self.assertEqual(rc, 2)
        self.assertIn("the boot uses /dev/ps3db", err)


class InstallManifest(unittest.TestCase):
    """stamp.py install's manifest against ps3-imgmap-boot install-conf (the initramfs's reader)"""
    def setUp(self):
        self.tool = build_boot_tool()
        if self.tool is None:
            self.skipTest("no gcc")
        SCRATCH.mkdir(parents=True, exist_ok=True)
        self.f = SCRATCH / ("%s.conf" % self._testMethodName)

    def conf(self, text):
        self.f.write_text(text)
        return subprocess.run([str(self.tool), "install-conf", str(self.f)], capture_output=True, text=True)

    def good(self):
        return stamp.manifest("/linux/walnut.img", UID, 16 << 30, 1024, "/linux/install/rootfs.tar.xz",
                              2_000_000_000, "ab" * 32, 6_000_000_000)

    def test_accepted_and_printed(self):
        p = self.conf(self.good())
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("image=/linux/walnut.img\n", p.stdout)
        self.assertIn("uuid=%s\n" % UID, p.stdout)
        self.assertIn("tar_size=6000000000\n", p.stdout)

    def test_tampered_or_truncated_refused(self):
        m = self.good()
        self.assertNotEqual(self.conf(m.replace("swap_mib 1024", "swap_mib 1025")).returncode, 0)  # sha256 no longer fits
        self.assertNotEqual(self.conf(m[:-10]).returncode, 0)                                     # truncated
        self.assertNotEqual(self.conf(m.rsplit("sha256 ", 1)[0]).returncode, 0)                   # no closing line

    def test_rootfs_outside_install_dir_refused(self):
        m = stamp.manifest("/linux/walnut.img", UID, 16 << 30, 1024, "/linux/boot/x.tar.xz", 1, "ab" * 32, 1)
        p = self.conf(m)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("/linux/install/", p.stdout + p.stderr)

    def test_prepare(self):
        """stamp.py prepare (the release archive): its install.conf passes the initramfs's reader, its
        image unpacks to stamped blocks of that uuid, the boot files are where kboot.conf says"""
        import tarfile
        d = SCRATCH / "prepare"
        shutil.rmtree(d, ignore_errors=True)
        (d / "in").mkdir(parents=True)
        with tarfile.open(d / "in/rootfs.tar.xz", "w:xz") as tf:
            info = tarfile.TarInfo("etc/hostname")
            info.size = 7
            tf.addfile(info, io.BytesIO(b"walnut\n"))
        for n in ("vmlinux", "initrd"):
            (d / "in" / n).write_bytes(n.encode())
        (d / "in/r.args").write_text("quiet\n")
        r = subprocess.run([sys.executable, str(HERE / "stamp.py"), "prepare", "--size", "260M", "--swap-mib", "0",
                            "--rootfs", str(d / "in/rootfs.tar.xz"), "--kernel", str(d / "in/vmlinux"),
                            "--initrd", str(d / "in/initrd"), "--release", "6.12.1+ps3.1", "--args-file",
                            str(d / "in/r.args"), "--uuid", str(UID), "--out", str(d / "out")],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        p = subprocess.run([str(self.tool), "install-conf", str(d / "out/dev_hdd1/linux/install/install.conf")],
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("uuid=%s\n" % UID, p.stdout)
        img = d / "walnut.img"
        with lzma.open(d / "out/dev_hdd0/linux/walnut.img.xz") as src, open(img, "wb") as dst:
            shutil.copyfileobj(src, dst, 16 * stamp.MIB)
        self.assertEqual(img.stat().st_size, 260 * stamp.MIB)
        c = subprocess.run([sys.executable, str(HERE / "stamp.py"), "check", str(img), "--uuid", str(UID)],
                           capture_output=True, text=True)
        self.assertEqual(c.returncode, 0, c.stdout)
        kboot = (d / "out/dev_hdd1/kboot.conf").read_text()
        self.assertIn("walnut='/linux/boot/vmlinux-6.12.1+ps3.1 initrd=/linux/boot/initrd-walnut.img ", kboot)
        for f in ("dev_hdd1/linux/boot/vmlinux-6.12.1+ps3.1", "dev_hdd1/linux/boot/initrd-walnut.img",
                  "dev_hdd0/linux/walnut-good/vmlinux-6.12.1+ps3.1", "dev_hdd0/linux/walnut-good/kboot.conf"):
            self.assertTrue((d / "out" / f).is_file(), f)
        shutil.rmtree(d)

    def test_unpacked_size(self):
        t = SCRATCH / "u.tar.xz"
        import tarfile
        with tarfile.open(t, "w:xz") as tf:
            for name, n in (("a", 1), ("b", 4097)):
                info = tarfile.TarInfo(name)
                info.size = n
                tf.addfile(info, io.BytesIO(b"x" * n))
        self.assertEqual(stamp.unpacked_size(t), (4096 + 4096) + (4096 + 8192))


class FakeGuard:
    """the kernel's /sys/block/ps3db/write_guard (local/0045) for ps3-imgmap's guard functions: a write
    of more than 4096 bytes cut at 4096 (sysfs), each write parsed whole or refused (EINVAL), the seal
    sorts and merges, EPERM after it. FAULTS as in guard-fake.h: short=K, einval=K, sealfail,
    nr_ranges+=D, nr_sectors+=D"""

    def __init__(self, policy="whitelist", sealed=False, pending=0, **faults):
        self.policy, self.sealed, self.faults = policy, sealed, faults
        self.ranges = [(10 ** 6 + 100 * i, 8) for i in range(pending)]
        self.writes, self.nwrites = [], 0
        if sealed:
            self.seal()

    def seal(self):
        out = []
        for a, n in sorted(self.ranges):
            if out and a <= out[-1][1]:
                out[-1][1] = max(out[-1][1], a + n)
            else:
                out.append([a, a + n])
        self.ranges = [(a, b - a) for a, b in out]
        self.sealed = True

    def install(self, m):
        m.guard_present = lambda: True
        m.guard_read = self.read
        m.guard_open = lambda name: {"ranges": 100, "sealed": 101}[name]
        m.guard_write = self.write
        m.guard_close = lambda fd: None

    def read(self, name):
        if name == "policy":
            return self.policy
        if name == "sealed":
            return "1" if self.sealed else "0"
        if name == "nr_ranges":
            return str(len(self.ranges) + (self.faults.get("nr_ranges", 0) if self.sealed else 0))
        if name == "nr_sectors":
            return str(sum(n for _, n in self.ranges) + self.faults.get("nr_sectors", 0) if self.sealed else 0)
        raise FileNotFoundError(name)

    def write(self, fd, data):
        if fd == 101:
            self.writes.append(("sealed", data))
            if self.faults.get("sealfail"):
                raise OSError(5, "Input/output error")
            if data.strip() != b"1":
                raise OSError(22, "Invalid argument")
            if self.sealed:
                raise OSError(1, "Operation not permitted")
            self.seal()
            return len(data)
        self.nwrites += 1
        self.writes.append(("ranges", data))
        if self.sealed:
            raise OSError(1, "Operation not permitted")
        if self.faults.get("einval") == self.nwrites:
            raise OSError(22, "Invalid argument")
        count = min(len(data), 4096)
        if self.faults.get("short") == self.nwrites:
            count = data.index(b"\n") + 1
        buf = data[:count]
        if not buf.endswith(b"\n"):
            raise OSError(22, "Invalid argument")
        new = []
        for line in buf.decode().splitlines():
            if not line.strip():
                continue
            a, n = line.split()
            if not (a.isdigit() and n.isdigit()) or int(n) == 0:
                raise OSError(22, "Invalid argument")
            new.append((int(a), int(n)))
        self.ranges += new
        return count


def table_ranges(path):
    """[(ps3db sector, sectors)] of a table file ps3-imgmap create wrote"""
    return [(int(x.split()[4]), int(x.split()[1])) for x in path.read_text().splitlines()]


class GuardPhaseA(Pipeline):
    """ps3-imgmap create on a kernel with the write guard (FakeGuard): what it registers, how, and
    every refusal"""

    def ready(self, swap="1"):
        self.assertEqual(self.cli("map", str(self.file))[0], 0)
        self.assertEqual(self.cli("verify", self.mapfile())[0], 0)
        self.assertEqual(self.cli("recheck", self.mapfile())[0], 0)
        self.minfo.write_text("")

    def create(self, g, swap="1"):
        g.install(self.m)
        return self.cli("create", self.mapfile(), "ps3ssd", "--yes", "--swap-mib", swap)

    def check_writes(self, g):
        """every write to ranges: whole lines, at most 4096 bytes, one range per line"""
        for kind, data in g.writes:
            if kind == "ranges":
                self.assertLessEqual(len(data), 4096)
                self.assertTrue(data.endswith(b"\n"))
                for line in data.decode().splitlines():
                    self.assertRegex(line, r"^[0-9]+ [0-9]+$")

    def test_no_guard_goes_on(self):
        self.ready()
        rc, out = self.cli("create", self.mapfile(), "ps3ssd", "--yes", "--swap-mib", "1")
        self.assertEqual(rc, 0, out)
        self.assertIn("kernel has no write guard", out)

    def test_registers_the_tables_and_seals(self):
        self.ready()
        g = FakeGuard()
        rc, out = self.create(g)
        self.assertEqual(rc, 0, out)
        want = table_ranges(self.state / "ps3ssd.table") + table_ranges(self.state / "ps3ssd-swap.table")
        sent = [(int(a), int(n)) for k, d in g.writes if k == "ranges" for a, n in (x.split() for x in d.decode().splitlines())]
        self.assertEqual(sent, want)   # exactly the device side of both tables, in order
        self.assertEqual(g.writes[-1], ("sealed", b"1\n"))
        self.assertTrue(g.sealed)
        self.check_writes(g)
        # the header area (image sectors 0..2047) is in no range
        mp = json.loads(Path(self.mapfile()).read_text())
        hdr = {p + i for l, p, n in mp["extents"] for i in range(n) if l + i < 2048}
        self.assertFalse(any(a <= s < a + n for a, n in g.ranges for s in hdr))
        self.assertIn("write guard sealed", out)
        log = self.log.read_text()
        self.assertIn("dmsetup table ps3ssd-swap", log)
        self.assertNotIn("remove", log)

    def test_many_ranges_go_in_several_whole_line_writes(self):
        self.size = 16 << 20
        self.file.write_bytes(b"".join(stamp.chunks(UID.bytes, self.size)))
        self.bsize = self.fsize
        nb = self.size // self.bsize
        self.ufs = FakeUFS(self.fsize, self.bsize, nb, gaps=set(range(0, nb, 2)))
        self.m.fibmap = self.ufs.fibmap
        self.m.ufs_super = lambda p: (self.bsize, self.fsize, "big-endian")
        self.write_device()
        self.ready()
        g = FakeGuard()
        rc, out = self.create(g)
        self.assertEqual(rc, 0, out)
        chunks = [d for k, d in g.writes if k == "ranges"]
        self.assertGreater(len(chunks), 1)
        self.check_writes(g)
        want = table_ranges(self.state / "ps3ssd.table") + table_ranges(self.state / "ps3ssd-swap.table")
        self.assertEqual(len(want), sum(d.count(b"\n") for d in chunks))
        self.assertEqual(len(g.ranges), len(want))   # nothing touches: an indirect block between each pair

    def test_refusals_before_anything_is_made(self):
        self.ready()
        for g, why in ((FakeGuard(sealed=True), "sealed earlier in this boot"),
                       (FakeGuard(policy="writable"), "not whitelist"),
                       (FakeGuard(pending=2), "already registered")):
            if self.log.exists():
                self.log.unlink()
            rc, out = self.create(g)
            self.assertEqual(rc, 2, out)
            self.assertIn(why, out)
            self.assertFalse(self.log.exists(), why)   # no setro, no dmsetup
            self.assertFalse([w for w in g.writes if w[0] == "ranges" and not g.sealed], why)

    def test_refusals_after_the_devices_remove_them(self):
        self.ready()
        for faults, why in (({"short": 1}, "took"), ({"einval": 1}, "Invalid argument"),
                            ({"sealfail": True}, "sealed: writing 1 failed"), ({"nr_ranges": 1}, "the sealed guard reads"),
                            ({"nr_sectors": 8}, "the sealed guard reads")):
            if self.log.exists():
                self.log.unlink()
            g = FakeGuard(**faults)
            rc, out = self.create(g)
            self.assertEqual(rc, 2, (faults, out))
            self.assertIn(why, out, faults)
            self.assertIn("reboot", out, faults)
            log = self.log.read_text().splitlines()
            self.assertIn("dmsetup remove ps3ssd-swap", log, faults)
            self.assertIn("dmsetup remove ps3ssd", log, faults)
            self.assertEqual(os.listdir(self.d / "tables"), [], faults)


def build_boot_tool_fake():
    """ps3-imgmap-boot.c's test build: the guard's sysfs I/O replaced by guard-fake.h, on the kernel's
    ps3disk_guard.h from the patch; None without gcc"""
    if not shutil.which("gcc"):
        return None
    out = SCRATCH / "ps3-imgmap-boot-fake"
    if not out.exists():
        SCRATCH.mkdir(parents=True, exist_ok=True)
        inc = SCRATCH / "guard-inc"
        inc.mkdir(exist_ok=True)
        with open(inc / "ps3disk_guard.h", "w") as f:
            subprocess.run([str(ROOT / "linux" / "tests" / "guard-header.sh")], stdout=f, check=True)
        subprocess.run(["gcc", "-O2", "-Wall", "-Wextra", "-Werror", "-DPS3_GUARD_FAKE", "-I", str(inc), "-I", str(HERE),
                        "-o", str(out), str(HERE / "ps3-imgmap-boot.c")], check=True)
    return out


class GuardBootTool(BootMap):
    """ps3-imgmap-boot's guard steps (the test build's guard-test: create's precheck, registration,
    seal and checks on the boot map's tables) against the model of the kernel's side"""

    def setUp(self):
        super().setUp()
        self.fake = build_boot_tool_fake()

    def gtest(self, bm, faults="", *extra):
        logf = self.d / "guard.log"
        if logf.exists():
            logf.unlink()
        env = dict(os.environ, PS3_GUARD_FAKE=faults, PS3_GUARD_FAKE_LOG=str(logf))
        r = subprocess.run([str(self.fake), "guard-test", str(bm)] + list(extra), capture_output=True, text=True, env=env)
        writes = []
        if logf.exists():
            data = logf.read_bytes()
            while data:
                head, _, data = data.partition(b"\n")
                kind, *rest = head.decode().split(" ", 1)
                if kind == "ranges":
                    n = int(rest[0].split()[0])
                    writes.append(("ranges", data[:n]))
                    data = data[n:]
                else:
                    writes.append(("sealed", rest[0].encode() + b"\n"))
        return r.returncode, r.stderr, writes

    def boot_map(self):
        rc, o, bm = self.export()
        self.assertEqual(rc, 0, o)
        return bm

    def test_registers_both_tables_and_seals(self):
        self.made()
        bm = self.boot_map()
        rc, err, writes = self.gtest(bm)
        self.assertEqual(rc, 0, err)
        want = table_ranges(self.state / "ps3ssd.table") + table_ranges(self.state / "ps3ssd-swap.table")
        sent = [(int(a), int(n)) for k, d in writes if k == "ranges" for a, n in (x.split() for x in d.decode().splitlines())]
        self.assertEqual(sent, want)
        self.assertEqual(writes[-1], ("sealed", b"1\n"))
        self.assertIn("write guard sealed", err)
        # the same ranges as Phase A's ps3-imgmap create registers
        g = FakeGuard()
        g.install(self.m)
        self.m.guard_register(want)
        self.assertEqual([w for w in g.writes if w[0] == "ranges"], [w for w in writes if w[0] == "ranges"])

    def test_chunks(self):
        self.size = 16 << 20
        self.file.write_bytes(b"".join(stamp.chunks(UID.bytes, self.size)))
        self.bsize = self.fsize
        nb = self.size // self.bsize
        self.ufs = FakeUFS(self.fsize, self.bsize, nb, gaps=set(range(0, nb, 2)))
        self.m.fibmap = self.ufs.fibmap
        self.m.ufs_super = lambda p: (self.bsize, self.fsize, "big-endian")
        self.write_device()
        self.made()
        bm = self.boot_map()
        rc, err, writes = self.gtest(bm)
        self.assertEqual(rc, 0, err)
        chunks = [d for k, d in writes if k == "ranges"]
        self.assertGreater(len(chunks), 1)
        for c in chunks:
            self.assertLessEqual(len(c), 4096)
            self.assertTrue(c.endswith(b"\n"))
        self.assertTrue(all(len(c) + 12 > 4096 for c in chunks[:-1]))   # filled up before the next write
        # the second write fails: the first one's ranges stay registered, reboot
        rc, err, writes = self.gtest(bm, "einval=2")
        self.assertEqual(rc, 2, err)
        self.assertIn("write 2", err)
        self.assertIn("Invalid argument", err)
        self.assertIn("reboot", err)

    def test_without_guard(self):
        self.made()
        bm = self.boot_map()
        rc, err, writes = self.gtest(bm, "absent")
        self.assertEqual(rc, 0, err)
        self.assertIn("kernel has no write guard", err)
        self.assertEqual(writes, [])
        rc, err, writes = self.gtest(bm, "absent", "--guard", "required")
        self.assertEqual(rc, 2, err)
        self.assertIn("for a kernel with it", err)

    def test_refusals(self):
        self.made()
        bm = self.boot_map()
        for faults, why, wrote in (("sealed", "sealed earlier in this boot", False),
                                   ("policy=writable", "not whitelist", False),
                                   ("policy=read-only", "not whitelist", False),
                                   ("pending=3", "already registered", False),
                                   ("short=1", "a short count", True),
                                   ("einval=1", "Invalid argument", True),
                                   ("sealfail", "writing 1 got Input/output error", True),
                                   ("nr_ranges+=1", "ours merge into", True),
                                   ("nr_sectors+=8", "our targets", True),
                                   ("capacity=4096", "Invalid argument", True)):
            rc, err, writes = self.gtest(bm, faults)
            self.assertEqual(rc, 2, (faults, err))
            self.assertIn(why, err, faults)
            self.assertEqual(bool(writes), wrote, faults)
            if wrote:
                self.assertIn("reboot", err, faults)


# the guard classes reuse the fixtures of Pipeline and BootMap, not their tests (those run there)
for _cls in (GuardPhaseA, GuardBootTool):
    for _name in dir(_cls.__mro__[1]):
        if _name.startswith("test") and _name not in _cls.__dict__:
            setattr(_cls, _name, None)


class VswapScript(unittest.TestCase):
    """ps3-system's ps3-vswap with PS3_VSWAP_DM (the SSD swap arm's backing): commands stubbed, /etc
    and /sys under PS3_VSWAP_ROOT; the default (no /etc/default/ps3-vswap) must stay the partition"""
    SCRIPT = ROOT / "ps3-system" / "files" / "sbin" / "ps3-vswap"

    def setUp(self):
        import stat
        self.d = SCRATCH / ("vswap-" + self._testMethodName)
        shutil.rmtree(self.d, ignore_errors=True)
        r, b = self.d / "root", self.d / "bin"
        (r / "etc" / "default").mkdir(parents=True)
        (r / "sys" / "block" / "ps3vswap0").mkdir(parents=True)
        (r / "sys" / "module" / "ps3rsx" / "parameters").mkdir(parents=True)
        b.mkdir()
        (r / "etc" / "fstab").write_text("PARTUUID=50533301-01  none  swap   sw,pri=200,noauto 0 0\n")
        (r / "sys" / "block" / "ps3vswap0" / "backing").write_text("")
        (r / "sys" / "module" / "ps3rsx" / "parameters" / "vswap_size_mb").write_text("1024\n")
        self.part = next("/dev/" + n for n in sorted(os.listdir("/sys/class/block"))
                         if os.path.exists("/dev/" + n) and stat.S_ISBLK(os.stat("/dev/" + n).st_mode))
        self.log = self.d / "log"
        for c, body in (("findfs", 'echo %s' % self.part), ("blockdev", 'echo 8388608'),
                        ("modprobe", 'exit 0'), ("dmsetup", 'echo "dmsetup $*" >> %s; exit ${DM_RC:-0}' % self.log),
                        ("mkswap", 'echo "mkswap $*" >> %s' % self.log), ("swapon", 'echo "swapon $*" >> %s' % self.log),
                        ("swapoff", 'echo "swapoff $*" >> %s' % self.log)):
            (b / c).write_text("#!/bin/sh\n%s\n" % body)
            (b / c).chmod(0o755)
        self.root, self.bin = r, b

    def tearDown(self):
        shutil.rmtree(self.d, ignore_errors=True)

    def run_script(self, **env):
        e = dict(os.environ, PS3_VSWAP_ROOT=str(self.root), PATH="%s:%s" % (self.bin, os.environ["PATH"]), **env)
        subprocess.run(["sh", str(self.SCRIPT)], env=e, capture_output=True, text=True, timeout=30)
        back = (self.root / "sys" / "block" / "ps3vswap0" / "backing").read_text().strip()
        return back, [x for x in self.log.read_text().splitlines() if not x.startswith("swapoff")] if self.log.exists() else []

    def test_default_is_the_partition(self):
        back, log = self.run_script()
        self.assertEqual(back, self.part)
        self.assertEqual(log, ["mkswap /dev/ps3vswap0", "swapon -p 300 --discard=pages /dev/ps3vswap0"])

    def test_dm_backing(self):
        (self.root / "etc" / "default" / "ps3-vswap").write_text("PS3_VSWAP_DM=1\n")
        back, log = self.run_script()
        self.assertEqual(back, "/dev/mapper/ps3vswap-back")
        self.assertEqual(log, ["dmsetup create ps3vswap-back --table 0 2097152 linear %s 0" % self.part,
                               "mkswap /dev/ps3vswap0", "swapon -p 300 --discard=pages /dev/ps3vswap0"])

    def test_dm_failing_falls_back(self):
        (self.root / "etc" / "default" / "ps3-vswap").write_text("PS3_VSWAP_DM=1\n")
        back, log = self.run_script(DM_RC="1")
        self.assertEqual(back, self.part)
        self.assertEqual(log[-2:], ["mkswap /dev/ps3vswap0", "swapon -p 300 --discard=pages /dev/ps3vswap0"])

    def test_no_vram_swap(self):
        (self.root / "sys" / "block" / "ps3vswap0" / "backing").unlink()
        back_file = self.root / "sys" / "block" / "ps3vswap0" / "backing"
        e = dict(os.environ, PS3_VSWAP_ROOT=str(self.root), PATH="%s:%s" % (self.bin, os.environ["PATH"]))
        subprocess.run(["sh", str(self.SCRIPT)], env=e, capture_output=True, text=True, timeout=30)
        log = [x for x in self.log.read_text().splitlines() if not x.startswith("swapoff")]
        self.assertFalse(back_file.exists())
        self.assertEqual(log, ["swapon -p 200 %s" % self.part])


if __name__ == "__main__":
    r = unittest.main(exit=False, verbosity=2).result
    shutil.rmtree(SCRATCH, ignore_errors=True)
    sys.exit(0 if r.wasSuccessful() else 1)
