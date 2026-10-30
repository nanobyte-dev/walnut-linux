#!/usr/bin/env python3
"""submissions.py pull|list|show|accept|reject: testers' sessions (`walnut-rate submit`) from walnut.nanobyte.dev's
quarantine into gamedb, each one accepted or rejected by the maintainer first (docs/GAMEDB.md "How a game is tested",
docs/games/SUITE.md "Played sessions"). Provisional, PLAN 5.12: the transport, the credit and the weight are the plan's
recommended answers, the user hasn't settled them.

  pull [--from SRC]       fetch the bundles the receiver kept (servers/walnut.nanobyte.dev/rate/walnut-rate-receiver)
                          that aren't here yet: SRC is HOST:DIR over ssh (default WALNUT_HOST from config.local.mk
                          and REMOTE_DIR) or a local directory; each one checked again here (the receiver's own
                          check_bundle and its sha256), then the server's copies of the ones already decided removed
  list                    what is here: id, game, nickname, publish yes/no, size, state (new, unpacked, reviewed:
                          its REVIEW.md written, accepted, rejected)
  show ID                 the bundle's files, the submitter's answers (the contact too: for us only), and the run
                          unpacked into build/bench/<run>/ for the review, with the next steps
  accept ID [--session NAME] [--launcher NAME] [--content DIR]
                          import-run.py on the unpacked run once it has its REVIEW.md (tools/gamedb/review.py, by the
                          importing agent as for ours): result.toml gets source = "submitted" and the tester's nickname
                          (none: anonymous), raw/import.toml the submission id; refused when the tester said no to
                          publishing. Then the server's copy goes
  reject ID [--why TEXT]  never imported; the server's copy goes, the local one moves to rejected/

Nothing in a bundle is run; it is unpacked only after the format check, with tarfile's "data" filter. The local store
(WALNUT_SUBMISSIONS, default build/submissions/) holds inbox/<id>.tar and <id>.json, rejected/, and decisions.tsv
(id, accepted|rejected, when, where it went or why, removed: the server's copy is gone). The server keeps every
bundle until it is decided, so the store can be pulled again. stdlib only (plus rsync and ssh for pull).
"""
import argparse
import datetime
import hashlib
import importlib.machinery
import importlib.util
import json
import os
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
RECEIVER = REPO / "servers" / "walnut.nanobyte.dev" / "rate" / "walnut-rate-receiver"
REMOTE_DIR = "/data/nanobyte/Walnut/submissions"
MARK = "submission.txt"          # in an unpacked run: the id it came from


def receiver():
    loader = importlib.machinery.SourceFileLoader("walnut_rate_receiver", str(RECEIVER))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    m = importlib.util.module_from_spec(spec)
    loader.exec_module(m)
    return m


RX = receiver()


def import_run_module():
    spec = importlib.util.spec_from_file_location("import_run", HERE / "import-run.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def store():
    s = Path(os.environ.get("WALNUT_SUBMISSIONS") or REPO / "build" / "submissions")
    for d in ("inbox", "rejected"):
        (s / d).mkdir(parents=True, exist_ok=True)
    return s


def bench_dir():
    return Path(os.environ.get("WALNUT_SUBMISSIONS_BENCH") or REPO / "build" / "bench")


def stamp():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


# ---- decisions -------------------------------------------------------------------------------------

def decisions(s):
    """{id: {state, when, what, removed}} of decisions.tsv (the last line of an id wins)"""
    out = {}
    f = s / "decisions.tsv"
    for line in f.read_text().splitlines() if f.exists() else []:
        p = line.split("\t")
        if len(p) >= 4 and RX.ID_RE.match(p[0]):
            if p[1] == "removed":
                if p[0] in out:
                    out[p[0]]["removed"] = True
                continue
            out[p[0]] = {"state": p[1], "when": p[2], "what": p[3], "removed": False}
    return out


def decide(s, sid, state, what):
    with open(s / "decisions.tsv", "a") as f:
        f.write("%s\t%s\t%s\t%s\n" % (sid, state, stamp(), " ".join(str(what).split())))


# ---- the source: HOST:DIR over ssh, or a local directory ---------------------------------------------

def default_source():
    v = os.environ.get("WALNUT_SUBMISSIONS_FROM")
    if v:
        return v
    p = subprocess.run([str(REPO / "tools" / "config-get.sh"), "WALNUT_HOST"], capture_output=True, text=True)
    host = p.stdout.strip()
    if p.returncode or not host:
        raise SystemExit("!! no WALNUT_HOST (config.local.mk), and no --from")
    return "%s:%s" % (host, REMOTE_DIR)


def split_source(src):
    """(host or None, directory)"""
    if Path(src).is_dir():
        return None, str(Path(src))
    host, sep, d = src.partition(":")
    if not sep or not host or not d.startswith("/") or host.startswith("-"):
        raise SystemExit("!! --from %s: HOST:/DIR or a local directory" % src)
    return host, d


SSH = ["ssh", "-o", "BatchMode=yes"]


def remote_names(src):
    host, d = split_source(src)
    if host is None:
        return sorted(os.listdir(d))
    p = subprocess.run(SSH + [host, "ls", "-1", "--", d], capture_output=True, text=True, timeout=120)
    if p.returncode:
        raise SystemExit("!! ssh %s ls %s: %s" % (host, d, p.stderr.strip()[-300:]))
    return sorted(p.stdout.split())


def fetch(src, names, dest):
    """the named files of the source into dest (rsync, times kept, never a link followed)"""
    host, d = split_source(src)
    base = (d if host is None else "%s:%s" % (host, d)).rstrip("/") + "/"
    cmd = ["rsync", "-t", "--files-from=-", "--no-links", "--chmod=F644"]
    if host is not None:
        cmd += ["-e", " ".join(SSH)]
    p = subprocess.run(cmd + [base, str(dest) + "/"], input="".join(n + "\n" for n in names), text=True,
                       capture_output=True, timeout=3600)
    if p.returncode:
        raise SystemExit("!! rsync from %s: %s" % (base, p.stderr.strip()[-300:]))


def remove_remote(src, sid):
    """the server's copy of sid removed; True when it is gone"""
    if not RX.ID_RE.match(sid):
        return False
    host, d = split_source(src)
    files = ["%s/%s.tar" % (d, sid), "%s/%s.json" % (d, sid)]
    if host is None:
        for f in files:
            Path(f).unlink(missing_ok=True)
        return True
    p = subprocess.run(SSH + [host, "rm", "-f", "--"] + files, capture_output=True, text=True, timeout=120)
    if p.returncode:
        print("!! couldn't remove %s on %s (%s): the next pull tries again" % (sid, host, p.stderr.strip()[-200:]))
    return p.returncode == 0


# ---- the bundles here ------------------------------------------------------------------------------

def verify(tar, meta):
    """the receiver's check again, and the sha256 the receiver recorded; the check's info, or raises RX.Refused"""
    data = Path(tar).read_bytes()
    if hashlib.sha256(data).hexdigest() != meta.get("sha256"):
        raise RX.Refused("its sha256 isn't the one the receiver recorded")
    info = RX.check_bundle(data)
    if info["run"] != meta.get("run"):
        raise RX.Refused("the run isn't the one the receiver recorded")
    return info


def load(s, sid):
    """(tar, meta) of a bundle here, or SystemExit"""
    if not RX.ID_RE.match(sid):
        raise SystemExit("!! %r is not a submission id (submissions.py list)" % sid[:80])
    tar, js = s / "inbox" / (sid + ".tar"), s / "inbox" / (sid + ".json")
    if not tar.is_file() or not js.is_file():
        raise SystemExit("!! %s is not here (submissions.py pull, then list)" % sid)
    return tar, json.loads(js.read_text())


def submitter_of(tar, run):
    import tomllib
    with tarfile.open(tar, "r:") as t:
        return tomllib.loads(t.extractfile("%s/%s" % (run, RX.SUBMITTER)).read().decode())


def unpack(s, sid, tar, meta):
    """the run unpacked into build/bench/<run>/ (once), marked with its id; its path"""
    info = verify(tar, meta)
    run = bench_dir() / info["run"]
    if run.exists():
        mark = run / MARK
        if not mark.is_file() or mark.read_text().strip() != sid:
            raise SystemExit("!! %s exists and isn't submission %s: move it away first" % (run, sid))
        return run
    bench_dir().mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".submission-", dir=bench_dir()) as tmp:
        with tarfile.open(tar, "r:") as t:
            t.extractall(tmp, filter="data")
        (Path(tmp) / info["run"] / MARK).write_text(sid + "\n")
        os.rename(Path(tmp) / info["run"], run)
    return run


def state_of(s, sid, meta, dec):
    if sid in dec:
        return dec[sid]["state"]
    run = bench_dir() / str(meta.get("run", ""))
    if (run / MARK).is_file() and (run / MARK).read_text().strip() == sid:
        return "reviewed" if (run / "REVIEW.md").is_file() else "unpacked"
    return "new"


# ---- the commands ----------------------------------------------------------------------------------

def cmd_pull(a):
    s = store()
    src = a.source or default_source()
    names = set(remote_names(src))
    dec = decisions(s)
    ready = sorted(n[:-5] for n in names if n.endswith(".json") and RX.ID_RE.match(n[:-5]) and n[:-5] + ".tar" in names)
    new = [i for i in ready if i not in dec and not (s / "inbox" / (i + ".json")).exists()]
    got = bad = 0
    if new:
        with tempfile.TemporaryDirectory(prefix=".pull-", dir=s) as tmp:
            fetch(src, [i + ext for i in new for ext in (".tar", ".json")], tmp)
            for sid in new:
                tar, js = Path(tmp) / (sid + ".tar"), Path(tmp) / (sid + ".json")
                try:
                    meta = json.loads(js.read_text())
                    if meta.get("id") != sid:
                        raise RX.Refused("its record names another id")
                    info = verify(tar, meta)
                except (OSError, ValueError, RX.Refused) as e:
                    print("!! %s: %s; left on the server, not taken" % (sid, e))
                    bad += 1
                    continue
                os.replace(tar, s / "inbox" / (sid + ".tar"))
                os.replace(js, s / "inbox" / (sid + ".json"))
                got += 1
                print(">> %s: %s, %s, %s" % (sid, info["game"], info["nickname"] or "anonymous",
                                            "publish yes" if info["publish"] else "publish NO"))
    gone = 0
    for sid, d in dec.items():
        if not d["removed"] and (sid + ".tar" in names or sid + ".json" in names):
            if remove_remote(src, sid):
                decide(s, sid, "removed", "the server's copy")
                gone += 1
        elif not d["removed"]:
            decide(s, sid, "removed", "not on the server any more")
    print("pull: %d new, %d refused, %d waiting on the server in all, %d decided ones removed there"
          % (got, bad, len(ready), gone))
    return 1 if bad else 0


def cmd_list(a):
    s = store()
    dec = decisions(s)
    rows = []
    for js in sorted((s / "inbox").glob("*.json")) + sorted((s / "rejected").glob("*.json")):
        m = json.loads(js.read_text())
        rows.append((m["id"], m.get("game", "?"), m.get("nickname") or "-", "yes" if m.get("publish") else "no",
                     "%.1f MB" % (m.get("bytes", 0) / 1048576), state_of(s, m["id"], m, dec)))
    if not rows:
        print("no submissions here (submissions.py pull)")
        return 0
    print("%-26s  %-16s  %-16s  %-7s  %-8s  %s" % ("ID", "GAME", "NICKNAME", "PUBLISH", "SIZE", "STATE"))
    for r in rows:
        print("%-26s  %-16s  %-16s  %-7s  %-8s  %s" % r)
    return 0


def cmd_show(a):
    s = store()
    tar, meta = load(s, a.id)
    info = verify(tar, meta)
    sub = submitter_of(tar, info["run"])
    print("%s: %s, received %s, %d bytes, %d files" % (a.id, info["run"], meta.get("received"), meta["bytes"],
                                                       info["files"]))
    print("  nickname %r, publish %s, walnut-rate %s, contact (ours only): %s" % (
        sub.get("nickname", ""), "yes" if sub.get("publish") else "no", sub.get("walnut_rate", "?"),
        sub.get("contact") or "none"))
    with tarfile.open(tar, "r:") as t:
        for m in t.getmembers():
            if m.isfile():
                print("  %9d  %s" % (m.size, m.name))
    run = unpack(s, a.id, tar, meta)
    print("unpacked: %s" % (run.relative_to(REPO) if run.is_relative_to(REPO) else run))
    next_steps(a.id, run, info)
    return 0


def next_steps(sid, run, info):
    rel = run.relative_to(REPO) if run.is_relative_to(REPO) else run
    print("next, as for our own sessions (docs/games/SUITE.md \"Played sessions\"): the PC reference if the game has "
          "none (make bench-ref GAME=%s), then python3 tools/gamedb/review.py %s and its questions answered by "
          "looking; then submissions.py accept %s (or reject %s --why ...)" % (info["game"], rel, sid, sid))


def cmd_accept(a):
    s = store()
    dec = decisions(s)
    if a.id in dec:
        raise SystemExit("!! %s was %s already (%s)" % (a.id, dec[a.id]["state"], dec[a.id]["when"]))
    tar, meta = load(s, a.id)
    info = verify(tar, meta)
    if not info["publish"]:
        raise SystemExit("!! %s: the tester said no to publishing it: reject it (or ask them, if they left a "
                         "contact: submissions.py show %s)" % (a.id, a.id))
    run = unpack(s, a.id, tar, meta)
    if not (run / "REVIEW.md").is_file():
        print("%s: unpacked in %s, no REVIEW.md yet" % (a.id, run))
        next_steps(a.id, run, info)
        return 1
    ir = import_run_module()
    content = Path(a.content) if a.content else REPO / "gamedb" / "games"
    try:
        ir.import_run(run, info["game"], content, launcher=a.launcher, session=a.session,
                      tester=info["nickname"], source="submitted", submission=a.id)
    except ir.Refused as e:
        raise SystemExit("!! %s" % e)
    decide(s, a.id, "accepted", "%s %s" % (info["game"], run.name))
    if remove_remote(a.source or default_source(), a.id):
        decide(s, a.id, "removed", "the server's copy")
    print("accepted %s: credited to %s; commit the session folder (gamedb/games/%s/rating/sessions/)"
          % (a.id, info["nickname"] or "a tester", info["game"]))
    return 0


def cmd_reject(a):
    s = store()
    dec = decisions(s)
    if a.id in dec:
        raise SystemExit("!! %s was %s already (%s)" % (a.id, dec[a.id]["state"], dec[a.id]["when"]))
    tar, meta = load(s, a.id)
    decide(s, a.id, "rejected", a.why or "-")
    for f in (tar, tar.with_suffix(".json")):
        os.replace(f, s / "rejected" / f.name)
    if remove_remote(a.source or default_source(), a.id):
        decide(s, a.id, "removed", "the server's copy")
    print("rejected %s%s" % (a.id, ": " + a.why if a.why else ""))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pull")
    p.add_argument("--from", dest="source", help="HOST:DIR (ssh) or a local directory")
    sub.add_parser("list")
    p = sub.add_parser("show")
    p.add_argument("id")
    p = sub.add_parser("accept")
    p.add_argument("id")
    p.add_argument("--session", help="the session folder's name (import-run.py --session)")
    p.add_argument("--launcher", help="import-run.py --launcher, for a run that recorded none")
    p.add_argument("--content", help="the games' directory (default gamedb/games)")
    p.add_argument("--from", dest="source", help="where the server's copy is (as for pull)")
    p = sub.add_parser("reject")
    p.add_argument("id")
    p.add_argument("--why")
    p.add_argument("--from", dest="source", help="where the server's copy is (as for pull)")
    a = ap.parse_args(argv)
    return {"pull": cmd_pull, "list": cmd_list, "show": cmd_show, "accept": cmd_accept, "reject": cmd_reject}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
