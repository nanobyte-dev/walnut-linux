#!/usr/bin/env python3
"""run-tests.py [--slow] [--ci] [--only NAME] [--list] [-j N]: every host test suite (no PS3 involved) from
tools/tests.toml, then a table of suite, result and seconds; exit 1 when any failed. `tools/run-tests.sh` runs this.

A suite is one [[suite]] block in tools/tests.toml: name, what, run (a command; a list of commands runs in order and
stops at the first failure), and optionally slow, group, alone, needs_cmd, needs_path, skip_note, warn_unless, xfail,
ci_skip. Adding a suite is adding one block there. Output of each suite: build/tmp/tests/<suite>.log (its tail is shown
for failures).

A suite whose prerequisites are missing exits 77 and is "skip"; a part of a suite that cannot run prints
"SKIP suite:part reason" and goes on. Both, and unittest's skipped tests, are listed after the table, so a skip never
reads as a pass.
  --ci     for GitHub's runner: a skip fails unless the suite's ci_skip names its key (the second word of the SKIP
           line: the suite, suite:part or suite:unittest) with a reason. A ci_skip that did not skip is only mentioned.
  --slow   also the suites marked slow (a virtual desktop, a KVM guest)
  --only   one suite
  --list   the suites and what each checks
  -j N     N suites at a time (default 1); suites of one `group` (the Mesa ones share a work tree) never overlap, and
           an `alone` suite (gprefetch reads the page cache, which other suites disturb) runs after the others
A known-failing suite (xfail = "EXIT|REGEX": the repro of a bug the shipped code does not fix yet) must fail with that
exit code and message ("xfail"); a pass (XPASS: fixed? remove the xfail) or any other failure fails the run."""
import argparse
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import tomllib
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOGS = ROOT / "build" / "tmp" / "tests"
REGISTRY = ROOT / "tools" / "tests.toml"
FIELDS = {"name", "what", "run", "slow", "group", "alone", "needs_cmd", "needs_path", "skip_note", "warn_unless", "xfail",
          "ci_skip"}
say_lock = threading.Lock()


def say(text):
    with say_lock:
        print(text, flush=True)


def load_suites(path=REGISTRY):
    suites = tomllib.loads(Path(path).read_text())["suite"]
    names = set()
    for s in suites:
        extra = set(s) - FIELDS
        if extra or "name" not in s or "run" not in s or s["name"] in names:
            sys.exit("%s: suite %r: unknown field %s, or no name or run, or a repeated name" % (path, s.get("name"), sorted(extra)))
        names.add(s["name"])
        bad = check_types(s)
        if bad:
            sys.exit("%s: suite %r: %s" % (path, s["name"], bad))
    return suites


def check_types(s):
    """why suite block S has a wrongly typed field, or ''"""
    def strs(v):
        return isinstance(v, list) and all(isinstance(x, str) for x in v)
    run = s["run"]
    if not (strs(run) and run) and not (isinstance(run, list) and run and all(strs(c) and c for c in run)):
        return "run must be a command (a list of strings) or a list of such commands"
    for k in ("needs_cmd", "needs_path"):
        if k in s and not strs(s[k]):
            return "%s must be a list of strings" % k
    for k in ("name", "what", "group", "skip_note", "warn_unless", "xfail"):
        if k in s and not isinstance(s[k], str):
            return "%s must be a string" % k
    for k in ("slow", "alone"):
        if k in s and not isinstance(s[k], bool):
            return "%s must be true or false" % k
    ci = s.get("ci_skip", {})
    if not isinstance(ci, dict) or not all(isinstance(k, str) and isinstance(v, str) and v for k, v in ci.items()):
        return "ci_skip must be a table of key = \"reason\""
    return ""


def steps(suite):
    run = suite["run"]
    return run if run and isinstance(run[0], list) else [run]


def run_suite(s, logs=LOGS):
    """-> dict(name, res, secs, skips, tail): the suite run, its log written"""
    name, log = s["name"], logs / (s["name"] + ".log")
    t0 = time.monotonic()
    rc = 0
    missing = [("no " + c) for c in s.get("needs_cmd", []) if not shutil.which(c)]
    missing += ["no " + p for p in s.get("needs_path", []) if not (ROOT / p).exists()]
    if missing:
        log.write_text("skip: %s\n" % s.get("skip_note", ", ".join(missing)))
        rc = 77
    else:
        env = dict(os.environ, TEST_SUITE=name)
        with log.open("wb") as f:
            for cmd in steps(s):
                try:
                    rc = subprocess.run(cmd, cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT).returncode
                except OSError as e:     # a missing program: this suite fails, the run goes on
                    f.write(("%s: %s\n" % (cmd[0], e)).encode())
                    rc = 127
                if rc:
                    break
    text = log.read_text(errors="replace")
    lines = text.splitlines()
    out = {"name": name, "secs": int(time.monotonic() - t0), "skips": [l for l in lines if l.startswith("SKIP ")],
           "tail": "\n".join(lines[-25:]), "msg": [], "fail": False}
    ok = [m for m in re.findall(r"^OK \(skipped=(\d+)", text, re.M)]
    if ok:
        out["skips"].append("SKIP %s:unittest %s tests skipped (see its log)" % (name, ok[-1]))
    xfail = s.get("xfail")
    if rc == 77:
        out["res"] = "skip"
        last = lines[-1] if lines else ""
        out["msg"].append(last)
        if not any(l.startswith("SKIP ") for l in lines):
            out["skips"].append(("SKIP %s %s" % (name, last[6:] if last.startswith("skip: ") else "")))
    elif xfail:
        code, _, rx = xfail.partition("|")
        if str(rc) == code and re.search(rx, text, re.M):
            out["res"] = "xfail"
        elif rc == 0:
            out.update(res="XPASS", fail=True)
            out["msg"].append("   known-failing suite passed: fixed? take its xfail off")
        else:
            out.update(res="FAIL", fail=True)
            out["msg"] += ["   known-failing suite: exit %d, want %s with /%s/" % (rc, code, rx), out["tail"]]
    elif rc == 0:
        out["res"] = "pass"
    else:
        out.update(res="FAIL", fail=True)
        out["msg"].append(out["tail"])
    wu = s.get("warn_unless")
    if wu and text.strip() and wu not in text:
        out["msg"].append("   warn: %s (%s)" % (lines[-1], os.path.relpath(log, ROOT) if log.is_relative_to(ROOT) else log))
    return out


def ci_check(selected, results, only):
    """-> (failed, notes): every skip must be listed in its suite's ci_skip"""
    failed, notes = False, []
    for s, r in zip(selected, results):
        allowed, used = s.get("ci_skip", {}), set()
        for line in r["skips"]:
            key = line[5:].split(" ", 1)[0]
            if key in allowed:
                used.add(key)
            else:
                notes.append("!! skipped in CI and not in %s's ci_skip (tools/tests.toml): %s" % (s["name"], key))
                failed = True
        if not only:
            notes += ["-- tools/tests.toml lists a ci_skip %s, which did not skip" % k for k in allowed if k not in used]
    return failed, notes


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--slow", action="store_true")
    ap.add_argument("--ci", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("-j", type=int, default=1)
    a = ap.parse_args(argv)
    suites = load_suites()
    if a.list:
        for s in suites:
            print("%-26s %s%s" % (s["name"], "[slow] " if s.get("slow") else "", s.get("what", "")))
        return 0
    selected = [s for s in suites if (a.slow or not s.get("slow")) and (not a.only or a.only == s["name"])]
    if not selected:
        print("no suite %s (tools/run-tests.sh --list)" % (repr(a.only) if a.only else "selected"), file=sys.stderr)
        return 2
    LOGS.mkdir(parents=True, exist_ok=True)
    group_locks = {g: threading.Lock() for g in {s["group"] for s in selected if "group" in s}}

    def job(s):
        lock = group_locks.get(s.get("group"))
        with lock if lock else threading.Lock():
            say("== " + s["name"])
            r = run_suite(s)
        for m in r["msg"]:
            say(m)
        return r

    together = [s for s in selected if not s.get("alone") or a.j == 1]
    with ThreadPoolExecutor(max_workers=max(1, a.j)) as pool:
        done = dict(zip((s["name"] for s in together), pool.map(job, together)))
    for s in selected:
        if s["name"] not in done:
            done[s["name"]] = job(s)
    results = [done[s["name"]] for s in selected]
    fail = any(r["fail"] for r in results)
    print("\n%-26s %-6s %s" % ("suite", "result", "seconds"))
    for r in results:
        print("%-26s %-6s %s" % (r["name"], r["res"], r["secs"]))
    skips = [l for r in results for l in r["skips"]]
    if skips:
        print("skipped (not passes):")
        for l in skips:
            print("  " + l)
    if a.ci:
        failed, notes = ci_check(selected, results, a.only)
        fail = fail or failed
        for n in notes:
            print(n)
    print("all passed" if not fail else "FAILED (logs in build/tmp/tests)")
    return int(fail)


if __name__ == "__main__":
    sys.exit(main())
