#!/usr/bin/env python3
"""image-check.py TAR: the public image's rootfs tar holds nothing that belongs to the dev profile
or to licensed content. It reads the tar itself (xz, gzip or plain), so a match can't be lost to a
broken pipe the way a `tar -tf | grep -q` under pipefail can, and it fails when anything is unclear.

  - no dev account or dev sources: /home/tibi, /etc/sudoers.d/tibi, /etc/apt/sources.list.d/ps3-lan*
  - no path that matches one of tools/licensed.py's leak globs
  - a dpkg status file is present and tools/leak-check.py installed finds no licensed package in it

Exit status: 0 clean, 1 a finding (each printed), 2 the tar or a declaration can't be read.
Run it as python3 -I tools/image-check.py TAR.
"""
import importlib.util
import os
import posixpath
import subprocess
import sys
import tarfile
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEV_PREFIXES = ("home/tibi", "etc/sudoers.d/tibi", "etc/apt/sources.list.d/ps3-lan")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def normalize(name):
    """the path a member lands at, root-relative: // and ./ collapsed, .. resolved, no leading /"""
    return posixpath.normpath("/" + name).lstrip("/").rstrip("/") if name.strip("/.") else ""


def dev_hit(rel):
    for p in DEV_PREFIXES:
        if p.endswith("tibi"):
            if rel == p or rel.startswith(p + "/"):
                return p
        elif rel.startswith(p):
            return p
    return None


def scan(tar_path, status_out):
    """prints each finding; returns the number found. Writes the dpkg status file to status_out."""
    lic = load("licensed", os.path.join(HERE, "licensed.py"))
    globs = lic.globs()
    found = 0
    have_status = False
    with tarfile.open(tar_path, "r:*") as tf:
        for m in tf:
            rel = normalize(m.name)
            if ".." in m.name.split("/"):
                print("LEAK: a '..' in a member name of the image: %s" % m.name[:80])
                found += 1
            if not rel:
                continue
            p = dev_hit(rel)
            if p:
                print("LEAK: dev profile path in the image: /%s" % rel)
                found += 1
            g = lic.glob_hit(rel, globs)
            if g:
                print("LEAK: licensed content path in the image: /%s (matches %s)" % (rel, g))
                found += 1
            if rel == "var/lib/dpkg/status" and m.isfile():
                with open(status_out, "wb") as out:
                    out.write(tf.extractfile(m).read())
                have_status = True
        # tarfile ends its iteration quietly at a bad or cut-short header; the stream is only whole when the
        # block where it stopped is tar's end-of-archive block (512 zero bytes), as GNU tar would end it
        tf.fileobj.seek(tf.offset)
        if tf.fileobj.read(512) != b"\0" * 512:
            print("!! the tar stops at a bad or short header, or without its end-of-archive block")
            found += 1
    if not have_status:
        print("!! no var/lib/dpkg/status in the image")
        found += 1
    return found, have_status


def main(argv):
    if len(argv) != 2:
        print(__doc__.split("\n")[0], file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory() as d:
        status = os.path.join(d, "status")
        try:
            found, have_status = scan(argv[1], status)
        except (OSError, tarfile.TarError, SystemExit) as e:
            print("!! cannot read %s: %s" % (argv[1], e), file=sys.stderr)
            return 2
        if have_status:
            r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "leak-check.py"), "installed", status],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            sys.stdout.write(r.stdout)
            if r.returncode == 2:
                return 2
            if r.returncode:
                found += 1
    if found:
        print("!! the image check found %d problem(s)" % found)
        return 1
    print("image check clean")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
