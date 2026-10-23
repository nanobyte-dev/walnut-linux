#!/bin/bash
# build.sh [--preview] OUT: build the Walnut Games site into the directory OUT (docs/GAMEDB.md).
#
# Steps, each of which stops the build on failure:
#   1. check.py: the catalog (gamedb/content/games) is valid, no licensed package in it
#   2. catalog.py: gamedb/data/catalog.json (the ratings, from the tests' result.toml); with
#      GAMEDB_DEB_INDEX=https://walnut.nanobyte.dev/apt the pages also link the .deb files of the patched
#      games (without it, or offline, no links)
#   3. Hugo: the pinned release of tools/gamedb/hugo.version, downloaded to build/tools/hugo-<version>/
#      on first use and verified with its sha256 (the pinned one and the release's checksums file)
#   4. the output against walnut-watch's site rules (tools/walnut-server/walnut-watch: SITE_EXT,
#      NAME_RE, no symlinks, an index.html): anything the server would refuse fails here, before an upload
# --preview builds for http://localhost:8000/ (serve it with: python3 -m http.server -d OUT 8000);
# without it the links are for https://walnut.nanobyte.dev/gamedb/.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
base=""
if [ "${1:-}" = "--preview" ]; then base="http://localhost:8000/"; shift; fi
[ $# -eq 1 ] || { echo "usage: $0 [--preview] OUT" >&2; exit 2; }
out=$(realpath -m "$1")

# shellcheck source=hugo.version
. "$here/hugo.version"
tools="$repo/build/tools/hugo-$HUGO_VERSION"
hugo="$tools/hugo"
if [ ! -x "$hugo" ]; then
	tar_name="hugo_extended_${HUGO_VERSION}_linux-amd64.tar.gz"
	url="https://github.com/gohugoio/hugo/releases/download/v$HUGO_VERSION"
	mkdir -p "$tools"
	curl -fsSL -o "$tools/$tar_name" "$url/$tar_name"
	curl -fsSL -o "$tools/checksums.txt" "$url/hugo_${HUGO_VERSION}_checksums.txt"
	got=$(sha256sum "$tools/$tar_name" | cut -d' ' -f1)
	listed=$(awk -v f="$tar_name" '$2 == f {print $1}' "$tools/checksums.txt")
	if [ "$got" != "$HUGO_SHA256" ] || [ "$got" != "$listed" ]; then
		echo "Hugo tarball checksum mismatch: got $got, pinned $HUGO_SHA256, release lists $listed" >&2
		rm -f "$tools/$tar_name"
		exit 1
	fi
	tar -xzf "$tools/$tar_name" -C "$tools" hugo
fi

python3 -I "$here/check.py"
deb=()
[ -z "${GAMEDB_DEB_INDEX:-}" ] || deb=(--deb-index "$GAMEDB_DEB_INDEX")
python3 -I "$here/catalog.py" "${deb[@]}"

mkdir -p "$out"
args=(--source "$repo/gamedb" --destination "$out" --cleanDestinationDir --cacheDir "$repo/build/tools/hugo-cache")
[ -z "$base" ] || args+=(--baseURL "$base")
"$hugo" "${args[@]}"
cp "$repo/gamedb/data/catalog.json" "$out/catalog.json"

# the server's rules, read from walnut-watch itself so the two cannot drift apart
python3 -I - "$repo/tools/walnut-server/walnut-watch" "$out" <<'PY'
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
if bad:
    print("\n".join("site rules: " + b for b in bad), file=sys.stderr)
    sys.exit(1)
print("site rules: ok (%d files)" % sum(len(f) for _, _, f in os.walk(sys.argv[2])))
PY
