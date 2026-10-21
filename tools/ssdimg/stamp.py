#!/usr/bin/env python3
"""stamp.py: the stamped test image for the SSD test drive (docs/GAMEOS-HDD.md "Test drive, Phase A").

Runs on the PC. Generates the image as a stream and uploads it to GameOS's dev_hdd0 over webMAN MOD's
FTP server (the PS3 in GameOS, FTP on in webMAN's setup). Nothing goes to the PC's disk: the stream
comes from a generator, block by block.

  stamp.py upload --size 16G [--path /dev_hdd0/linux/ssdtest.img] [--host PS3] [--port 21]
                  [--user U --password P] [--uuid UUID] [--force]
  stamp.py dry-run --mib N [--uuid UUID] [--out FILE|-]     N MiB of the same stream (tests)
  stamp.py check FILE [--uuid UUID]                         check a local stream (tests)
  stamp.py install --size 16G --rootfs ROOTFS.tar.xz --kernel VMLINUX --release REL --initrd INITRD
                  [--args-file ARGS] [--swap-mib 1024] [--image /linux/walnut.img] [--host ...] [--force]
        the internal install (docs/GAMEOS-HDD.md "The internal install"): the stamped image to
        dev_hdd0, the rootfs tarball, kernel, generic initramfs and Petitboot's kboot.conf entry to
        dev_hdd1, a known-good copy of the boot files to dev_hdd0:/linux/walnut-good/, and the install
        manifest (dev_hdd1:/linux/install/install.conf) LAST: the initramfs installs only when it is
        there, so an interrupted upload never installs. --skip-image reuses an uploaded image (same --uuid).

Stamp format (version 1). The image is a sequence of 4 KiB blocks. Each block starts with a 36-byte
stamp, the other 4060 bytes are zero:

  offset  size  field
       0     8  magic b"PS3IMGv1"
       8    16  instance UUID (raw bytes, RFC 4122 order), the same in every block of one image
      24     8  this block's offset in the image, in bytes, big-endian unsigned
      32     4  CRC-32 (zlib/IEEE 802.3) of bytes 0-31, big-endian unsigned

The first 1 MiB (256 blocks) is reserved as the image's header area (Phase B puts the real header
there); for now it is stamped like the rest. ps3-imgmap (same directory) reads the stamps back on the
PS3 through the file's block list, before anything writes there.

The size must be a whole number of MiB (so of every UFS block size, 64 KiB at most: no tail
fragment). After the upload the remote size is checked with SIZE.
"""
import argparse
import ftplib
import hashlib
import io
import tarfile
import os
import re
import struct
import sys
import time
import uuid as uuidlib
import zlib

MAGIC = b"PS3IMGv1"
BLOCK = 4096
MIB = 1 << 20
HEADER_AREA = MIB
STAMP_LEN = 36
ZERO_TAIL = bytes(BLOCK - STAMP_LEN)


def stamp(uid, offset):
    """the 36-byte stamp of the block at byte offset OFFSET of image UID (16 raw bytes)"""
    head = MAGIC + uid + struct.pack(">Q", offset)
    return head + struct.pack(">I", zlib.crc32(head) & 0xFFFFFFFF)


def block(uid, offset):
    return stamp(uid, offset) + ZERO_TAIL


def parse_stamp(b):
    """(magic_ok, uuid_bytes, offset, crc_ok) of a block's first 36 bytes"""
    head, crc = b[:32], struct.unpack(">I", b[32:36])[0]
    return (b[:8] == MAGIC, bytes(b[8:24]), struct.unpack(">Q", b[24:32])[0],
            (zlib.crc32(head) & 0xFFFFFFFF) == crc)


def chunks(uid, size, chunk=MIB, start=0):
    """the image from byte START to SIZE, CHUNK bytes at a time (CHUNK a multiple of 4 KiB)"""
    assert chunk % BLOCK == 0 and start % BLOCK == 0 and size % BLOCK == 0
    off = start
    while off < size:
        n = min(chunk, size - off)
        yield b"".join(block(uid, o) for o in range(off, off + n, BLOCK))
        off += n


class Stream:
    """a read()able file over chunks(), for ftplib's storbinary; counts what it handed out"""

    def __init__(self, uid, size, chunk=MIB):
        self.it = chunks(uid, size, chunk)
        self.buf = b""
        self.done = 0

    def read(self, n=-1):
        while (n < 0 or len(self.buf) < n):
            try:
                self.buf += next(self.it)
            except StopIteration:
                break
        if n < 0:
            n = len(self.buf)
        out, self.buf = self.buf[:n], self.buf[n:]
        self.done += len(out)
        return out


def parse_size(s):
    """'16G', '16GiB', '1024M', '1.5G' (GiB/MiB, binary) -> bytes; must be whole MiB"""
    m = re.fullmatch(r"\s*([0-9]+(?:\.[0-9]+)?)\s*([GgMm])(?:i?[Bb])?\s*", s)
    if not m:
        raise ValueError("size %r: give GiB or MiB, e.g. 16G or 512M" % s)
    v = float(m[1]) * (1 << 30 if m[2] in "Gg" else MIB)
    if v != int(v) or int(v) % MIB:
        raise ValueError("size %r is not a whole number of MiB" % s)
    if int(v) < 2 * MIB:
        raise ValueError("size %r: at least 2 MiB (1 MiB header area plus data)" % s)
    return int(v)


def get_uuid(s):
    return uuidlib.UUID(s) if s else uuidlib.uuid4()


def human(n):
    return "%.2f GiB" % (n / (1 << 30)) if n >= 1 << 30 else "%.1f MiB" % (n / MIB)


def upload(a):
    size = parse_size(a.size)
    uid = get_uuid(a.uuid)
    path = a.path
    d, name = path.rsplit("/", 1)
    if not name or not d.startswith("/dev_hdd0/"):
        sys.exit("!! --path %s: a file under /dev_hdd0/ (GameOS's internal disk)" % path)
    print(">> image %s, %s (%d bytes), uuid %s" % (path, human(size), size, uid))
    ftp = ftplib.FTP()
    ftp.connect(a.host, a.port, timeout=60)
    print(">> connected: %s" % ftp.getwelcome())
    ftp.login(a.user, a.password)
    ftp.voidcmd("TYPE I")
    try:   # webMAN answers SIZE for an existing file; anything else means it isn't there
        old = ftp.size(path)
    except ftplib.error_perm:
        old = None
    if old is not None and not a.force:
        sys.exit("!! %s exists (%d bytes): --force replaces it (the next ps3-imgmap map/verify "
                 "must then be redone; an old map no longer matches)" % (path, old))
    cur = ""
    for part in d.strip("/").split("/"):   # the directory, made if missing
        cur += "/" + part
        try:
            ftp.cwd(cur)
        except ftplib.error_perm:
            ftp.mkd(cur)
            print(">> made %s" % cur)
    ftp.cwd(d)
    s = Stream(uid.bytes, size, a.chunk)
    t0 = last = time.monotonic()
    shown = 0

    def progress(_):
        nonlocal last, shown
        now = time.monotonic()
        if now - last >= 2 or s.done == size:
            rate = (s.done - shown) / (now - last) / MIB if now > last else 0
            avg = s.done / (now - t0) / MIB if now > t0 else 0
            eta = (size - s.done) / (avg * MIB) if avg else 0
            print("\r>> %s of %s (%.0f%%), %.1f MB/s now, %.1f MB/s average, %d s left   " % (
                human(s.done), human(size), 100 * s.done / size, rate, avg, eta), end="", flush=True)
            last, shown = now, s.done

    ftp.storbinary("STOR " + name, s, blocksize=256 * 1024, callback=progress)
    dt = time.monotonic() - t0
    print("\n>> sent %d bytes in %.0f s (%.1f MB/s)" % (s.done, dt, s.done / dt / MIB if dt else 0))
    got = ftp.size(path)
    ftp.quit()
    if got != size:
        sys.exit("!! remote size %s, expected %d: the upload is incomplete" % (got, size))
    print(">> remote size %d: complete. uuid %s" % (got, uid))
    print(">> next, on Linux: ps3-imgmap map /mnt/gameos/hdd0%s" % path[len("/dev_hdd0"):])


def dry_run(a):
    uid = get_uuid(a.uuid)
    size = a.mib * MIB
    out = sys.stdout.buffer if a.out in (None, "-") else open(a.out, "wb")
    for c in chunks(uid.bytes, size):
        out.write(c)
    out.flush()
    if out is not sys.stdout.buffer:
        out.close()
    print("uuid %s, %d bytes" % (uid, size), file=sys.stderr)


def check(a):
    """every block of a local file: magic, one UUID (or --uuid), offset, CRC, zero tail"""
    want = uuidlib.UUID(a.uuid).bytes if a.uuid else None
    bad = 0
    off = 0
    with open(a.file, "rb") as f:
        while True:
            b = f.read(BLOCK)
            if not b:
                break
            mok, u, o, cok = parse_stamp(b) if len(b) >= STAMP_LEN else (False, b"", -1, False)
            if want is None and mok:
                want = u
            if not (len(b) == BLOCK and mok and cok and u == want and o == off and b[STAMP_LEN:] == ZERO_TAIL):
                bad += 1
                if bad <= 10:
                    print("bad block at %d: magic %s crc %s uuid %s offset %s" % (off, mok, cok, u == want, o))
            off += len(b)
    print("%d blocks, %d bad, uuid %s" % (off // BLOCK, bad, uuidlib.UUID(bytes=want) if want else "-"))
    return 1 if bad or off == 0 else 0


# --- the internal install (the PC side; the initramfs does the rest on the first boot) ------
INSTALL_DIR = "/linux/install"      # on dev_hdd1
BOOT_DIR = "/linux/boot"            # on dev_hdd1
GOOD_DIR = "/dev_hdd0/linux/walnut-good"   # the known-good boot files, written only here
INITRD_NAME = "initrd-walnut.img"
GOOD_PATH = re.compile(r"^/[A-Za-z0-9._+/-]+$")


def manifest(image, uid, size, swap_mib, rootfs, rootfs_size, rootfs_sha, tar_size):
    """the install manifest ps3-imgmap-boot install-conf reads: fields in order, a closing sha256 line"""
    body = ("walnut-install 1\nimage %s\nuuid %s\nimage_size %d\nswap_mib %d\nrootfs %s\nrootfs_size %d\n"
            "rootfs_sha256 %s\ntar_size %d\n" % (image, uid, size, swap_mib, rootfs, rootfs_size, rootfs_sha, tar_size))
    return body + "sha256 %s\n" % hashlib.sha256(body.encode()).hexdigest()


def unpacked_size(path):
    """bytes the tarball takes unpacked on ext4: files rounded up to 4 KiB, plus 4 KiB per entry"""
    total = 0
    with tarfile.open(path, "r:xz") as t:
        for m in t:
            total += 4096 + ((m.size + 4095) // 4096 * 4096 if m.isfile() else 0)
    return total


def file_sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(MIB), b""):
            h.update(b)
    return h.hexdigest()


def ftp_mkdirs(ftp, d):
    cur = ""
    for part in d.strip("/").split("/"):
        cur += "/" + part
        try:
            ftp.cwd(cur)
        except ftplib.error_perm:
            ftp.mkd(cur)


def ftp_put(ftp, data, remote):
    """bytes or a local path to REMOTE through REMOTE.tmp and a rename; the size checked"""
    d, name = remote.rsplit("/", 1)
    ftp_mkdirs(ftp, d)
    ftp.cwd(d)
    f = io.BytesIO(data) if isinstance(data, bytes) else open(data, "rb")
    n = len(data) if isinstance(data, bytes) else os.path.getsize(data)
    with f:
        ftp.storbinary("STOR " + name + ".tmp", f, blocksize=256 * 1024)
    if ftp.size(remote + ".tmp") != n:
        sys.exit("!! %s.tmp: remote size differs from %d: incomplete" % (remote, n))
    try:
        ftp.delete(remote)
    except ftplib.error_perm:
        pass
    ftp.rename(remote + ".tmp", remote)
    print(">> %s (%d bytes)" % (remote, n))


def ftp_get(ftp, remote):
    buf = io.BytesIO()
    try:
        ftp.retrbinary("RETR " + remote, buf.write)
    except ftplib.error_perm:
        return None
    return buf.getvalue()


def install(a):
    size = parse_size(a.size)
    uid = get_uuid(a.uuid)
    image = a.image
    if not GOOD_PATH.match(image) or "/../" in image or not image.endswith(".img"):
        sys.exit("!! --image %s: an absolute path on dev_hdd0 ending in .img" % image)
    if not re.match(r"^[A-Za-z0-9._+-]+$", a.release):
        sys.exit("!! --release %s: letters, digits, . _ + -" % a.release)
    rsize = os.path.getsize(a.rootfs)
    if rsize >= (4 << 30) - 1:
        sys.exit("!! %s: %d bytes, dev_hdd1 is FAT32 (under 4 GiB per file)" % (a.rootfs, rsize))
    print(">> measuring %s (unpacked size, sha256)..." % a.rootfs)
    tsize = unpacked_size(a.rootfs)
    rsha = file_sha(a.rootfs)
    root_bytes = size - HEADER_AREA - a.swap_mib * MIB
    if tsize + tsize // 16 + (256 << 20) > root_bytes:
        sys.exit("!! the system needs %s unpacked (plus ext4's overhead), the image's root has %s: a bigger "
                 "--size or a smaller --swap-mib" % (human(tsize), human(root_bytes)))
    args = open(a.args_file).read().split() if a.args_file else []
    entry = ("walnut='%s/vmlinux-%s initrd=%s/%s root=/dev/mapper/ps3root rootfstype=ext4 ro ps3ssd.image=%s %s'"
             % (BOOT_DIR, a.release, BOOT_DIR, INITRD_NAME, image, " ".join(args))).replace(" '", "'")
    if not a.skip_image:
        upload(argparse.Namespace(size=a.size, path="/dev_hdd0" + image, host=a.host, port=a.port, user=a.user,
                                  password=a.password, uuid=str(uid), chunk=a.chunk, force=a.force))
    ftp = ftplib.FTP()
    ftp.connect(a.host, a.port, timeout=60)
    ftp.login(a.user, a.password)
    ftp.voidcmd("TYPE I")
    if a.skip_image and ftp.size("/dev_hdd0" + image) != size:
        sys.exit("!! --skip-image: /dev_hdd0%s is not %d bytes" % (image, size))
    rootfs = INSTALL_DIR + "/rootfs.tar.xz"
    ftp_put(ftp, a.rootfs, "/dev_hdd1" + rootfs)
    ftp_put(ftp, a.kernel, "/dev_hdd1%s/vmlinux-%s" % (BOOT_DIR, a.release))
    ftp_put(ftp, a.initrd, "/dev_hdd1%s/%s" % (BOOT_DIR, INITRD_NAME))
    old = ftp_get(ftp, "/dev_hdd1/kboot.conf")
    keep = [l for l in (old or b"").decode(errors="replace").splitlines()
            if not l.startswith(("walnut=", "walnut-prev="))]
    kboot = ("\n".join(["# Petitboot's menu on dev_hdd1 (the internal install, stamp.py install)", entry] + keep)
             + "\n").encode()
    ftp_put(ftp, kboot, "/dev_hdd1/kboot.conf")
    ftp_put(ftp, a.kernel, "%s/vmlinux-%s" % (GOOD_DIR, a.release))
    ftp_put(ftp, a.initrd, "%s/%s" % (GOOD_DIR, INITRD_NAME))
    ftp_put(ftp, kboot, GOOD_DIR + "/kboot.conf")
    m = manifest(image, uid, size, a.swap_mib, rootfs, rsize, rsha, tsize)
    ftp_put(ftp, m.encode(), "/dev_hdd1" + INSTALL_DIR + "/install.conf")
    ftp.quit()
    print(">> done: set the boot flag to OtherOS and pick 'walnut' in Petitboot; the first boot checks the "
          "whole image, then formats it and unpacks the system")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    u = sub.add_parser("upload", help="stream the image to the PS3 over FTP (GameOS, webMAN MOD)")
    u.add_argument("--size", required=True, help="image size in GiB or MiB, e.g. 16G (whole MiB)")
    u.add_argument("--path", default="/dev_hdd0/linux/ssdtest.img")
    u.add_argument("--host", default=os.environ.get("PS3_IP"), required=not os.environ.get("PS3_IP"),
                   help="the PS3 in GameOS (default: $PS3_IP)")
    u.add_argument("--port", type=int, default=21)
    u.add_argument("--user", default="anonymous", help="webMAN takes any login")
    u.add_argument("--password", default="ps3")
    u.add_argument("--uuid", help="instance UUID (default: a new random one)")
    u.add_argument("--chunk", type=int, default=MIB, help=argparse.SUPPRESS)
    u.add_argument("--force", action="store_true", help="replace an existing file")
    d = sub.add_parser("dry-run", help="write N MiB of the stream to a file or stdout")
    d.add_argument("--mib", type=int, required=True)
    d.add_argument("--uuid")
    d.add_argument("--out", help="file (default: stdout)")
    i = sub.add_parser("install", help="the internal install: image, rootfs, boot files, manifest last")
    i.add_argument("--size", required=True, help="image size, e.g. 16G (whole MiB)")
    i.add_argument("--rootfs", required=True, help="the system as a .tar.xz")
    i.add_argument("--kernel", required=True, help="vmlinux")
    i.add_argument("--release", required=True, help="the kernel's release, e.g. 6.12.111+ps3.648")
    i.add_argument("--initrd", required=True, help="the generic initramfs (make initrd-walnut)")
    i.add_argument("--args-file", help="the kernel's extra arguments (/usr/lib/ps3/boot/<release>.args)")
    i.add_argument("--swap-mib", type=int, default=1024)
    i.add_argument("--image", default="/linux/walnut.img", help="the image's path on dev_hdd0")
    i.add_argument("--host", default=os.environ.get("PS3_IP"), required=not os.environ.get("PS3_IP"))
    i.add_argument("--port", type=int, default=21)
    i.add_argument("--user", default="anonymous")
    i.add_argument("--password", default="ps3")
    i.add_argument("--uuid")
    i.add_argument("--chunk", type=int, default=MIB, help=argparse.SUPPRESS)
    i.add_argument("--force", action="store_true", help="replace an existing image")
    i.add_argument("--skip-image", action="store_true", help="the image is uploaded already (pass its --uuid)")
    c = sub.add_parser("check", help="check a local stream file")
    c.add_argument("file")
    c.add_argument("--uuid")
    a = p.parse_args(argv)
    try:
        if a.cmd == "upload":
            upload(a)
        elif a.cmd == "install":
            if a.skip_image and not a.uuid:
                sys.exit("!! --skip-image needs the uploaded image's --uuid")
            install(a)
        elif a.cmd == "dry-run":
            dry_run(a)
        else:
            return check(a)
    except ValueError as e:
        sys.exit("!! %s" % e)
    return 0


if __name__ == "__main__":
    sys.exit(main())
