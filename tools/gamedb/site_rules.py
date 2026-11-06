"""site_rules.py WALNUT_WATCH OUT: OUT against the server's own rules, read from walnut-watch (SITE_EXT, NAME_RE) so the two
cannot drift apart, and the site's CSP (no inline script or handler). Exit 1 and the reasons on stderr. build.sh runs it."""
import os, re, sys
src = open(sys.argv[1]).read()
ext = set(eval(re.search(r"^SITE_EXT = (\{.*?\})", src, re.S | re.M)[1]))
name_re = re.compile(eval(re.search(r"^NAME_RE = re\.compile\((r?\".*?\")\)", src, re.M)[1]))
bad = []
for root, dirs, files in os.walk(sys.argv[2]):
    for n in dirs + files:
        p = os.path.join(root, n)
        rel = os.path.relpath(p, sys.argv[2])
        if os.path.islink(p):
            bad.append(rel + ": a symlink")
        elif not name_re.match(n):
            bad.append(rel + ": a name the server refuses (hidden or odd characters)")
        elif n in files and n.rsplit(".", 1)[-1].lower() not in ext:
            bad.append(rel + ": not a static site file type")
if not os.path.isfile(os.path.join(sys.argv[2], "index.html")):
    bad.append("no index.html")
# the site's CSP is script-src 'self': an inline script or handler would be dropped silently by the browser
import re
for root, _, fs in os.walk(sys.argv[2]):
    for n in fs:
        if n.endswith(".html"):
            t = open(os.path.join(root, n), encoding="utf-8", errors="replace").read()
            if any("src=" not in m.group(1) for m in re.finditer(r"<script\b([^>]*)>", t)) or \
               re.search(r"(?i)\son[a-z]+\s*=\s*[\"']", t) or "javascript:" in t.lower():
                bad.append(os.path.relpath(os.path.join(root, n), sys.argv[2]) + ": an inline script or handler (the site's CSP drops it)")
if bad:
    print("\n".join("site rules: " + b for b in bad), file=sys.stderr)
    sys.exit(1)
print("site rules: ok (%d files)" % sum(len(f) for _, _, f in os.walk(sys.argv[2])))
