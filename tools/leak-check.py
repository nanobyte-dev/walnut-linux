#!/usr/bin/env python3
"""leak-check.py: keeps licensed content (docs/PACKAGING.md "Licensed packages") out of what we publish.

  leak-check.py debs DEB...                      no licensed binary package among these
  leak-check.py installed STATUS                 no licensed package installed (a dpkg status file)
  leak-check.py classify DSC...                  prints "licensed" or "free" and the .dsc, one per line
  leak-check.py sources [--licensed DEB|DIR]... DSC...
                                                 these source packages are free, and no file in
                                                 their tarballs is licensed content: not a file of a
                                                 licensed binary package (by SHA-256, members of .zip
                                                 archives included on both sides) and no path
                                                 matching a leak glob (tools/licensed.py globs)

What is licensed is tools/licensed.py's answer (its names, the Section rule, its leak globs; it reads
gamedb's build.toml [[licensed]], tools/licensed-files.txt, the dfsg-exclude.txt files and the licensed directories): a binary package
is licensed when its Section is non-free/* or local/* or its name is a licensed name; a source
package when its name is one or any package in its Package-List is licensed. Exit status: 0 clean,
1 a leak (each one printed), 2 usage or a licensed declaration that can't be read (fails closed).
Reads .deb files itself (ar, then data.tar.{xz,gz,zst,} with python's modules or zstd), so it runs
without dpkg on the PC too.
"""
import argparse
import gzip
import hashlib
import importlib.util
import io
import lzma
import os
import re
import subprocess
import sys
import tarfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
# tools/licensed.py by path: python3 -I keeps the script's directory off sys.path
_spec = importlib.util.spec_from_file_location("licensed", os.path.join(HERE, "licensed.py"))
licensed = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(licensed)
# files of a licensed package that say nothing about its content, and tiny files that match by chance
SKIP_PREFIXES = ("usr/share/doc/", "usr/share/lintian/", "usr/share/doc-base/")
MIN_SIZE = 256


def is_licensed(name, section, names):
    return licensed.is_licensed(name, section, names)


def parse_fields(text):
    """Debian control-style fields; continuation lines kept with their newline."""
    fields, key = {}, None
    for line in text.splitlines():
        if not line.strip():
            if fields and key:
                key = None
            continue
        if line[0] in " \t" and key:
            fields[key] += "\n" + line.strip()
        elif ":" in line:
            key, _, val = line.partition(":")
            key = key.strip()
            fields[key] = val.strip()
    return fields


def ar_members(path):
    with open(path, "rb") as f:
        if f.read(8) != b"!<arch>\n":
            raise ValueError("%s: not a .deb (ar) file" % path)
        while True:
            hdr = f.read(60)
            if len(hdr) < 60:
                return
            name = hdr[:16].decode().strip().rstrip("/")
            size = int(hdr[48:58].decode().strip())
            data = f.read(size)
            if size % 2:
                f.read(1)
            yield name, data


def decompress(name, data):
    if name.endswith(".xz"):
        return lzma.decompress(data)
    if name.endswith(".gz"):
        return gzip.decompress(data)
    if name.endswith(".zst"):
        return subprocess.run(["zstd", "-dc"], input=data, stdout=subprocess.PIPE, check=True).stdout
    return data


def deb_parts(path):
    control = data = None
    for name, blob in ar_members(path):
        if name.startswith("control.tar"):
            control = decompress(name, blob)
        elif name.startswith("data.tar"):
            data = decompress(name, blob)
    if control is None or data is None:
        raise ValueError("%s: no control.tar or data.tar" % path)
    return control, data


def deb_control(path):
    control, _ = deb_parts(path)
    with tarfile.open(fileobj=io.BytesIO(control)) as t:
        for m in t:
            if m.isfile() and os.path.basename(m.name) == "control":
                return parse_fields(t.extractfile(m).read().decode("utf-8", "replace"))
    raise ValueError("%s: no control file" % path)


def file_hashes(name, blob, out, depth=0):
    """sha256 of a file and, for a .zip (or a zip by its magic), of each member, into out[hash] = name"""
    if len(blob) >= MIN_SIZE:
        out.setdefault(hashlib.sha256(blob).hexdigest(), name)
    if depth < 2 and blob[:4] == b"PK\x03\x04":
        try:
            with zipfile.ZipFile(io.BytesIO(blob)) as z:
                for zi in z.infolist():
                    if not zi.is_dir():
                        file_hashes(name + "!" + zi.filename, z.read(zi), out, depth + 1)
        except (zipfile.BadZipFile, NotImplementedError, RuntimeError):
            pass


def tar_files(blob_or_path):
    """(path, bytes) of every regular file in a tar (any compression python knows)"""
    if isinstance(blob_or_path, bytes):
        t = tarfile.open(fileobj=io.BytesIO(blob_or_path), mode="r:*")
    else:
        t = tarfile.open(blob_or_path, mode="r:*")
    with t:
        for m in t:
            if m.isfile():
                yield m.name, t.extractfile(m).read()


def licensed_hashes(debs, names):
    """hash -> 'deb:path' for every file of the licensed packages among debs"""
    out = {}
    for d in debs:
        try:
            c = deb_control(d)
        except ValueError as e:
            print("leak-check: skipped %s" % e, file=sys.stderr)
            continue
        if not is_licensed(c.get("Package", ""), c.get("Section"), names):
            continue
        _, data = deb_parts(d)
        for path, blob in tar_files(data):
            rel = path[2:] if path.startswith("./") else path
            if rel.startswith(SKIP_PREFIXES):
                continue
            file_hashes("%s:%s" % (os.path.basename(d), rel), blob, out)
    return out


def dsc_info(dsc):
    with open(dsc, encoding="utf-8", errors="replace") as f:
        text = f.read()
    # a signed .dsc: the fields are inside the PGP block
    if "-----BEGIN PGP SIGNED MESSAGE-----" in text:
        text = text.split("\n\n", 1)[1].split("-----BEGIN PGP SIGNATURE-----")[0]
    fields = parse_fields(text)
    pkgs = []
    for line in fields.get("Package-List", "").splitlines():
        parts = line.split()
        if len(parts) >= 3:
            pkgs.append((parts[0], parts[2]))
    if not pkgs:
        pkgs = [(b.strip(), "") for b in fields.get("Binary", "").split(",") if b.strip()]
    files = [ln.split()[-1] for ln in fields.get("Files", "").splitlines() if len(ln.split()) == 3]
    return fields.get("Source", ""), pkgs, files


def source_licensed(dsc, names):
    src, pkgs, _ = dsc_info(dsc)
    return src in names or any(is_licensed(n, s, names) for n, s in pkgs)


def expand_debs(items):
    out = []
    for it in items:
        if os.path.isdir(it):
            for root, _, files in os.walk(it):
                out += [os.path.join(root, f) for f in files if f.endswith(".deb")]
        elif it.endswith(".deb") and os.path.exists(it):
            out.append(it)
    return sorted(set(out))


def cmd_debs(a, names):
    bad = 0
    for d in a.files:
        c = deb_control(d)
        if is_licensed(c.get("Package", ""), c.get("Section"), names):
            print("LEAK: licensed package %s (%s, Section %s)" % (c.get("Package"), d, c.get("Section")))
            bad += 1
    print("%d packages, %d licensed" % (len(a.files), bad))
    return 1 if bad else 0


def cmd_installed(a, names):
    with open(a.status, encoding="utf-8", errors="replace") as f:
        stanzas = f.read().split("\n\n")
    bad = n = 0
    for st in stanzas:
        c = parse_fields(st)
        if not c.get("Package") or "installed" not in c.get("Status", ""):
            continue
        n += 1
        if is_licensed(c["Package"], c.get("Section"), names):
            print("LEAK: licensed package installed: %s %s (Section %s)" % (c["Package"], c.get("Version"), c.get("Section")))
            bad += 1
    print("%d packages installed, %d licensed" % (n, bad))
    return 1 if bad else 0


def cmd_classify(a, names):
    for d in a.dsc:
        print("%s %s" % ("licensed" if source_licensed(d, names) else "free", d))
    return 0


def cmd_sources(a, names):
    patterns = licensed.globs()
    hashes = licensed_hashes(expand_debs(a.licensed), names)
    bad = 0
    for dsc in a.dsc:
        src, _, files = dsc_info(dsc)
        if source_licensed(dsc, names):
            print("LEAK: %s is a licensed source package (%s)" % (src, dsc))
            bad += 1
            continue
        for fn in files:
            p = os.path.join(os.path.dirname(dsc), fn)
            if not re.search(r"\.tar(\.(gz|xz|bz2|lzma|zst))?$", fn):	# not the .asc signatures next to them
                continue
            for path, blob in tar_files(p):
                hit = licensed.glob_hit(path, patterns)
                if hit:
                    print("LEAK: %s: %s matches %s (tools/licensed.py globs)" % (fn, path, hit))
                    bad += 1
                found = {}
                file_hashes(path, blob, found)
                for h, where in found.items():
                    if h in hashes:
                        print("LEAK: %s: %s is %s" % (fn, where, hashes[h]))
                        bad += 1
    print("%d source packages checked against %d files of licensed packages, %d leaks"
          % (len(a.dsc), len(hashes), bad))
    return 1 if bad else 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("debs")
    s.add_argument("files", nargs="*")
    s = sub.add_parser("installed")
    s.add_argument("status")
    s = sub.add_parser("classify")
    s.add_argument("dsc", nargs="+")
    s = sub.add_parser("sources")
    s.add_argument("--licensed", action="append", default=[], help="licensed .deb files or directories of them")
    s.add_argument("dsc", nargs="*")
    a = p.parse_args(argv)
    try:
        names = licensed.names()
        return {"debs": cmd_debs, "installed": cmd_installed, "classify": cmd_classify,
                "sources": cmd_sources}[a.cmd](a, names)
    except (OSError, ValueError, tarfile.TarError, licensed.Error) as e:
        print("leak-check: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
