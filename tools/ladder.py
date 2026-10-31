#!/usr/bin/env python3
"""ladder.py [--top] [--component NAME] [--refresh] [--list] [--manifest PATH]: the fix ladder
(docs/VALIDATION.md). The stability fixes of a component go in one rung at a time, in series order, and
its host tests run at every rung: with each fix more tests pass, and at the top rung all of them.

Expectations come from the patch register (gamedb/patches/*/patch.md): a stability entry's `tests`
name the tests that guard it (a run-tests suite, or one part of it as "suite:part"; "ps3:<program>"
for the PS3 ladder). At rung k a test is expected to pass when every fix it guards is at or below k;
a test that guards no stability fix is expected to pass at every rung. Reported as surprises:
  too early   a test passes below its fix's rung: the repro doesn't reproduce
  after fix   it fails at its fix's own rung: the fix is broken (or the test needs more than the fix)
  regression  it passed at a rung and fails at a later one: that later rung caused it
Exit 1 on any surprise.

Components:
  mesa         rung trees in a clone of build/work/mesa, LADDER_DIR/mesa (LADDER_DIR from the
               environment or config.local.mk, else build/tmp/ladder/trees; never build/work itself). Branch ladder/all is the
               Debian base tag plus every patch file of packages/patched/graphics/mesa in series order;
               the rungs below it come from reverting the stability fixes from the top down. Rung 0
               is the series without its stability fixes. A fix whose revert doesn't apply (later
               patches are written on top of it) can't be taken out of today's series: it is "fused",
               carried in rung 0, and its tests are expected to pass everywhere (its off check stays
               the per-file one, host-tests.sh alloc-fail fix-off, or the PS3 session). The tests are
               packages/patched/graphics/mesa/tests/host-tests.sh with MESA_WORK at the rung's tree.
  ps3rsx       our driver has no series: its validator's fix-off.sh turns each rule off in a copy
               (rung "off", each case must fail) and validate-test runs them all on (rung "on").
  sdl2-compat  stock tarball vs our series, through its suite (close-locked-test.sh).
  luajit       stock vs our deb, through its suite (low-2gb.sh).
Not laddered on the host (no host test of their fix in a tree): the kernel's stability patches (PS3
fault injection session, docs/VALIDATION.md), boost-context, plymouth, pnmixer, webkit2gtk.

--top         the top rung only (every fix, every test must pass): run-tests suite fix-ladder, Mesa only
--refresh     clone build/work/mesa again (after a merge there); the old clone is kept as mesa.prev
--list        rungs, fused fixes and expectations, nothing run
--manifest P  write the PS3 ladder's manifest (rungs, the ps3: tests and their fix rungs) to P
--prepare     build the rung branches (and the manifest) only
--rev REV     Mesa's series (package.conf, series files, patches), the register and Mesa's host tests as
              commit REV of this repository has them, not the working tree (while a merge is going on
              there)
Output: the matrices, and build/tmp/ladder/result.json (tools/gamedb/patches.py warns when its top
rung failed). Test logs: build/tmp/ladder/<component>/<rung>/. Stdlib only.
"""
import argparse
import concurrent.futures
import datetime
import fcntl
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
REGISTER = REPO / "gamedb" / "patches"
MESA_PKG = REPO / "packages" / "patched" / "graphics" / "mesa"
HOST_TESTS = MESA_PKG / "tests" / "host-tests.sh"
OUT = REPO / "build" / "tmp" / "ladder"


def ladder_dir():
    """LADDER_DIR from the environment or config.local.mk (tools/config-get.sh), else build/tmp/ladder/trees."""
    v = os.environ.get("LADDER_DIR")
    if not v:
        r = subprocess.run([str(REPO / "tools" / "config-get.sh"), "LADDER_DIR"], capture_output=True, text=True)
        v = r.stdout.strip() if r.returncode == 0 else ""
    return (REPO / v) if v else OUT / "trees"


LADDER_DIR = ladder_dir()
CLONE = LADDER_DIR / "mesa"
SOURCE = REPO / "build" / "work" / "mesa"
FM_RE = re.compile(r"\A\+\+\+\n(.*?)\n\+\+\+\n", re.S)
GIT_ENV = {"GIT_COMMITTER_NAME": "ladder", "GIT_COMMITTER_EMAIL": "ladder@localhost",
           "GIT_COMMITTER_DATE": "@0 +0000", "GIT_AUTHOR_NAME": "ladder", "GIT_AUTHOR_EMAIL": "ladder@localhost",
           "GIT_AUTHOR_DATE": "@0 +0000"}
JOBS = 4


def log(*a):
    print(*a, flush=True)


def rev_files(prefix):
    """{path: bytes} of the files under PREFIX (a repository path) in commit REV."""
    ls = subprocess.run(["git", "ls-tree", "-r", "--name-only", REV, "--", prefix], cwd=REPO,
                        capture_output=True, text=True, check=True).stdout.split()
    return {p: subprocess.run(["git", "show", f"{REV}:{p}"], cwd=REPO, capture_output=True, check=True).stdout for p in ls}


def register():
    """{slug: front matter} of every register entry (REV's with --rev)."""
    out = {}
    if REV:
        texts = {p: b.decode() for p, b in rev_files("gamedb/patches").items() if p.endswith("/patch.md")}
    else:
        texts = {str(f): f.read_text() for f in REGISTER.glob("*/patch.md")}
    for path, text in sorted(texts.items()):
        m = FM_RE.match(text)
        if m:
            try:
                out[Path(path).parent.name] = tomllib.loads(m.group(1))
            except tomllib.TOMLDecodeError:
                pass
    return out


def stability(reg, component):
    return {s: fm for s, fm in reg.items() if fm.get("component") == component and fm.get("category") == "stability"}


# ---------------------------------------------------------------- expectations and the matrix
class Ladder:
    """rungs: labels, rung 0 first; tests: {test: fix rung} (0 for a test guarding no fix or a fused one)."""

    def __init__(self, name, rungs, tests, guards=None, notes=()):
        self.name, self.rungs, self.tests = name, rungs, tests
        self.guards = guards or {}	# test -> the entries it guards (for the report)
        self.notes = list(notes)
        self.results = {}	# (test, rung index) -> pass | fail | skip | - (not run)

    def surprises(self):
        """[(test, rung, kind, why)]: too-early passes (one line per test), the first failure at or above
        the fix's rung when the test never passed there ("after fix"), each fall from pass to not-pass
        above it ("regression", naming the rung that caused it)."""
        out = []
        for t, fr in self.tests.items():
            early, passed, prev_ok, after = [], False, None, False
            for i, _ in enumerate(self.rungs):
                r = self.results.get((t, i))
                if r is None or r == "-":
                    continue
                ok = r == "pass"
                if i < fr:
                    if ok:
                        early.append(i)
                    continue
                if not ok:
                    if passed and prev_ok:
                        out.append((t, i, "regression", f"{r} at rung {i} ({self.rungs[i]}) after passing at rung {i - 1}: caused by rung {i}"))
                    elif not passed and not after:
                        after = True
                        out.append((t, i, "after fix", f"{r} at rung {i}, its fix's rung is {fr}"
                                    + (": the fix is broken, or the test needs more than its fix" if i == fr else "")))
                passed |= ok
                prev_ok = ok
            if early:
                out.append((t, early[0], "too early", f"passes at rung(s) {', '.join(map(str, early))}, below its fix's rung {fr}: the repro doesn't reproduce"))
        return out

    def cell(self, t, i):
        r = self.results.get((t, i))
        if r is None:
            return "."
        if r == "-":
            return "-"
        exp = i >= self.tests[t]
        ok = r == "pass"
        if ok and exp:
            return "P"
        if not ok and not exp:
            return {"fail": "x", "skip": "s"}.get(r, "x")
        return "!P" if ok else ("!" + {"fail": "F", "skip": "S"}.get(r, "F"))

    def print(self):
        log(f"\n== {self.name}: {len(self.rungs)} rungs")
        for i, r in enumerate(self.rungs):
            log(f"  rung {i:2}  {r}")
        for n in self.notes:
            log("  " + n)
        if not self.tests:
            return
        w = max(len(t) for t in self.tests) + 2
        ran = sorted({i for (_, i) in self.results})
        cols = ran or range(len(self.rungs))
        log("  " + "test".ljust(w) + "fix  " + " ".join(f"{i:>3}" for i in cols))
        for t, fr in self.tests.items():
            log("  " + t.ljust(w) + f"{fr:>3}  " + " ".join(f"{self.cell(t, i):>3}" for i in cols))
        log("  (P pass, x/s fails/skips below its fix as expected, !P too-early pass, !F/!S fails/skips at or above its fix)")


# ---------------------------------------------------------------- Mesa
def git(*a, cwd=None, check=True, env=None):
    e = dict(os.environ, **GIT_ENV, **(env or {}))
    r = subprocess.run(["git", *a], cwd=cwd or CLONE, capture_output=True, text=True, env=e)
    if check and r.returncode:
        raise SystemExit(f"git {' '.join(a)}: {r.stderr.strip()}")
    return r


REV = None	# --rev: the Mesa series, the register and Mesa's host tests as a commit of this repository has them
TESTS_DIR = HOST_TESTS.parent


def pkg_read(rel):
    """A file of packages/patched/graphics/mesa (bytes; None when missing), from REV or the working tree."""
    if REV:
        r = subprocess.run(["git", "show", f"{REV}:{(MESA_PKG / rel).relative_to(REPO).as_posix()}"],
                           cwd=REPO, capture_output=True)
        return r.stdout if r.returncode == 0 else None
    f = MESA_PKG / rel
    return f.read_bytes() if f.is_file() else None


def mesa_series():
    out = []
    for sub in ("packaging", "patches"):
        s = pkg_read(f"{sub}/series")
        if s is not None:
            out += [f"{sub}/{ln.split()[0]}" for ln in s.decode().splitlines() if ln.strip() and not ln.lstrip().startswith("#")]
    return out


def mesa_base():
    conf = pkg_read("package.conf").decode()
    v = re.search(r"^VERSION=(\S+)", conf, re.M).group(1)
    return "base/" + v.replace(":", "%")


def ensure_clone(refresh):
    if refresh and CLONE.exists():
        prev = CLONE.with_name("mesa.prev")
        shutil.rmtree(prev, ignore_errors=True)
        CLONE.rename(prev)	# the old clone kept until the next refresh
        log(f"ladder: old clone kept at {prev}")
    if not (CLONE / ".git").is_dir():
        if not (SOURCE / ".git").is_dir():
            return False
        CLONE.parent.mkdir(parents=True, exist_ok=True)
        log(f"ladder: cloning {SOURCE} to {CLONE}")
        git("clone", "-q", str(SOURCE), str(CLONE), cwd=REPO)
    return True


def mesa_entries(reg):
    """The Mesa stability entries in series order: [(slug, [patch paths in series order])]."""
    series = mesa_series()
    pos = {p: i for i, p in enumerate(series)}
    out = []
    for slug, fm in stability(reg, "mesa").items():
        ps = sorted((p for p in fm.get("patches", []) if p in pos), key=pos.get)
        if ps:
            out.append((slug, ps))
    out.sort(key=lambda e: pos[e[1][0]])
    return out


def mesa_prepare(reg, refresh=False):
    """Build (or reuse) the rung branches. Returns the state: rungs [{label, slug, branch}], fused {slug: why}."""
    if not ensure_clone(refresh):
        return None
    base = mesa_base()
    if git("rev-parse", "-q", "--verify", base + "^{commit}", check=False).returncode:
        raise SystemExit(f"ladder: the clone has no {base} (the series moved to a new Debian version?): --refresh")
    series = mesa_series()
    entries = mesa_entries(reg)
    h = hashlib.sha256(git("rev-parse", base + "^{commit}").stdout.encode())
    for p in series:
        h.update(p.encode() + b"\0" + (pkg_read(p) or b""))
    h.update(json.dumps(entries).encode())
    key = h.hexdigest()[:16]
    statef = CLONE / ".git" / "ladder.json"
    if statef.is_file():
        st = json.loads(statef.read_text())
        if st.get("key") == key and all(git("rev-parse", "-q", "--verify", r["branch"], check=False).returncode == 0
                                        for r in st["rungs"]):
            return st
    log(f"ladder: building the rung branches in {CLONE} ({len(series)} patches, {len(entries)} stability fixes)")
    git("checkout", "-q", "-f", "--detach", base)
    git("reset", "-q", "--hard")	# the index refreshed: a root build in the tree changes its stat data
    git("clean", "-qfdx")
    commits = {}
    tmp = OUT / "patch.tmp"
    for p in series:
        data = pkg_read(p)
        if data is None:
            raise SystemExit(f"ladder: {p} is in the series but missing")
        f = OUT / Path(p).name
        tmp.write_bytes(data)
        r = git("apply", "--index", "-p1", str(tmp), check=False)
        if r.returncode:
            raise SystemExit(f"ladder: {p} does not apply on {base}: {r.stderr.strip()[:400]}")
        meta = subprocess.run([sys.executable, "-I", str(REPO / "tools" / "pkg-patchmsg.py"), str(tmp), "", f.name],
                              capture_output=True, text=True, check=True).stdout.split("\n", 2)
        msg = (meta[2] if len(meta) > 2 else f.name).strip() + f"\n\nLadder-Patch: {p}\n"
        subprocess.run(["git", "commit", "-q", "--allow-empty", "-F", "-"], cwd=CLONE, input=msg, text=True,
                       capture_output=True, check=True, env=dict(os.environ, **GIT_ENV))
        commits[p] = git("rev-parse", "HEAD").stdout.strip()
    git("branch", "-f", "ladder/all", "HEAD")
    # take the fixes out from the top down; one that doesn't revert is fused
    trees, fused = [], {}
    tip = git("rev-parse", "HEAD").stdout.strip()
    for slug, ps in reversed(entries):
        ok = True
        for p in reversed(ps):
            r = git("revert", "--no-edit", commits[p], check=False)
            if r.returncode:
                files = git("diff", "--name-only", "--diff-filter=U", check=False).stdout.split()
                git("revert", "--abort", check=False)
                git("reset", "-q", "--hard", tip)
                fused[slug] = "its revert conflicts with later patches in " + ", ".join(f.rsplit("/", 1)[-1] for f in files)
                ok = False
                break
        if ok:
            trees.append((slug, tip))	# the tree with this fix (and the ones below it)
            tip = git("rev-parse", "HEAD").stdout.strip()
    trees.reverse()
    rungs = [{"label": "base: the series without its stability fixes" + (f" (fused ones carried: {', '.join(fused)})" if fused else ""),
              "slug": None, "commit": tip}]
    for slug, c in trees:
        rungs.append({"label": f"{slug} ({', '.join(p.split('/')[-1][:4] for p in dict(entries)[slug])})", "slug": slug, "commit": c})
    for i, r in enumerate(rungs):
        r["branch"] = f"ladder/rung{i:02}"
        git("branch", "-f", r["branch"], r["commit"])
    st = {"key": key, "base": base, "rungs": rungs, "fused": fused,
          "entries": [s for s, _ in entries]}
    statef.write_text(json.dumps(st, indent=1))
    git("checkout", "-q", "-f", rungs[-1]["branch"])
    return st


def mesa_tests_dir():
    """Mesa's host tests: the working tree's, or with --rev that commit's, exported to build/tmp/ladder/tests-REV."""
    if not REV:
        return HOST_TESTS.parent
    rel = HOST_TESTS.parent.relative_to(REPO).as_posix()
    d = OUT / f"tests-{REV.replace('/', '_')}"
    shutil.rmtree(d, ignore_errors=True)	# an export of a commit: made again in a second
    for p, data in rev_files(rel).items():
        f = d / Path(p).relative_to(rel)
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_bytes(data)
    if not (d / "host-tests.sh").is_file():
        log(f"ladder: {REV} has no {rel}/host-tests.sh: the working tree's tests")
        return HOST_TESTS.parent
    return d


def mesa_host_tests():
    """[(suite, [parts])] of host-tests.sh; the ladder leaves out alloc-fail's fix-off (the per-file off check)."""
    out = []
    for ln in subprocess.run(["bash", str(TESTS_DIR / "host-tests.sh"), "--list"], capture_output=True, text=True, check=True).stdout.splitlines():
        s, *parts = ln.split()
        out.append((s, [p for p in parts if p != "fix-off"]))
    return out


def test_names(suites):
    return [f"mesa-{s}" + (f":{p}" if p else "") for s, parts in suites for p in (parts or [""])]


def guard_map(reg, component_slugs, names):
    """{test: [slugs]}: a register test "suite" guards every part of the suite, "suite:part" that part."""
    g = {}
    for slug in component_slugs:
        for t in reg[slug].get("tests", []):
            for n in names:
                if n == t or n.startswith(t + ":"):
                    g.setdefault(n, []).append(slug)
    return g


def mesa_ladder(reg, st):
    suites = mesa_host_tests()
    names = test_names(suites)
    rung_of = {r["slug"]: i for i, r in enumerate(st["rungs"]) if r["slug"]}
    for s in st["fused"]:
        rung_of[s] = 0
    g = guard_map(reg, st["entries"], names)
    tests = {n: max([rung_of[s] for s in g.get(n, [])] or [0]) for n in names}
    notes = [f"fused (carried in rung 0): {s}: {why}" for s, why in st["fused"].items()]
    notes += [f"guards: {n} -> {', '.join(g[n])}" for n in names if n in g]
    unguarded = [n for n in names if n not in g]
    if unguarded:
        notes.append("guard no stability fix (pass at every rung): " + ", ".join(unguarded))
    lad = Ladder("mesa (host-tests.sh against each rung's tree)", [r["label"] for r in st["rungs"]], tests, g, notes)
    lad.suites = suites
    return lad


def mesa_run(lad, st, rungs):
    for i in rungs:
        r = st["rungs"][i]
        git("checkout", "-q", "-f", r["branch"])
        git("clean", "-qfdx")
        d = OUT / "mesa" / f"rung{i:02}"
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)

        def one(suite_parts):
            s, parts = suite_parts
            o = d / s
            o.mkdir()
            env = dict(os.environ, MESA_WORK=str(CLONE), MESA_TEST_OUT=str(o), MESA_TESTS=str(TESTS_DIR), TEST_SUITE=f"mesa-{s}")
            p = subprocess.run(["nice", "-n", "10", "ionice", "-c3", "bash", str(TESTS_DIR / "host-tests.sh"), s, *parts],
                               cwd=REPO, capture_output=True, text=True, env=env)
            (d / f"{s}.log").write_text(p.stdout + p.stderr)
            res = dict(re.findall(r"^RESULT (\S+) (pass|fail|skip)$", p.stdout, re.M))
            out = {}
            for part in parts or [""]:
                key = s + (f":{part}" if part else "")
                out["mesa-" + key] = res.get(key, "skip" if p.returncode == 77 else "fail")
            return out
        with concurrent.futures.ThreadPoolExecutor(JOBS) as ex:
            for res in ex.map(one, lad.suites):
                for t, v in res.items():
                    lad.results[(t, i)] = v
        bad = [t for t in lad.tests if lad.results.get((t, i)) != "pass"]
        log(f"  rung {i:2} {r['label'][:60]}: {len(lad.tests) - len(bad)}/{len(lad.tests)} pass (logs {d.relative_to(REPO)})")
    git("checkout", "-q", "-f", st["rungs"][-1]["branch"])


def manifest(st, reg, path):
    """The PS3 ladder's manifest: its rungs, and the ps3: tests of Mesa's stability entries with their fix rungs."""
    rung_of = {r["slug"]: i for i, r in enumerate(st["rungs"]) if r["slug"]}
    lines = ["# fix ladder manifest (tools/ladder.py --manifest): rung N LABEL; fused SLUG; test NAME FIXRUNG SLUGS",
             f"key {st['key']}"]
    lines += [f"rung {i} {r['branch']} {r['label']}" for i, r in enumerate(st["rungs"])]
    lines += [f"fused {s}" for s in st["fused"]]
    tests = {}
    for slug in st["entries"]:
        for t in reg[slug].get("tests", []):
            if t.startswith("ps3:"):
                tests.setdefault(t[4:], []).append(slug)
    for t, slugs in tests.items():
        fr = max(rung_of.get(s, 0) for s in slugs)
        lines.append(f"test {t.replace(' ', '_')}\t{fr}\t{','.join(slugs)}")
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text("\n".join(lines) + "\n")
    log(f"ladder: manifest {path}")


# ---------------------------------------------------------------- ps3rsx, sdl2-compat, luajit
def ps3rsx_ladder(reg):
    T = REPO / "packages" / "walnut" / "drivers" / "ps3rsx" / "test"
    o = OUT / "ps3rsx"
    o.mkdir(parents=True, exist_ok=True)
    rules = re.findall(r"^off ([a-z0-9-]+) ", (T / "fix-off.sh").read_text(), re.M)
    stab = stability(reg, "ps3rsx")
    names, g = [], {}
    for rule in rules:
        slug = next((s for s in (f"ps3rsx-validator-{rule}", f"ps3rsx-{rule}") if s in reg), None)
        n = f"rule {rule}"
        names.append(n)
        if slug in stab:
            g[n] = [slug]
    lad = Ladder("ps3rsx (validator rules: fix-off.sh, then validate-test)", ["off: each rule off in a copy of validate.c", "on: src/validate.c"],
                 {n: 1 for n in names}, g,
                 ["rules with no stability entry (security/correctness): " + ", ".join(n[5:] for n in names if n not in g)])
    env = dict(os.environ, JOBS="4")
    b = subprocess.run(["nice", "-n", "10", "make", "-s", "-C", str(T), f"O={o}", f"{o}/validate-test"], capture_output=True, text=True, env=env)
    on = subprocess.run([str(o / "validate-test")], capture_output=True, text=True) if b.returncode == 0 else None
    off = subprocess.run(["nice", "-n", "10", "make", "-s", "-C", str(T), f"O={o}", "fix-off"], capture_output=True, text=True, env=env)
    (o / "ladder.log").write_text(b.stdout + b.stderr + (on.stdout + on.stderr if on else "") + off.stdout + off.stderr)
    for n in names:
        rule = n[5:]
        m = re.search(rf"^fix-off {re.escape(rule)}: (.*)$", off.stdout, re.M)
        lad.results[(n, 0)] = "fail" if m and m.group(1).startswith("fails as expected") else ("pass" if m and "passed" in m.group(1) else "fail")
        if m and not m.group(1).startswith(("fails as expected", "passed")):
            lad.notes.append(f"rule {rule} off: {m.group(1)}")
        lad.results[(n, 1)] = "pass" if on is not None and on.returncode == 0 else "fail"
    if on is None:
        lad.notes.append("validate-test did not build: " + b.stderr.strip()[-300:])
    return lad


def suite_ladder(name, script, parse, reg, slug):
    p = subprocess.run(["nice", "-n", "10", "bash", str(REPO / script)], cwd=REPO, capture_output=True, text=True,
                       env=dict(os.environ, TEST_SUITE=name))
    o = OUT / name
    o.mkdir(parents=True, exist_ok=True)
    (o / "ladder.log").write_text(p.stdout + p.stderr)
    tests = parse(p.stdout)
    lad = Ladder(f"{name} ({script}: stock, then ours)", ["stock", "our series"], {t: 1 for t in tests}, {t: [slug] for t in tests})
    if p.returncode == 77 or not tests:
        lad.notes.append("skipped: " + (p.stdout.strip().splitlines() or ["no output"])[-1])
        lad.tests = {}
    for t, (stock, ours) in tests.items():
        lad.results[(t, 0)], lad.results[(t, 1)] = stock, ours
    return lad


def parse_sdl(out):
    return {f"close-locked:{api}": ("pass" if s == "pass" else "fail", "pass" if p == "pass" else "fail")
            for api, s, p in re.findall(r"^sdl2-compat:(\S+) stock=(\S+) patched=(\S+)", out, re.M)}


def parse_luajit(out):
    p = re.search(r"^patched \(our deb\): (\d+) of", out, re.M)
    s = re.search(r"^stock \(orig tarball\): (\d+) of", out, re.M)
    if not p:
        return {}
    return {"low-2gb": ("-" if not s else ("pass" if s.group(1) == "0" else "fail"), "pass" if p.group(1) == "0" else "fail")}


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--top", action="store_true")
    ap.add_argument("--component", choices=("mesa", "ps3rsx", "sdl2-compat", "luajit", "all"), default="all")
    ap.add_argument("--refresh", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--prepare", action="store_true")
    ap.add_argument("--manifest")
    ap.add_argument("--rev", help="Mesa's series as this commit has it (default: the working tree)")
    a = ap.parse_args()
    global REV, TESTS_DIR
    REV = a.rev
    if "mesa" in (a.component, "all") and not a.prepare:
        TESTS_DIR = mesa_tests_dir()
    OUT.mkdir(parents=True, exist_ok=True)
    LADDER_DIR.mkdir(parents=True, exist_ok=True)
    lock = open(LADDER_DIR / ".lock", "w")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        log(f"ladder: waiting for another ladder run ({LADDER_DIR}/.lock)")
        fcntl.flock(lock, fcntl.LOCK_EX)
    reg = register()
    comps = ["mesa"] if a.top else (["mesa", "ps3rsx", "sdl2-compat", "luajit"] if a.component == "all" else [a.component])
    ladders, skipped = [], []
    t0 = datetime.datetime.now()
    if "mesa" in comps:
        st = mesa_prepare(reg, a.refresh)
        if st is None:
            skipped.append(f"mesa: no {SOURCE} to clone (make mesa-work)")
            if a.top:
                print(f"skip: no {CLONE} and no {SOURCE} to clone it from (make mesa-work)")
                return 77
        else:
            if a.manifest:
                manifest(st, reg, a.manifest)
            lad = mesa_ladder(reg, st)
            if not (a.list or a.prepare):
                mesa_run(lad, st, [len(st["rungs"]) - 1] if a.top else range(len(st["rungs"])))
            ladders.append(lad)
    if a.prepare:
        return 0
    if not a.list:
        if "ps3rsx" in comps:
            ladders.append(ps3rsx_ladder(reg))
        if "sdl2-compat" in comps:
            ladders.append(suite_ladder("sdl2-compat", "packages/patched/graphics/sdl2-compat/tests/close-locked-test.sh",
                                        parse_sdl, reg, "sdl2-compat-close-locked-deadlock"))
        if "luajit" in comps:
            ladders.append(suite_ladder("luajit", "packages/patched/libs/luajit/tests/low-2gb.sh", parse_luajit, reg, "luajit-low-2gb"))
    surprises = []
    for lad in ladders:
        lad.print()
        for t, i, kind, why in lad.surprises():
            surprises.append(f"{lad.name.split()[0]}: {t}: {kind}: {why}")
    if a.list:
        return 0
    log("\nnot laddered on the host: the kernel's stability patches (the PS3 kernel session), boost-context, "
        "plymouth, pnmixer, webkit2gtk (no host test of their fix in a tree)")
    for s in skipped:
        log("skipped: " + s)
    log(f"\nsurprises: {len(surprises)}" + "".join("\n  " + s for s in surprises))
    log(f"ladder: {'top rung' if a.top else 'full'} in {(datetime.datetime.now() - t0).seconds} s")
    rf = OUT / "result.json"
    try:
        res = json.loads(rf.read_text())
    except (OSError, ValueError):
        res = {}
    if not isinstance(res.get("components"), dict):
        res = {}
    comp = res.setdefault("components", {})
    for lad in ladders:
        top = len(lad.rungs) - 1
        name = lad.name.split()[0]
        comp[name] = {"date": t0.isoformat(timespec="seconds"), "mode": "top" if a.top else "full", "rev": REV,
                      "top_ok": all(lad.results.get((t, top)) in ("pass", "-") for t in lad.tests),
                      "top_failing": [t for t in lad.tests if lad.results.get((t, top)) not in ("pass", "-")],
                      "surprises": [s for s in surprises if s.startswith(name + ": ")]}
    rf.write_text(json.dumps(res, indent=1) + "\n")
    return 1 if surprises else 0


if __name__ == "__main__":
    sys.exit(main())
