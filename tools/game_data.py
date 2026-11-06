#!/usr/bin/env python3
"""game_data.py: the licensed games' data formats, one list for the commit hook (commit_tests/checks/private.py)
and the public export (tools/public-export.sh). Matched on the lower-cased path, so case never matters.
  match(path) -> the glob it hits or None;  magic(bytes) -> "a Doom WAD" ... or None
  python3 -I tools/game_data.py < paths     prints the paths that match (the export's history check)"""
import fnmatch
import struct
import sys

GLOBS = ("*.wad", "*.mpq", "*.agg", "*.pk3", "*.pak", "*.grp", "*.lfl", "*.lec", "*.sou", "*.grf",
         "*.sv6", "*.td6", "*.sc6", "g1.dat", "*/g1.dat", "csg1.dat", "*/csg1.dat", "data/*.dat", "*/data/*.dat",
         "objdata/*.dat", "*/objdata/*.dat", "tyrian*.lvl", "*/tyrian*.lvl", "tyrian.hdt", "*/tyrian.hdt",
         "sample.cat", "*/sample.cat", "heroes2*.agg", "gm_tt*.gm", "*/gm_tt*.gm", "9_tt*.fm", "*/9_tt*.fm",
         "*/tentacle.00[01]", "*/monkey.00[01]", "*/monkey2.00[01]", "*/atlantis.00[01]", "*/monster.sou")
MAGIC_MIN = 12


def match(path):
    low = path.lower()
    return next((g for g in GLOBS if fnmatch.fnmatchcase(low, g)), None)


def magic(b):
    """the kind of game data a file's first bytes say it is, else None: Doom WADs, Blizzard MPQs, Quake PAKs
    (PACK + the directory's offset and size, which must fit the file: plain text starting with PACK doesn't)"""
    if b[:4] in (b"IWAD", b"PWAD"):
        return "a Doom WAD"
    if b[:4] == b"MPQ\x1a":
        return "a Blizzard MPQ"
    if b[:4] == b"PACK" and len(b) >= MAGIC_MIN:
        off, ln = struct.unpack("<ii", b[4:12])
        if 12 <= off <= len(b) and ln > 0 and ln % 64 == 0 and off + ln <= len(b):
            return "a Quake PAK"
    return None


if __name__ == "__main__":
    for line in sys.stdin.read().splitlines():
        if line and match(line):
            print(line)
