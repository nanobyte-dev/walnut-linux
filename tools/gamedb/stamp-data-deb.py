#!/usr/bin/env python3
"""stamp-data-deb.py DEB...: give a licensed data deb the control fields walnut-games-helper import-data
writes (docs/PACKAGING.md "Data built on the console"): X-Walnut-Edition and, when known,
X-Walnut-Release. For the debs our licensed-suite builders make that write no such fields themselves:
game-data-packager's (tools/game-data.sh, make game-data and make licensed-data).

- The edition and a fixed release come from gamedb (build-info.py fields: [data_setup] editions, else the
  [[licensed]] entry's edition and release).
- Without a fixed release, a package in the import manifest (tools/gamedb/gdp-manifest.json) gets the one
  the helper would compute: every file of the deb in the manifest is hashed and matched against its
  alternatives, and the release names of the alternatives the required files matched are joined with +
  (the helper's own release_label). A file no alternative matches leaves the release out, with a warning.
- Only the control member is rewritten: debian-binary and data.tar.* keep their bytes, the ar headers
  their dates and modes, the control tarball its members' metadata and its compression (xz, gzip or
  none). The same input gives the same output. A deb that has the fields already is refused (it was
  stamped, or made by the helper).

Run only on NEW files: a version published to the licensed suite is never made again (game-data.sh
downloads it unchanged).
"""
import argparse
import gzip
import hashlib
import importlib.machinery
import importlib.util
import io
import json
import lzma
import os
import re
import sys
import tarfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
MANIFEST = HERE / "gdp-manifest.json"
HELPER = REPO / "packages/walnut/apps/walnut-games/files/sbin/walnut-games-helper"
FIELDS = ("X-Walnut-Edition", "X-Walnut-Release")


class Error(Exception):
    pass


def load(name, path):
    loader = importlib.machinery.SourceFileLoader(name, str(path))
    spec = importlib.util.spec_from_loader(name, loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


def ar_members(data):
    """[(header bytes, name, payload)] of an ar archive"""
    if not data.startswith(b"!<arch>\n"):
        raise Error("not an ar archive")
    out, i = [], 8
    while i < len(data):
        h = data[i:i + 60]
        if len(h) != 60 or h[58:60] != b"`\n":
            raise Error("a broken ar header at %d" % i)
        size = int(h[48:58].decode().strip())
        out.append((h, h[:16].decode().strip().rstrip("/"), data[i + 60:i + 60 + size]))
        i += 60 + size + (size & 1)
    return out


def ar_header(h, size):
    return h[:48] + ("%-10d" % size).encode() + h[58:60]


def decompress(name, blob):
    if name.endswith(".xz"):
        return lzma.decompress(blob), lambda b: lzma.compress(b, format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC32)
    if name.endswith(".gz"):
        return gzip.decompress(blob), lambda b: gzip.compress(b, mtime=0)
    if name.endswith(".tar"):
        return blob, lambda b: b
    raise Error("control member %s: only xz, gzip or none" % name)


def with_fields(control, fields):
    """control text with fields added before Description (refused when it has one already)"""
    lines = control.decode().splitlines()
    have = [f for f in FIELDS if any(x.lower().startswith(f.lower() + ":") for x in lines)]
    if have:
        raise Error("its control has %s already" % ", ".join(have))
    add = ["%s: %s" % (k, v) for k, v in fields.items() if v]
    at = next((i for i, x in enumerate(lines) if x.startswith("Description:")), len(lines))
    return ("\n".join(lines[:at] + add + lines[at:]) + "\n").encode()


def rewrite_control(tar_bytes, fields):
    """the control tarball with ./control changed, every member's metadata and order kept"""
    src = tarfile.open(fileobj=io.BytesIO(tar_bytes), mode="r:")
    out = io.BytesIO()
    dst = tarfile.open(fileobj=out, mode="w:", format=src.format)
    found = False
    for m in src.getmembers():
        data = src.extractfile(m).read() if m.isfile() else None
        if m.isfile() and m.name.removeprefix("./") == "control":
            data = with_fields(data, fields)
            m.size = len(data)
            found = True
        dst.addfile(m, io.BytesIO(data) if data is not None else None)
    dst.close()
    if not found:
        raise Error("no ./control in the control member")
    return out.getvalue()


def data_files(member, blob):
    """{path: (size, {algo: hexdigest})} of the regular files of the deb's data member (streamed)"""
    if member.endswith(".xz"):
        f = lzma.open(io.BytesIO(blob))
    elif member.endswith(".gz"):
        f = gzip.open(io.BytesIO(blob))
    elif member.endswith(".tar"):
        f = io.BytesIO(blob)
    else:
        raise Error("data member %s: only xz, gzip or none" % member)
    out = {}
    with tarfile.open(fileobj=f, mode="r|") as tf:
        for m in tf:
            if m.isfile():
                hs = {k: hashlib.new(k) for k in ("sha256", "sha1", "md5")}
                x = tf.extractfile(m)
                while chunk := x.read(1 << 20):
                    for h in hs.values():
                        h.update(chunk)
                out[m.name.removeprefix("./")] = (m.size, {k: h.hexdigest() for k, h in hs.items()})
    return out


def control_of(tar_bytes):
    with tarfile.open(fileobj=io.BytesIO(tar_bytes), mode="r:") as tf:
        for m in tf.getmembers():
            if m.isfile() and m.name.removeprefix("./") == "control":
                return tf.extractfile(m).read().decode()
    raise Error("no ./control in the control member")


def manifest_release(files, pkg, manifest, helper, warn):
    """the helper's X-Walnut-Release for the deb's files ({path: (size, digests)}), or None"""
    entry = manifest.get("packages", {}).get(pkg)
    if not entry:
        return None
    found = {}
    for path, (size, dig) in files.items():
        alts = entry["files"].get(path)
        if alts is None:
            continue
        hit = [a for a in alts if a.get("size") == size
               and all(dig[k] == a[k].lower() for k in helper.HASHES if k in a)]
        if not hit:
            warn("%s: %s matches no alternative of the manifest: no X-Walnut-Release" % (pkg, path))
            return None
        rel = hit[0].get("release")
        found[path] = rel if isinstance(rel, str) and helper.RELEASE_RE.match(rel) else None
    missing = [p for p in entry.get("required") or [] if p not in found]
    if missing:
        warn("%s: the deb lacks the manifest's required %s: no X-Walnut-Release" % (pkg, missing[0]))
        return None
    return helper.release_label(found, entry)


def stamp(deb, fields_of, manifest, helper, warn=lambda m: print("stamp-data-deb: " + m, file=sys.stderr)):
    """deb rewritten in place with its X-Walnut fields; returns them"""
    deb = Path(deb)
    raw = deb.read_bytes()
    members = ar_members(raw)
    names = [n for _, n, _ in members]
    if len(names) != 3 or names[0] != "debian-binary" or not names[1].startswith("control.tar") \
            or not names[2].startswith("data.tar"):
        raise Error("%s: not a deb (debian-binary, control.tar.*, data.tar.*)" % deb.name)
    h, name, blob = members[1]
    tar_bytes, comp = decompress(name, blob)
    m = re.search(r"(?m)^Package:[ \t]*(\S+)", control_of(tar_bytes))
    if not m:
        raise Error("%s: its control has no Package" % deb.name)
    pkg = m.group(1)
    fields = dict(fields_of(pkg))
    if not fields.get("X-Walnut-Edition"):
        raise Error("%s: gamedb gives %s no edition ([data_setup] editions or its [[licensed]] edition)" % (deb.name, pkg))
    if not fields.get("X-Walnut-Release"):
        fields["X-Walnut-Release"] = manifest_release(data_files(names[2], members[2][2]), pkg, manifest, helper, warn)
    new = comp(rewrite_control(tar_bytes, fields))
    members[1] = (ar_header(h, len(new)), name, new)
    out = bytearray(b"!<arch>\n")
    for hh, _, payload in members:
        out += hh + payload + (b"\n" if len(payload) & 1 else b"")
    tmp = deb.with_name(deb.name + ".stamp")
    tmp.write_bytes(bytes(out))
    os.replace(tmp, deb)
    return {k: v for k, v in fields.items() if v}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("debs", nargs="+")
    a = ap.parse_args(argv)
    bi = load("build_info", HERE / "build-info.py")
    helper = load("walnut_games_helper", HELPER)
    manifest = json.loads(MANIFEST.read_text())
    try:
        for d in a.debs:
            got = stamp(d, lambda p: bi.data_fields(bi.GAMEDB, p), manifest, helper)
            print("%s: %s" % (Path(d).name, ", ".join("%s: %s" % kv for kv in got.items())))
    except (Error, bi.Error, tarfile.TarError, lzma.LZMAError, OSError, UnicodeDecodeError) as e:
        print("stamp-data-deb.py: %s" % e, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
