#!/usr/bin/env python3
"""baseline-check.py save|check [options]: the GameDB migration's safety net (the plan "GameDB holds
everything about a game", Phase 0). `save DIR` runs every probe below on this checkout and writes
its output to DIR (default build/tmp/gamedb-baseline), with a README that names each file and the
command that made it. `check DIR` runs the same probes again into DIR/check-<stamp>/ and diffs each
against the saved file: "same" or "DIFFERENT" per probe, the first lines of each diff, exit 1 when
any probe differs (a phase that changes something on purpose names the probes it expects to change
with --expect, and gets exit 0 only when exactly those differ).

Probes (each one file, made by the command its README line shows):
  make-vars       every makefile variable, expanded (an extra makefile's $(info)), config.local.mk's
                  values hashed; make-key-vars the ones the build hangs on (PKG_DIRS, SRC_PACKAGES,
                  DEB_ENV, *_ORIG); make-pn the variable part of `make -pn` (not compared: line numbers)
  pkg-names       every package name and its directory (tools/pkg-dir.sh --all, else today's globs)
  versions        every package's tools/deb-version.sh (N, date, commit)
  workflows       sha256 of every .github/workflows file, and whether gen-workflows.py writes them unchanged
  licensed        the licensed package names and leak globs (tools/licensed.py, else today's lists)
  debs            every .deb in build/debs: its control fields and its file list (names, modes, sizes)
  catalog         catalog.py's catalog.json (SOURCE_DATE_EPOCH=0); pack-catalog its tarball's members
  site            the Hugo site's files with their sha256 (tools/gamedb/build.sh; skipped without Hugo)
  report, logscan, logscan-json, interference, matrix
                  the analysis tools over every run in build/bench (matrix: one row per run into a
                  scratch BENCH_RESULTS seeded with results/interfered.txt)
  export          the public copy's commits (tools/public-export.sh into a scratch directory): every
                  baseline commit must come out with the same id, in the same place (newer ones may follow)
  image-packages  the image's package list per profile (tools/build-image.sh's selection)
  doclinks        tools/doclinks.py's broken links (report mode), when it exists; doclinks-paths the
                  same with --paths
Slow ones: report (~1 min), export (~1 min). --only NAME[,NAME] runs a few, --skip NAME[,NAME] leaves some out.
"""
import argparse
import datetime
import difflib
import hashlib
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
DEFAULT = REPO / "build" / "tmp" / "gamedb-baseline"
EXPORT_SCRATCH = Path(os.environ.get("GAMEDB_BASELINE_EXPORT", "/mnt/data_linux/Temp/ps3/gamedb-baseline-export"))
KEY_VARS = re.compile(r"(PKG_DIRS|SRC_PACKAGES|DEB_ENV|DEPLOY_TOP|.*_ORIG|.*_UPVER)\Z")


def sh(cmd, env=None, check=True, cwd=REPO):
    e = dict(os.environ)
    e.update(env or {})
    p = subprocess.run(cmd, shell=isinstance(cmd, str), cwd=cwd, env=e, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, errors="replace")
    if check and p.returncode:
        raise RuntimeError("%s: exit %d\n%s" % (cmd, p.returncode, p.stdout[-3000:]))
    return p.stdout


def local_names():
    f = REPO / "config.local.mk"
    if not f.exists():
        return set()
    return {m[1] for m in re.finditer(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*[:?+]?=", f.read_text(), re.M)}


def redact(name, value, secret):
    if name in secret or re.search(r"AUTH|PASS|SECRET|TOKEN", name):
        return "<sha256:%s>" % hashlib.sha256(value.encode()).hexdigest()[:12]
    return value


# --- probes: each returns (command for the README, text) ------------------------------------------
def p_make_vars(tmp):
    dump = tmp / "dump.mk"
    dump.write_text("__baseline_dump:\n\t@:$(foreach v,$(sort $(.VARIABLES)),$(if $(filter file override,"
                    "$(origin $v)),$(info @@VAR $v=$(subst $(newline__), ,$($v)))))\n"
                    "define newline__\n\n\nendef\n")
    out = sh(["make", "-s", "--no-print-directory", "-f", "Makefile", "-f", str(dump), "__baseline_dump"])
    secret = local_names()
    lines = []
    for ln in out.splitlines():
        if not ln.startswith("@@VAR "):
            continue
        name, _, val = ln[6:].partition("=")
        if name in ("MAKEFILE_LIST", "newline__") or name.startswith("."):
            continue
        lines.append("%s=%s" % (name, redact(name, val, secret)))
    return "make -s -f Makefile -f dump.mk __baseline_dump (every file/override variable, expanded)", "\n".join(lines) + "\n"


def p_make_key_vars(tmp):
    cmd, text = p_make_vars(tmp)
    keep = [ln for ln in text.splitlines() if KEY_VARS.match(ln.partition("=")[0])]
    out = []
    for ln in keep:		# one word per line: a diff names the word
        n, _, v = ln.partition("=")
        words = v.split() if n in ("PKG_DIRS",) else [v]
        out += ["%s: %s" % (n, w) for w in (sorted(words) if n == "PKG_DIRS" else words)]
    return "the make-vars dump filtered to %s" % KEY_VARS.pattern, "\n".join(out) + "\n"


def p_make_pn(tmp):
    out = sh(["make", "-pn", "--no-print-directory", "help"])
    secret, lines, origin = local_names(), [], None
    for ln in out.splitlines():
        if ln.startswith("# makefile") or ln.startswith("# environment") or ln.startswith("# default") \
                or ln.startswith("# automatic") or ln.startswith("# override"):
            origin = ln
            continue
        m = re.match(r"([A-Za-z_][A-Za-z0-9_.-]*) (:=|=|\?=|\+=|::=) (.*)$", ln)
        if m and origin and origin.startswith("# makefile"):
            lines.append("%s %s %s" % (m[1], m[2], redact(m[1], m[3], secret)))
        origin = None
    return "make -pn help: the variables whose origin is a makefile (not compared: unexpanded, line-numbered)", \
        "\n".join(sorted(lines)) + "\n"


def package_dirs():
    """name -> dir: tools/pkg-dir.sh --all when it has it, else the globs of 2026-10-26"""
    p = subprocess.run(["tools/pkg-dir.sh", "--all"], cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if p.returncode == 0 and p.stdout.strip():
        dirs = p.stdout.split()
        how = "tools/pkg-dir.sh --all"
    else:
        dirs = sorted({str(c.parent.relative_to(REPO)) for pat in ("packages/*/*/package.conf", "packages/*/*/*/package.conf")
                       for c in REPO.glob(pat)})
        how = "glob packages/*/*/package.conf packages/*/*/*/package.conf"
    return {os.path.basename(d): d for d in dirs}, how


def p_pkg_names(tmp):
    dirs, how = package_dirs()
    return how, "".join("%s %s\n" % (n, dirs[n]) for n in sorted(dirs))


def p_versions(tmp):
    dirs, how = package_dirs()
    out = []
    for n in sorted(dirs):
        v = sh(["tools/deb-version.sh", n]).splitlines()
        out.append("%s %s" % (n, " | ".join(v)))
    return "tools/deb-version.sh NAME for every package (%s)" % how, "\n".join(out) + "\n"


def p_workflows(tmp):
    wf = REPO / ".github" / "workflows"
    out = ["%s %s" % (hashlib.sha256(f.read_bytes()).hexdigest(), f.name) for f in sorted(wf.iterdir()) if f.is_file()]
    # regenerated in a scratch copy of the tree's workflow directory: the generator writes in place
    gen = REPO / "github-ci" / "gen-workflows.py"
    src = gen.read_text()
    if "--check" in src:
        p = subprocess.run([sys.executable, str(gen), "--check"], cwd=REPO, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
        out.append("gen-workflows: %s" % ("current" if p.returncode == 0 else "STALE\n" + p.stdout))
    else:
        before = {f.name: f.read_bytes() for f in wf.iterdir()}
        sh([sys.executable, str(gen)])
        after = {f.name: f.read_bytes() for f in wf.iterdir()}
        out.append("gen-workflows: %s" % ("current" if before == after else "STALE"))
    return "sha256 of .github/workflows/*; gen-workflows.py (--check when it has it)", "\n".join(out) + "\n"


def read_list(path):
    out = []
    for ln in Path(path).read_text().splitlines():
        ln = ln.split("#", 1)[0].strip()
        if ln:
            out.append(ln)
    return out


def p_licensed(tmp):
    tool = REPO / "tools" / "licensed.py"
    if tool.exists():
        names = sh([sys.executable, "-I", str(tool), "names"]).split()
        globs = sh([sys.executable, "-I", str(tool), "globs"]).splitlines()
        how = "tools/licensed.py names; tools/licensed.py globs"
    else:
        names = read_list(REPO / "tools" / "licensed-packages.txt")
        globs = read_list(REPO / "tools" / "licensed-files.txt")
        how = "tools/licensed-packages.txt and tools/licensed-files.txt, comments left out"
    return how, "".join("name %s\n" % n for n in sorted(set(names))) + "".join("glob %s\n" % g for g in sorted(set(globs)))


def leakcheck():
    spec = importlib.util.spec_from_file_location("leakcheck", REPO / "tools" / "leak-check.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


FIELDS = ("Package", "Source", "Version", "Architecture", "Section", "Priority", "Depends", "Pre-Depends",
          "Recommends", "Suggests", "Provides", "Conflicts", "Breaks", "Replaces", "Enhances")


def p_debs(tmp):
    lc = leakcheck()
    debs = sorted((REPO / "build" / "debs").glob("*/*.deb"))
    out = []
    for d in debs:
        rel = d.relative_to(REPO / "build" / "debs")
        if d.name.split("_")[0].endswith("-dbgsym"):
            out.append("== %s (debug symbols: control only)" % rel)
        else:
            out.append("== %s" % rel)
        c = lc.deb_control(str(d))
        for k in FIELDS:
            if k in c:
                out.append("  %s: %s" % (k, " ".join(c[k].split())))
        if d.name.split("_")[0].endswith("-dbgsym"):
            continue
        _, data = lc.deb_parts(str(d))
        import io
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:*") as t:
            for m in sorted(t.getmembers(), key=lambda m: m.name):
                kind = "d" if m.isdir() else "l" if m.issym() else "h" if m.islnk() else "f"
                extra = (" -> " + m.linkname) if m.issym() or m.islnk() else (" %d" % m.size if m.isfile() else "")
                out.append("  %s %o %s%s" % (kind, m.mode, m.name, extra))
    if not debs:
        out.append("(no .deb in build/debs)")
    return "every build/debs/*/*.deb: control fields %s and data.tar members (tools/leak-check.py's reader)" % ",".join(FIELDS), \
        "\n".join(out) + "\n"


def p_catalog(tmp):
    out = tmp / "catalog.json"
    sh([sys.executable, "-I", "tools/gamedb/catalog.py", "--out", str(out)], env={"SOURCE_DATE_EPOCH": "0"})
    return "SOURCE_DATE_EPOCH=0 tools/gamedb/catalog.py --out FILE", out.read_text()


def p_pack_catalog(tmp):
    out = tmp / "catalog.tar"
    sh([sys.executable, "-I", "tools/gamedb/pack-catalog.py", "--out", str(out)], env={"SOURCE_DATE_EPOCH": "0"})
    lines = []
    with tarfile.open(out) as t:
        for m in t:
            data = t.extractfile(m).read() if m.isfile() else b""
            lines.append("%s %o %d %s" % (m.name, m.mode, m.size, hashlib.sha256(data).hexdigest() if m.isfile() else "-"))
    return "SOURCE_DATE_EPOCH=0 tools/gamedb/pack-catalog.py --out FILE; its members with sha256", "\n".join(lines) + "\n"


def p_site(tmp):
    hv = (REPO / "tools" / "gamedb" / "hugo.version").read_text()
    ver = re.search(r"HUGO_VERSION=(\S+)", hv)[1]
    if not (REPO / "build" / "tools" / ("hugo-" + ver) / "hugo").exists():
        return "tools/gamedb/build.sh (skipped: no build/tools/hugo-%s)" % ver, "skipped\n"
    out = tmp / "site"
    # build.sh rewrites website/gamedb/data/catalog.json (ignored): keep the developer's copy
    cj = REPO / "website" / "gamedb" / "data" / "catalog.json"
    keep = cj.read_bytes() if cj.exists() else None
    try:
        sh(["tools/gamedb/build.sh", str(out)], env={"SOURCE_DATE_EPOCH": "0"})
    finally:
        if keep is not None:
            cj.write_bytes(keep)
    lines = []
    for f in sorted(out.rglob("*")):
        if f.is_file():
            lines.append("%s %s" % (hashlib.sha256(f.read_bytes()).hexdigest(), f.relative_to(out)))
    return "SOURCE_DATE_EPOCH=0 tools/gamedb/build.sh OUT; every file's sha256", "\n".join(lines) + "\n"


def run_dirs():
    return sorted(str(d.relative_to(REPO)) for d in (REPO / "build" / "bench").glob("ps3-bench-*") if d.is_dir())


def p_report(tmp):
    return "bench/report.py --stalls --perf --mem --load build/bench/ps3-bench-* (sorted)", \
        sh([sys.executable, "bench/report.py", "--stalls", "--perf", "--mem", "--load"] + run_dirs(), check=False)


def p_logscan(tmp):
    return "bench/logscan.py -v --show-ignored", sh([sys.executable, "bench/logscan.py", "-v", "--show-ignored"], check=False)


def p_logscan_json(tmp):
    return "bench/logscan.py --json --by-message", sh([sys.executable, "bench/logscan.py", "--json", "--by-message"], check=False)


def p_interference(tmp):
    return "bench/interference-scan.py -v build/bench/ps3-bench-* (no --write)", \
        sh([sys.executable, "bench/interference-scan.py", "-v"] + run_dirs(), check=False)


def p_matrix(tmp):
    res = tmp / "matrix"
    res.mkdir()
    intf = REPO / "bench" / "results" / "interfered.txt"
    if intf.exists():
        shutil.copy(intf, res / "interfered.txt")
    log = []
    for d in run_dirs():
        log.append(sh([sys.executable, "bench/matrix.py", Path(d).name, d], env={"BENCH_RESULTS": str(res)}, check=False)
                   .replace(str(res), "$BENCH_RESULTS"))
    text = "".join(log)
    for f in sorted(res.glob("*.csv")):
        text += "== %s\n%s" % (f.name, f.read_text())
    return "BENCH_RESULTS=SCRATCH bench/matrix.py <run name> <run dir>, every run in turn; the CSVs", text


def p_export(tmp):
    d = EXPORT_SCRATCH
    if d.exists():
        shutil.rmtree(d)
    d.parent.mkdir(parents=True, exist_ok=True)
    log = sh(["tools/public-export.sh", str(d)], check=False)
    ok = "history scan clean" in log
    commits = sh(["git", "-C", str(d), "log", "--reverse", "--format=%H %s", "main"]) if (d / ".git").exists() else ""
    files = sh(["git", "-C", str(d), "ls-tree", "-r", "--name-only", "main"]) if (d / ".git").exists() else ""
    lic = [f for f in files.splitlines() if re.match(r"(packages/licensed/|gamedb/[^/]+/[^/]+/licensed/)", f)]
    shutil.rmtree(d, ignore_errors=True)
    head = "scan: %s\nlicensed files in main: %d\n" % ("clean" if ok else "NOT CLEAN\n" + log[-2000:], len(lic))
    return "tools/public-export.sh %s; git log --reverse --format='%%H %%s' main (compared as a prefix)" % d, head + commits


def p_image_packages(tmp):
    tool = REPO / "tools" / "image-packages.sh"
    out = []
    for prof in ("dev", "public"):
        if tool.exists():
            pk = sh([str(tool), prof]).split()
        else:   # tools/build-image.sh of 2026-10-26
            pk = read_list(REPO / "system" / "image" / "packages-ours.txt")
            if prof == "public":
                pk = ["ps3-games-free" if p == "ps3-games" else p for p in pk if p not in
                      ("linux-image-ps3-debug", "ps3rsx-modules-ps3-debug", "ps3-games-licensed", "classicube")]
        out += ["%s %s" % (prof, p) for p in pk]
    img = REPO / "build" / "rootfs" / "debian-ppc64.tar"
    if img.exists():
        st = sh(["tar", "-xOf", str(img), "./var/lib/dpkg/status"], check=False)
        for stanza in st.split("\n\n"):
            f = dict(re.findall(r"^(Package|Version|Status): (.*)$", stanza, re.M))
            if "installed" in f.get("Status", "") and "Package" in f:
                out.append("image %s %s" % (f["Package"], f.get("Version")))
    else:
        out.append("image: none (no build/rootfs/debian-ppc64.tar)")
    return "%s dev|public, else build-image.sh's selection; the image's dpkg status when built" % (
        "tools/image-packages.sh" if tool.exists() else "packages-ours.txt"), "\n".join(out) + "\n"


def p_doclinks(tmp):
    tool = REPO / "tools" / "doclinks.py"
    if not tool.exists():
        return "tools/doclinks.py (not there yet)", "none\n"
    return "tools/doclinks.py --report (the links that don't resolve)", sh([sys.executable, "-I", str(tool), "--report"], check=False)


def p_doclinks_paths(tmp):
    tool = REPO / "tools" / "doclinks.py"
    if not tool.exists():
        return "tools/doclinks.py --paths (not there yet)", "none\n"
    return "tools/doclinks.py --report --paths (also repository paths written out)", \
        sh([sys.executable, "-I", str(tool), "--report", "--paths"], check=False)


PROBES = {
    "make-vars": p_make_vars, "make-key-vars": p_make_key_vars, "make-pn": p_make_pn,
    "pkg-names": p_pkg_names, "versions": p_versions, "workflows": p_workflows, "licensed": p_licensed,
    "debs": p_debs, "catalog": p_catalog, "pack-catalog": p_pack_catalog, "site": p_site,
    "report": p_report, "logscan": p_logscan, "logscan-json": p_logscan_json, "interference": p_interference,
    "matrix": p_matrix, "export": p_export, "image-packages": p_image_packages, "doclinks": p_doclinks,
    "doclinks-paths": p_doclinks_paths,
}
NOT_COMPARED = {"make-pn"}


def run_probes(names, out):
    out.mkdir(parents=True, exist_ok=True)
    cmds = {}
    for n in names:
        with tempfile.TemporaryDirectory(dir=REPO / "build" / "tmp") as t:
            print("-- %s" % n, flush=True)
            try:
                cmd, text = PROBES[n](Path(t))
            except Exception as e:  # a probe that fails is a difference, not a crash of the whole check
                cmd, text = "FAILED", "probe failed: %s\n" % e
        (out / (n + ".txt")).write_text(text)
        cmds[n] = cmd
    return cmds


def compare_export(old, new):
    o, n = old.splitlines(), new.splitlines()
    oh = [l for l in o if re.match(r"[0-9a-f]{40} ", l)]
    nh = [l for l in n if re.match(r"[0-9a-f]{40} ", l)]
    problems = [l for l in n if not re.match(r"[0-9a-f]{40} ", l) and l not in o]
    if nh[:len(oh)] != oh:
        bad = next(i for i, (a, b) in enumerate(zip(oh, nh + [""] * len(oh))) if a != b)
        problems.append("commit %d differs: baseline %s, now %s" % (bad + 1, oh[bad], (nh + [""] * len(oh))[bad]))
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=("save", "check"))
    ap.add_argument("dir", nargs="?", default=str(DEFAULT))
    ap.add_argument("--only", default="")
    ap.add_argument("--skip", default="")
    ap.add_argument("--expect", default="", help="probes this phase changes on purpose (comma-separated)")
    a = ap.parse_args()
    names = [n for n in PROBES if (not a.only or n in a.only.split(",")) and n not in a.skip.split(",")]
    base = Path(a.dir)
    if a.mode == "save":
        cmds = run_probes(names, base)
        rev = sh(["git", "rev-parse", "HEAD"]).strip()
        dirty = sh(["git", "status", "--porcelain", "--untracked-files=no"]).strip()
        old = (base / "README").read_text() if (base / "README").exists() else ""
        keep = {m[2]: m[1] for m in re.finditer(r"^  ((\S+)\.txt .*)$", old, re.M)}
        lines = [keep[n] for n in keep if n not in names]
        lines += ["%s.txt  %s" % (n, cmds[n]) for n in names]
        now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        made = "Made %s at %s%s." % (now, rev, " (dirty tree)" if dirty else "")
        m = re.search(r"(Made \S+ at [0-9a-f]+(?: \(dirty tree\))?\.)", old)
        if m and a.only:	# a partial save keeps the baseline's date and says what was saved later
            made = m[1] + "\n" + "".join(l for l in re.findall(r"^Re-saved .*\n", old, re.M)) + \
                "Re-saved %s at %s%s: %s." % (now, rev, " (dirty tree)" if dirty else "", ", ".join(names))
        (base / "README").write_text(
            "GameDB migration baseline (tools/gamedb/baseline-check.py; the plan's Phase 0). Keep it until\n"
            "the whole plan is done. %s\n"
            "Re-check: python3 tools/gamedb/baseline-check.py check %s [--only ...] [--expect ...]\n\n"
            "Files (probe, then the command that made it):\n%s\n" % (
                made, base, "".join("  %s\n" % l for l in sorted(lines))))
        print(">> baseline in %s" % base)
        return 0
    stamp = datetime.datetime.now().strftime("%Y%m%dT%H%M%S")
    out = base / ("check-" + stamp)
    run_probes(names, out)
    expect = set(filter(None, a.expect.split(",")))
    differ = []
    for n in names:
        if n in NOT_COMPARED:
            continue
        old_f = base / (n + ".txt")
        if not old_f.exists():
            print("%-15s no baseline" % n)
            continue
        old, new = old_f.read_text(), (out / (n + ".txt")).read_text()
        if n == "export":
            probs = compare_export(old, new)
            same = not probs
            diff = probs
        else:
            same = old == new
            diff = list(difflib.unified_diff(old.splitlines(), new.splitlines(), "baseline", "now", n=0, lineterm=""))
        print("%-15s %s%s" % (n, "same" if same else "DIFFERENT", " (expected)" if not same and n in expect else ""))
        if not same:
            differ.append(n)
            for l in diff[:40]:
                print("    " + l)
            if len(diff) > 40:
                print("    ... %d more lines (diff %s %s)" % (len(diff) - 40, old_f, out / (n + ".txt")))
    print(">> outputs in %s" % out)
    unexpected = [n for n in differ if n not in expect]
    missing = [n for n in expect if n not in differ]
    if missing:
        print("!! expected to differ but the same: %s" % ", ".join(missing))
    return 1 if unexpected else 0


if __name__ == "__main__":
    sys.exit(main())
