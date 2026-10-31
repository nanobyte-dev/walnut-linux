"""shell: every shell script parses (bash -n; sh -n for #!/bin/sh), shellcheck finds no error, and none of
the patterns that hurt us before:
  - `|| true` after apt-get, docker run, or a cp of build output (build/, $BUILD, debs/): a failure that
    must stop the script is hidden (error)
  - pkill -f / pgrep -f: the pattern matches the caller's own shell too (error; pgrep -x, pid files)
  - find / or bfs / (also /mnt): a disk-wide search saturates the disks (error; locate)
  - `| grep -q` under pipefail: grep exits at the first match, the writer gets SIGPIPE and the pipeline
    fails (warning)
Shell scripts: *.sh, *.bash, and files whose first line names sh, bash or dash."""
import re
import shutil
from pathlib import Path

NAME = "shell"
TARGETS = ("**",)
SHARED_INPUTS = ()
SHEBANG_RE = re.compile(rb"#!\s*(/usr)?/bin/(env\s+)?(ba|da)?sh\b")
HINT = "install ShellCheck: sudo dnf install ShellCheck"
# -f without -x (and without a ^...$ pattern): the pattern matches anywhere in any command line, the caller's too
PKILL_RE = re.compile(r"(^|[\s;&|(`])p(kill|grep)\s+(?![^#\n;|&]*(\s-[A-Za-z]*x|\s--exact\b|\s['\"]\^[^'\"]*\$['\"]))"
                      r"([^#\n;|&]*\s)?(-[A-Za-z]*f[A-Za-z]*|--full)(\s|$)")
# `... | grep -c PATTERN || true`: grep -c exits 1 on a count of 0, so the || true belongs to it
COUNT_RE = re.compile(r"\|\s*grep\s+(-[A-Za-z]*\s+)*-[A-Za-z]*c[A-Za-z]*\s[^|]*\|\|\s*true\b")
PATTERNS = (
    ("error", re.compile(r"(^|[\s;&|(`])(apt-get|apt)\s[^#\n]*\|\|\s*true\b"),
     "`|| true` after apt-get hides a failed install or update"),
    ("error", re.compile(r"\bdocker\s+run\b[^#\n]*\|\|\s*true\b"), "`|| true` after docker run hides a failed container"),
    ("error", re.compile(r"(^|[\s;&|(`])cp\s[^#\n;|&]*(\bbuild/|\$\{?BUILD\b|\$\(BUILD\)|\bdebs/)[^#\n]*\|\|\s*true\b"),
     "`|| true` after a cp of build output hides a missing artefact"),
    ("error", PKILL_RE, "pkill -f / pgrep -f match the caller's own command line (use pgrep -x NAME, an anchored "
     "-x -f pattern, or a pid file)"),
    ("error", re.compile(r"(^|[\s;&|(`])(find|bfs)\s+(-[HLPO]\S*\s+)*[\"']?/(mnt/?)?[\"']?(?=[\s;|&)]|$)"),
     "find / or bfs / (or /mnt) searches whole disks (use locate, or a path inside the project)"),
)
PIPEFAIL_RE = re.compile(r"^\s*set\s+(-[a-zA-Z]*o\s+pipefail|-o\s+pipefail|.*\bpipefail\b)", re.M)
GREPQ_RE = re.compile(r"\|\s*grep\s+(-[A-Za-z]*q|--quiet|--silent)")


def accept(ctx, path):
    name = Path(path).name
    if name.endswith((".sh", ".bash")):
        return True
    if "." in name and not name.endswith((".postinst", ".preinst", ".postrm", ".prerm", ".config")):
        return False
    b = ctx.read_bytes(path)
    return bool(b) and bool(SHEBANG_RE.match(b.split(b"\n", 1)[0]))


def logical_lines(text):
    """[(first line number, the command with its continuation lines joined, comments cut)]"""
    out, buf, start = [], "", None
    for i, line in enumerate(text.split("\n"), 1):
        s = line.lstrip()
        if not buf and s.startswith("#"):
            continue
        code = re.sub(r"(^|\s)#.*$", "", line)
        if start is None:
            start = i
        if code.endswith("\\"):
            buf += code[:-1] + " "
            continue
        out.append((start, buf + code))
        buf, start = "", None
    if buf:
        out.append((start, buf))
    return out


def pattern_findings(text):
    """[(kind, line, text)] of PATTERNS and the pipefail grep -q warning in a script's text"""
    out = []
    pipefail = bool(PIPEFAIL_RE.search(text))
    for n, line in logical_lines(text):
        for kind, rx, why in PATTERNS:
            if rx.search(line) and not (rx is not PKILL_RE and "|| true" in why and COUNT_RE.search(line)):
                out.append((kind, n, why))
        if pipefail and GREPQ_RE.search(line):
            out.append(("warning", n, "`| grep -q` under pipefail: grep stops reading at the first match, the writer "
                        "gets SIGPIPE and the pipeline fails (grep -c, or a here-string)"))
    return out


def run(ctx, files):
    scripts = []
    for c in files:
        if c.status == "D":
            continue
        b = ctx.read_bytes(c.path)
        if b is None:
            continue
        text = b.decode("utf-8", errors="replace")
        first = b.split(b"\n", 1)[0]
        posix = bool(re.match(rb"#!\s*/bin/(da)?sh\b|#!\s*/usr/bin/env\s+(da)?sh\b", first))
        f = str(ctx.path(c.path))
        args = ["bash", "-n", f] if not posix else [shutil.which("dash"), "-n", f] if shutil.which("dash") else ["bash", "--posix", "-n", f]
        r = ctx.run_tool(args, timeout=60)
        if r.returncode != 0:
            ctx.error(c.path, "doesn't parse (%s -n): %s" % (args[0], r.stderr.strip().replace(f, c.path)[:400]))
        for kind, n, why in pattern_findings(text):
            (ctx.error if kind == "error" else ctx.warn)(c.path, "line %d: %s" % (n, why))
        scripts.append((c.path, first.startswith(b"#!")))
    if not scripts:
        return
    sc = ctx.tool("shellcheck", ["shellcheck"], HINT)
    if not sc:
        return
    for path, has_shebang in scripts:
        r = ctx.run_tool(sc + ["-S", "error", "-f", "gcc"] + ([] if has_shebang else ["-s", "bash"]) + ["--", path], timeout=120)
        if r.returncode not in (0, 1):
            ctx.error(path, "shellcheck failed (exit %d): %s" % (r.returncode, r.stderr.strip()[:300]))
        for line in r.stdout.splitlines():
            m = re.match(r"(.+?):(\d+):\d+: (\w+): (.*)", line)
            if m:
                ctx.error(path, "line %s: shellcheck: %s" % (m[2], m[4]))
