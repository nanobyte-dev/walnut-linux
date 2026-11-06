#!/usr/bin/env python3
"""nor_fingerprint.py: a content check for the NOR dump, which the path and extension rules can't see
(a renamed or compressed copy). The fingerprint is a private file (tools/nor-fingerprint.local.txt, untracked,
mode 600, beside the replace list; $WALNUT_NOR_FINGERPRINT adds a second one): the dump's size and SHA-256 and a few
hashed windows (offset, length, SHA-256 of that slice). It holds no byte of the dump. A blob matches when
its size and SHA-256 are the dump's or when any window's hash matches the blob's bytes at that offset.
.gz/.xz/.bz2/.zst/.zip/.tar members are looked into (found by their magic bytes, not their names), two levels
deep, up to 64 MB of output per blob.

  python3 -I tools/nor_fingerprint.py make DUMP [-o FILE]    write the fingerprint of DUMP (default: the .local.txt)
  python3 -I tools/nor_fingerprint.py check FILE...          which files match (exit 1 if any)
The commit hook (commit_tests/checks/private.py) and tools/public-scan.py import check_blob()."""
import bz2
import hashlib
import io
import lzma
import os
import subprocess
import sys
import tarfile
import zipfile
import zlib

DEFAULT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "nor-fingerprint.local.txt")
CAP = 64 << 20
DEPTH = 2
WINDOW = 64
WINDOWS = 8


def sha(b):
    return hashlib.sha256(b).hexdigest()


def load(path):
    """{'size': int, 'sha256': hex, 'win': [(offset, length, hex)]}; ValueError on a bad line"""
    fp = {"size": None, "sha256": None, "win": []}
    with open(path, encoding="ascii") as f:
        for n, line in enumerate(f, 1):
            w = line.split("#", 1)[0].split()
            if not w:
                continue
            try:
                if w[0] == "size":
                    fp["size"] = int(w[1])
                elif w[0] == "sha256":
                    fp["sha256"] = w[1].lower()
                elif w[0] == "win":
                    fp["win"].append((int(w[1]), int(w[2]), w[3].lower()))
                else:
                    raise ValueError(w[0])
            except (ValueError, IndexError):
                raise ValueError("%s line %d: not a fingerprint line" % (path, n))
    if not fp["win"] and not fp["sha256"]:
        raise ValueError("%s: no signature in it" % path)
    return fp


def match(fp, data):
    """what of the fingerprint this exact byte string matches: [] or a list of short descriptions (no content)"""
    hits = []
    if fp["sha256"] and len(data) == fp["size"] and sha(data) == fp["sha256"]:
        hits.append("the whole dump")
    for off, ln, h in fp["win"]:
        if len(data) >= off + ln and sha(data[off:off + ln]) == h:
            hits.append("the window at offset %d" % off)
    return hits




def _zstd(data, room):
    try:
        from compression import zstd
        d = zstd.ZstdDecompressor()
        return d.decompress(data, room + 1)
    except ImportError:
        pass
    try:
        import zstandard
        return zstandard.ZstdDecompressor().decompressobj().decompress(data)[:room + 1]
    except ImportError:
        pass
    try:
        r = subprocess.run(["zstd", "-dc", "--no-progress"], input=data, capture_output=True, timeout=60)
        return r.stdout[:room + 1] if r.returncode == 0 else None
    except (OSError, subprocess.SubprocessError):
        return None


def members(data, room):
    """[(label, bytes)] one level of unpacking of DATA by its magic, or None when it is no archive; the string
    'truncated' / 'unreadable' / 'no zstd' as a label of None bytes when it could not be fully looked into"""
    out = []
    try:
        if data[:2] == b"\x1f\x8b":
            d = zlib.decompressobj(31).decompress(data, room + 1)
            out.append(("gzip", d))
        elif data[:6] == b"\xfd7zXZ\x00":
            out.append(("xz", lzma.LZMADecompressor().decompress(data, room + 1)))
        elif data[:3] == b"BZh":
            out.append(("bzip2", bz2.BZ2Decompressor().decompress(data, room + 1)))
        elif data[:4] == b"\x28\xb5\x2f\xfd":
            d = _zstd(data, room)
            if d is None:
                return [("no zstd decompressor", None)]
            out.append(("zstd", d))
        elif data[:4] == b"PK\x03\x04":
            z = zipfile.ZipFile(io.BytesIO(data))
            for zi in z.infolist():
                if zi.is_dir():
                    continue
                if zi.flag_bits & 1:
                    out.append(("encrypted zip member", None))
                elif zi.file_size > room:
                    out.append(("zip member over the cap", None))
                else:
                    out.append(("zip member", z.read(zi)))
        elif data[257:262] == b"ustar" or data[257:263] == b"ustar\x00":
            t = tarfile.open(fileobj=io.BytesIO(data), mode="r:")
            for ti in t:
                if not ti.isreg():
                    continue
                if ti.size > room:
                    out.append(("tar member over the cap", None))
                    continue
                out.append(("tar member", t.extractfile(ti).read()))
        else:
            return None
    except (OSError, EOFError, ValueError, zlib.error, lzma.LZMAError, zipfile.BadZipFile, tarfile.TarError):
        return [("unreadable archive", None)]
    res = []
    for label, b in out:
        if b is not None and len(b) > room:
            res.append(("output over the cap", None))
        else:
            res.append((label, b))
    return res


def check_blob(fps, data, name="", depth=DEPTH, room=None):
    """(hits, notes): hits [(where, what)] for matches in DATA or in what it unpacks to; notes [text] for what
    could not be looked into. fps: a list of load()ed fingerprints."""
    room = CAP if room is None else room
    hits, notes = [], []
    for fp in fps:
        for h in match(fp, data):
            hits.append((name or "the file", h))
    if depth <= 0:
        if members(data, 0) is not None and room > 0:
            notes.append("%s: archive nested deeper than %d levels, not looked into" % (name or "the file", DEPTH))
        return hits, notes
    ms = members(data, room)
    if ms is None:
        return hits, notes
    for label, b in ms:
        sub = "%s > %s" % (name or "the file", label)
        if b is None:
            notes.append("%s: %s, not looked into" % (sub, label))
            continue
        room -= len(b)
        h2, n2 = check_blob(fps, b, sub, depth - 1, max(room, 0))
        hits += h2
        notes += n2
    return hits, notes


def make(dump):
    with open(dump, "rb") as f:
        data = f.read()
    if len(data) < 1 << 20:
        raise SystemExit("%s is smaller than 1 MiB: not a NOR dump?" % dump)
    lines = ["# NOR dump fingerprint: private, chmod 600, never committed (tools/nor_fingerprint.py).",
             "# Hashes only; no byte of the dump is in this file.", "size %d" % len(data), "sha256 " + sha(data)]
    step = len(data) // (WINDOWS + 1)
    got = 0
    off = step // 2
    while got < WINDOWS and off + WINDOW <= len(data):
        w = data[off:off + WINDOW]
        if len(set(w)) >= 24:   # a window of padding (00/ff) or of a repeating table would match other data
            lines.append("win %d %d %s" % (off, WINDOW, sha(w)))
            got += 1
            off += step
        else:
            off += 256
    if got < 3:
        raise SystemExit("found only %d usable windows in %s" % (got, dump))
    return "\n".join(lines) + "\n"


def main(argv):
    if len(argv) >= 2 and argv[0] == "make":
        out = DEFAULT
        if "-o" in argv:
            out = argv[argv.index("-o") + 1]
        text = make(argv[1])
        fd = os.open(out, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write(text)
        os.chmod(out, 0o600)
        print("wrote %s (%d lines)" % (out, text.count("\n")))
        return 0
    if len(argv) >= 2 and argv[0] == "check":
        fps = [load(os.environ.get("WALNUT_NOR_FINGERPRINT") or DEFAULT)]
        rc = 0
        for p in argv[1:]:
            with open(p, "rb") as f:
                hits, notes = check_blob(fps, f.read(), p)
            for w, h in hits:
                print("MATCH %s: %s" % (w, h))
                rc = 1
            for n in notes:
                print("NOTE " + n)
        return rc
    print(__doc__.split("\n\n")[0] + "\nusage: nor_fingerprint.py make DUMP [-o FILE] | check FILE...", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
