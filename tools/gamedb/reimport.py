#!/usr/bin/env python3
"""reimport.py [--check] [--game ID]... [--unfreeze] [--content DIR]: derive every played session's record
(rating/sessions/<run>/result.toml) again from its evidence alone (gamedb/README.md "games/<id>/rating/"):
the run rebuilt from raw/, shots/, form.toml and review.md (tools/gamedb/evidence.py), imported with the
inputs in raw/import.toml, by tools/gamedb/import-run.py's own derivation. No build/bench needed: a change in
how the numbers are derived is a re-import.

- Writes each result.toml that comes out different, and says so; --check writes nothing and exits 1 when
  any differs (the diff is printed).
- A frozen record (raw/import.toml frozen = true, import-run.py --keep-record: derived by an importer of
  its day whose derivation has changed since, before records carried rules versions) is left as it is;
  --unfreeze derives it as well (its rating moves then: the catalog scores it from the new numbers).
- A session without raw/ (its run was gone when the evidence came in) keeps its record, and is listed.
- Needs the Git LFS objects of shots/ and raw/ (git lfs pull); a pointer is an error.
"""
import argparse
import difflib
import importlib.util
import io
import contextlib
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE.parent.parent / "bench" / d) for d in ("analysis", "harness")] + [str(HERE)]
import check     # noqa: E402
import evidence  # noqa: E402


def load_import_run():
    spec = importlib.util.spec_from_file_location("import_run", HERE / "import-run.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def sessions(content, games=None):
    """(game id, session folder) of every session with a result.toml"""
    for g in sorted(Path(content).iterdir()):
        if not g.is_dir() or (games and g.name not in games):
            continue
        s = g / check.SESSIONS
        for t in sorted(s.iterdir()) if s.is_dir() else []:
            if (t / "result.toml").is_file():
                yield g.name, t


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--content", default=str(check.CONTENT))
    ap.add_argument("--game", action="append", help="only this game (again for more)")
    ap.add_argument("--check", action="store_true", help="write nothing; exit 1 when a record would change")
    ap.add_argument("--unfreeze", action="store_true", help="derive the frozen records too")
    a = ap.parse_args()
    ir = load_import_run()
    counts = {"same": 0, "changed": 0, "frozen": 0, "no raw": 0, "failed": 0}
    for gid, t in sessions(a.content, a.game):
        name = "%s/%s" % (gid, t.name)
        pointers = evidence.lfs_pointers(t)
        if pointers:
            counts["failed"] += 1
            print("!! %s: Git LFS objects not fetched (git lfs pull): %s" % (name, ", ".join(pointers[:3])))
            continue
        opts = evidence.read_import(t)
        if not opts:
            counts["no raw"] += 1
            print("-- %s: no raw/%s: kept as recorded" % (name, evidence.IMPORT))
            continue
        if opts.get("frozen") and not a.unfreeze:
            counts["frozen"] += 1
            continue
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                text, _ = ir.derive_session(t, gid, a.content)
        except ir.Refused as e:
            counts["failed"] += 1
            print("!! %s: %s" % (name, e))
            continue
        old = (t / "result.toml").read_text()
        if text == old:
            counts["same"] += 1
            continue
        counts["changed"] += 1
        print("%s %s%s" % ("!!" if a.check else ">>", name, " (frozen)" if opts.get("frozen") else ""))
        sys.stdout.writelines("   " + x for x in difflib.unified_diff(old.splitlines(True), text.splitlines(True),
                                                                      "recorded", "derived", n=0))
        if not a.check:
            (t / "result.toml").write_text(text)
    print("reimport.py: " + ", ".join("%d %s" % (n, k) for k, n in counts.items() if n or k == "same"))
    return 1 if counts["failed"] or (a.check and counts["changed"]) else 0


if __name__ == "__main__":
    sys.exit(main())
