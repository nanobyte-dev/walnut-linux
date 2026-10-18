#!/usr/bin/env python3
"""mkcpio.py DIR OUT [--mtime N] [--node PATH:MODE:c|b:MAJOR:MINOR]...: DIR as a newc cpio archive (the
initramfs format), reproducible: sorted names, owner 0:0, every mtime N (SOURCE_DATE_EPOCH), inode
numbers in order, no hard links; plus device nodes that an unprivileged build can't mknod
(/dev/console). Regular files, directories and symlinks only. Used by tools/ssdimg/build-initrd.sh."""
import argparse
import os
import stat
import sys


def entry(out, ino, name, mode, mtime, data=b"", rdev=(0, 0)):
    n = name.encode() + b"\0"
    hdr = "070701" + "".join("%08x" % v for v in (
        ino, mode, 0, 0, 2 if stat.S_ISDIR(mode) else 1, mtime, len(data), 0, 0, rdev[0], rdev[1], len(n), 0))
    out.write(hdr.encode() + n)
    out.write(b"\0" * (-(110 + len(n)) % 4))
    out.write(data)
    out.write(b"\0" * (-len(data) % 4))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("dir")
    p.add_argument("out")
    p.add_argument("--mtime", type=int, default=int(os.environ.get("SOURCE_DATE_EPOCH", "0")))
    p.add_argument("--node", action="append", default=[])
    a = p.parse_args()
    items = []
    for root, dirs, files in os.walk(a.dir):
        dirs.sort()
        for n in dirs + files:
            full = os.path.join(root, n)
            items.append((os.path.relpath(full, a.dir), full))
    nodes = {}
    for spec in a.node:
        path, mode, kind, ma, mi = spec.split(":")
        nodes[path.lstrip("/")] = ((stat.S_IFCHR if kind == "c" else stat.S_IFBLK) | int(mode, 8), (int(ma), int(mi)))
    names = sorted(set([x[0] for x in items]) | set(nodes))
    full = dict(items)
    with open(a.out + ".tmp", "wb") as out:
        for ino, name in enumerate(names, 1):
            if name in nodes:
                mode, rdev = nodes[name]
                entry(out, ino, name, mode, a.mtime, rdev=rdev)
                continue
            st = os.lstat(full[name])
            if stat.S_ISLNK(st.st_mode):
                entry(out, ino, name, stat.S_IFLNK | 0o777, a.mtime, os.readlink(full[name]).encode())
            elif stat.S_ISDIR(st.st_mode):
                entry(out, ino, name, stat.S_IFDIR | (st.st_mode & 0o7777), a.mtime)
            elif stat.S_ISREG(st.st_mode):
                with open(full[name], "rb") as f:
                    entry(out, ino, name, stat.S_IFREG | (st.st_mode & 0o7777), a.mtime, f.read())
            else:
                sys.exit("mkcpio: %s: not a file, directory or symlink" % full[name])
        entry(out, 0, "TRAILER!!!", 0, 0)
    os.replace(a.out + ".tmp", a.out)


if __name__ == "__main__":
    main()
