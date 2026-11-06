"""site_relative.py OUT BASE_URL APP(0|1): every link in the built pages relative to the page (Hugo's relativeURLs gets it
wrong under a base path like /gamedb/); with APP=1 a link to a folder also names its index.html. tools/gamedb/build.sh runs it."""
import pathlib, posixpath, re, sys, urllib.parse
out, base, app = pathlib.Path(sys.argv[1]), urllib.parse.urlparse(sys.argv[2]).path, sys.argv[3] == "1"
link = re.compile(r'((?:href|src)=")([^"#?]*)([#?][^"]*)?"')
def fix(m, page_dir):
    path, rest = m[2], m[3] or ""
    if path.startswith(base):
        target = path[len(base):]
        rel = posixpath.relpath(target or ".", page_dir or ".")
        path = rel + "/" if (target.endswith("/") or not target) and rel != "." else ("./" if rel == "." else rel)
    elif not path or re.match(r"[a-zA-Z][a-zA-Z0-9+.-]*:|/", path):
        return m[0]                           # an anchor, another site, walnut://
    if app and path.endswith("/"):
        path += "index.html"
    return '%s%s%s"' % (m[1], path, rest)
for f in out.rglob("*.html"):
    page_dir = posixpath.dirname(f.relative_to(out).as_posix())
    f.write_text(link.sub(lambda m: fix(m, page_dir), f.read_text()))
