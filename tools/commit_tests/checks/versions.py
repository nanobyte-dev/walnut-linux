"""versions: our version never changes the original's (docs/PACKAGING.md "Versions"; CLAUDE.md). The
versions themselves are made by tools/deb-prepare.sh (debsrc: <VERSION>+ps3.N, upstream:
<UPVER>-<DEBREV>+ps3.N, native: 0.N) with N from tools/deb-version.sh, so what a commit can get wrong is
in package.conf and debian/control:
  - debsrc: VERSION is Debian's version as it is (no +ps3 in it); an epoch only when Debian's own version
    has it: the base commit's VERSION had the same epoch, or the fetched .dsc (build/sources/<pkg>/) says so;
  - upstream: UPVER is upstream's version, no epoch, no +ps3; DEBREV is 1;
  - native (0.N): only under a name that is ours (ps3*, walnut*, *-walnut), and no VERSION, UPVER or
    DEBREV of its own; no committed debian/changelog (deb-prepare.sh writes it);
  - a relation in a debian/control of ours that names a ps3 build uses +ps3 or +ps3.N (with an optional ~),
    never another spelling;
  - upstream.mk: no epoch in a *_VERSION pin."""
import re
from pathlib import PurePath

NAME = "versions"
TARGETS = ("**/package.conf", "**/upstream.mk", "**/debian/control", "**/debian/changelog")
SHARED_INPUTS = ("tools/deb-version.sh", "tools/deb-prepare.sh")
DEBVER_RE = re.compile(r"(\d+:)?[0-9][A-Za-z0-9.+~]*(-[A-Za-z0-9.+~]+)?\Z")
UPVER_RE = re.compile(r"[0-9][A-Za-z0-9.+~]*\Z")
OURS_RE = re.compile(r"(ps3|walnut)[a-z0-9.+-]*\Z|[a-z0-9.+-]+-walnut\Z")
REL_RE = re.compile(r"\(\s*(<<|<=|=|>=|>>)\s*([^)\s]+)\s*\)")
PS3_REL_RE = re.compile(r"(\d+:)?[^:\s]+\+ps3(\.\d+)?(~[A-Za-z0-9.+~]*)?\Z")


def conf(text):
    out = {}
    for line in (text or "").splitlines():
        m = re.match(r"\s*([A-Z_][A-Z0-9_]*)=(.*)$", line)
        if m:
            v = m[2].strip()
            if len(v) >= 2 and v[0] == v[-1] and v[0] in "\"'":
                v = v[1:-1]
            out[m[1]] = v
    return out


def epoch(v):
    m = re.match(r"(\d+):", v or "")
    return m[1] if m else None


def dsc_epoch(ctx, pkg, source, version):
    """the epoch the fetched .dsc gives (None: no .dsc here, "" : none)"""
    text = ctx.read("build/sources/%s/%s_%s.dsc" % (pkg, source, version.split(":", 1)[-1]))
    if text is None:
        return None
    m = re.search(r"^Version:\s*(\S+)", text, re.M)
    return (epoch(m[1]) or "") if m else None


def package_conf(ctx, path):
    c = conf(ctx.read(path))
    t, pkg = c.get("TYPE"), PurePath(path).parent.name
    if t == "debsrc":
        v = c.get("VERSION", "")
        if not DEBVER_RE.match(v):
            ctx.error(path, "VERSION %r is not a Debian version" % v)
        if "ps3" in v:
            ctx.error(path, "VERSION %r: it is Debian's version as it is; +ps3.N is added by tools/deb-prepare.sh" % v)
        e = epoch(v)
        if e:
            before = epoch(conf(ctx.base_text(path)).get("VERSION")) if ctx.base else None
            from_dsc = dsc_epoch(ctx, pkg, c.get("SOURCE", pkg), v)
            if before == e or from_dsc == e:
                pass
            elif ctx.mode == "all" and from_dsc is None:
                ctx.warn(path, "VERSION %r has an epoch: Debian's own? (no fetched .dsc in build/sources/%s to tell)" % (v, pkg))
            else:
                ctx.error(path, "VERSION %r has an epoch %s: we never add one (only Debian's own version carries it: "
                          "fetch the source first, make %s-deb, so build/sources has its .dsc)" % (
                              v, "the .dsc doesn't have" if from_dsc is not None else "the base commit didn't have", pkg))
    elif t == "upstream":
        v, rev = c.get("UPVER", ""), c.get("DEBREV", "")
        if ":" in v:
            ctx.error(path, "UPVER %r has an epoch: upstream's version as it is, never an epoch" % v)
        elif not UPVER_RE.match(v):
            ctx.error(path, "UPVER %r is not an upstream version (digits first, then letters, digits, . + ~)" % v)
        if "ps3" in v:
            ctx.error(path, "UPVER %r: upstream's version as it is; -1+ps3.N is added by tools/deb-prepare.sh" % v)
        if rev != "1":
            ctx.error(path, "DEBREV %r: upstream's version takes -1 (never a higher revision of ours)" % rev)
    elif t == "native":
        src = c.get("SOURCE", pkg)
        if not OURS_RE.match(src):
            ctx.error(path, "a native package (version 0.N) named %r: 0.N is only for a name that is ours alone "
                      "(ps3*, walnut*, *-walnut); a Debian or upstream package is debsrc or upstream" % src)
        for k in ("VERSION", "UPVER", "DEBREV"):
            if k in c:
                ctx.error(path, "%s in a native package: its version is 0.N (tools/deb-prepare.sh)" % k)
    elif t is not None:
        ctx.error(path, "TYPE %r: one of debsrc, upstream, native" % t)


def run(ctx, files):
    for c in files:
        if c.status == "D":
            continue
        p = PurePath(c.path)
        if p.name == "package.conf":
            package_conf(ctx, c.path)
        elif p.name == "upstream.mk":
            for n, line in enumerate((ctx.read(c.path) or "").splitlines(), 1):
                m = re.match(r"\s*[A-Z0-9_]*VERSION\s*:?=\s*(\S+)", line)
                if m and re.match(r"\d+:", m[1]):
                    ctx.error(c.path, "line %d: an epoch in a version pin (%s)" % (n, m[1]))
        elif p.name == "changelog":
            d = PurePath(*p.parts[:-2])
            if conf(ctx.read(str(d / "package.conf"))).get("TYPE") == "native":
                ctx.error(c.path, "a native package's debian/changelog is written by tools/deb-prepare.sh (0.N): don't commit one")
        elif p.name == "control":
            for n, line in enumerate((ctx.read(c.path) or "").splitlines(), 1):
                for m in REL_RE.finditer(line):
                    v = m[2]
                    if "ps3" in v and not v.startswith("${") and not PS3_REL_RE.match(v):
                        ctx.error(c.path, "line %d: %r names a ps3 build: +ps3 or +ps3.N (an optional ~)" % (n, v))
