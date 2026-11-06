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




class Budget:
    """the bytes still allowed to be decompressed for one top-level blob, across members and levels"""

    def __init__(self, total):
        self.left = total
        self.over = False


def _take(read, budget):
    """read(n) with n = left + 1; the bytes, cut to the budget (and the budget reduced); over is set when cut"""
    b = read(budget.left + 1)
    if len(b) > budget.left:
        b = b[:budget.left]
        budget.over = True
    budget.left -= len(b)
    return b


def _loop(make_dec, data, budget):
    """multi-member / multi-stream decompression with a bounded output: [bytes] one entry per member or stream
    (each is checked on its own: a dump behind another member starts at that member's offset 0)"""
    out = []
    while data and not budget.over:
        dec = make_dec()
        try:
            chunk = dec.decompress(data, budget.left + 1)
        except (zlib.error, OSError, EOFError, lzma.LZMAError, ValueError):
            if out:   # padding or garbage after a good member ends the stream; what was decoded stays checked
                break
            raise
        if len(chunk) > budget.left:
            chunk = chunk[:budget.left]
            budget.over = True
        budget.left -= len(chunk)
        out.append(chunk)
        if not dec.eof:
            break
        data = dec.unused_data.lstrip(b"\0")   # xz stream padding, gzip zero fill
    return out


def _zstd(data, budget):
    """None when no decompressor that honours a bound is here"""
    try:
        from compression import zstd
        return _loop(zstd.ZstdDecompressor, data, budget)
    except ImportError:
        pass
    try:
        import zstandard
        r = zstandard.ZstdDecompressor().stream_reader(io.BytesIO(data), read_across_frames=True)
        return [_take(r.read, budget)]
    except ImportError:
        pass
    try:
        p = subprocess.Popen(["zstd", "-dc", "--no-progress"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL)
    except OSError:
        return None
    try:
        import threading
        threading.Thread(target=lambda: (p.stdin.write(data), p.stdin.close()), daemon=True).start()
        return [_take(p.stdout.read, budget)]   # reads at most the budget (+1), then the process is stopped
    except (OSError, ValueError):
        return None
    finally:
        p.kill()
        p.wait()


def members(data, budget):
    """[(label, member name or None, bytes or None)]: one level of unpacking of DATA by its magic, or None when it
    is no archive. bytes None: could not be looked into (label says why). Every byte read counts against BUDGET;
    when it runs out the prefix is still returned and budget.over is set."""
    out = []
    try:
        if data[:2] == b"\x1f\x8b":
            out += [("gzip member", None, b) for b in _loop(lambda: zlib.decompressobj(31), data, budget)]
        elif data[:6] == b"\xfd7zXZ\x00":
            out += [("xz stream", None, b) for b in _loop(lambda: lzma.LZMADecompressor(memlimit=256 << 20), data, budget)]
        elif data[:3] == b"BZh":
            out += [("bzip2 stream", None, b) for b in _loop(bz2.BZ2Decompressor, data, budget)]
        elif data[:4] == b"\x28\xb5\x2f\xfd":
            d = _zstd(data, budget)
            out += [("zstd frame", None, b) for b in d] if d is not None else [("no zstd decompressor", None, None)]
        elif data[:4] == b"PK\x03\x04":
            z = zipfile.ZipFile(io.BytesIO(data))
            for zi in z.infolist():
                if zi.is_dir():
                    continue
                if budget.over:
                    break
                if zi.flag_bits & 1:
                    out.append(("encrypted zip member", zi.filename, None))
                    continue
                with z.open(zi) as f:
                    out.append(("zip member", zi.filename, _take(f.read, budget)))
        elif data[257:262] == b"ustar":
            t = tarfile.open(fileobj=io.BytesIO(data), mode="r:")
            for ti in t:
                if budget.over:
                    break
                if ti.isreg():
                    out.append(("tar member", ti.name, _take(t.extractfile(ti).read, budget)))
        else:
            return None
    except Exception as e:   # a damaged or exotic archive (Deflate64, a bad header): a note, never a crash
        return out + [("unreadable archive (%s)" % type(e).__name__, None, None)]
    return out


def check_blob(fps, data, name="", depth=DEPTH, budget=None, game_data=None):
    """(hits, notes): hits [(where, what, kind)] for matches in DATA or in what it unpacks to, kind "nor" (a
    fingerprint) or "data" (game_data, a module with match() and magic(): a member's name or first bytes);
    notes [text] for what could not be looked into. fps: a list of load()ed fingerprints. One budget (CAP bytes)
    covers the whole walk of one blob."""
    budget = budget or Budget(CAP)
    hits, notes = [], []
    for fp in fps:
        for h in match(fp, data):
            hits.append((name or "the file", h, "nor"))
    if game_data is not None and depth < DEPTH:   # the top-level bytes are the caller's own business
        kind = game_data.magic(data[:1 << 20])
        if kind:
            hits.append((name, "%s by its first bytes" % kind, "data"))
    if depth <= 0:
        probe = Budget(0)
        if members(data[:512], probe) is not None:
            notes.append("%s: an archive nested deeper than %d levels, not looked into" % (name or "the file", DEPTH))
        return hits, notes
    ms = members(data, budget)
    if ms is None:
        return hits, notes
    for label, mname, b in ms:
        sub = "%s > %s" % (name or "the file", mname or label)
        if game_data is not None and mname and game_data.match(mname):
            hits.append((sub, "game data by its name (%s)" % game_data.match(mname), "data"))
        if b is None:
            notes.append("%s: %s, not looked into" % (sub, label))
            continue
        h2, n2 = check_blob(fps, b, sub, depth - 1, budget, game_data)
        hits += h2
        notes += n2
    if budget.over:
        notes.append("%s: the walk stopped after %d MB decompressed in all (members and levels together); the rest was not looked into" % (name or "the file", CAP >> 20))
    return hits, notes


LFS_MAGIC = b"version https://git-lfs.github.com/spec/v1"


def lfs_oid(data):
    """the SHA-256 (hex) a Git LFS pointer names, else None"""
    if len(data) > 1024 or not data.startswith(LFS_MAGIC):
        return None
    for line in data.decode("ascii", "replace").splitlines():
        if line.startswith("oid sha256:"):
            oid = line[11:].strip().lower()
            if len(oid) == 64 and all(c in "0123456789abcdef" for c in oid):
                return oid
    return None


def lfs_read(lfs_dir, oid):
    """the object's bytes from a git LFS store (<lfs_dir>/<oid[:2]>/<oid[2:4]>/<oid>); None when it isn't there or
    is over the cap"""
    path = os.path.join(lfs_dir, oid[:2], oid[2:4], oid)
    try:
        if os.path.getsize(path) > CAP:
            return None
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return None


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
            for w, h, _ in hits:
                print("MATCH %s: %s" % (w, h))
                rc = 1
            for n in notes:
                print("NOTE " + n)
        return rc
    print(__doc__.split("\n\n")[0] + "\nusage: nor_fingerprint.py make DUMP [-o FILE] | check FILE...", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
